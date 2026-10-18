import { EventBus, globalEventBus } from '../EventBus.ts';
import { Graph } from '../Graph.ts';
import { PipelineRunner } from '../PipelineRunner.ts';
import { ProjectManager } from '../ProjectManager.ts';
import { IProjectRunnerConfig } from '../types.ts';

export type RunnerStatus = 'idle' | 'compiling' | 'running' | 'success' | 'error' | 'stopped';

export interface IRunnerNodeState {
  id: string;
  label: string;
  type: string;
  status: 'waiting' | 'running' | 'success' | 'error';
  startedAt?: number;
  durationMs?: number;
  outputPreview?: string;
}

export interface IRunnerToolCallRecord {
  id: string;
  timestamp: string;
  toolName: string;
  nodeId?: string;
  success: boolean;
  durationMs?: number;
  argsSummary?: string;
}

export interface IRunnerThinkingRecord {
  id: string;
  timestamp: string;
  nodeId: string;
  agentName: string;
  thought: string;
}

export interface IRunnerExportResult {
  ok: boolean;
  error?: string;
  outputDir?: string;
  executablePath?: string;
  manifestPath?: string;
  signaturePath?: string;
  packagingArtifacts?: string[];
}

export interface IRunnerDiagnostics {
  totalTokens: number;
  promptTokens: number;
  completionTokens: number;
  toolCallsCount: number;
  rateLimitRetries: number;
  avgLatencyMs: number;
  toolCalls: IRunnerToolCallRecord[];
  thinkingLogs: IRunnerThinkingRecord[];
  errors: Array<{ timestamp: string; stage: string; message: string }>;
}

export class RunnerManager {
  private static instance: RunnerManager | null = null;
  private bus: EventBus;
  private projectManager: ProjectManager;

  // Estado geral
  private status: RunnerStatus = 'idle';
  private statusMessage: string = 'Pronto para execução';
  private progressPercent: number = 0;
  private startTime: number | null = null;
  private elapsedMs: number = 0;
  private timerInterval: any = null;

  // Topologia de etapas
  private nodes: Map<string, IRunnerNodeState> = new Map();
  private activeNodeId: string | null = null;

  // Diagnóstico
  private diagnostics: IRunnerDiagnostics = {
    totalTokens: 0,
    promptTokens: 0,
    completionTokens: 0,
    toolCallsCount: 0,
    rateLimitRetries: 0,
    avgLatencyMs: 0,
    toolCalls: [],
    thinkingLogs: [],
    errors: []
  };

  // Parâmetros de operação definidos pelo usuário
  private paramValues: Record<string, any> = {};

  private constructor(bus: EventBus = globalEventBus, pm: ProjectManager = ProjectManager.getInstance()) {
    this.bus = bus;
    this.projectManager = pm;
    this.bindBusEvents();
  }

  public static getInstance(bus: EventBus = globalEventBus, pm: ProjectManager = ProjectManager.getInstance()): RunnerManager {
    if (!RunnerManager.instance) {
      RunnerManager.instance = new RunnerManager(bus, pm);
    }
    return RunnerManager.instance;
  }

  public static resetInstance(): void {
    if (RunnerManager.instance) {
      RunnerManager.instance.stopTimer();
      RunnerManager.instance = null;
    }
  }

  public initGraph(graph: Graph): void {
    this.nodes.clear();
    const gNodes = typeof graph.getAllNodes === 'function' ? graph.getAllNodes() : (typeof (graph as any).getNodes === 'function' ? (graph as any).getNodes() : []);
    gNodes.forEach((n: any) => {
      this.nodes.set(n.id, {
        id: n.id,
        label: n.label || n.id,
        type: n.type || 'Task',
        status: 'waiting'
      });
    });
    this.calculateProgress();
    this.bus.emit('runner:state-changed', this.getState());
  }

  private bindBusEvents(): void {
    this.bus.on('pipeline:compiling', (msg: string) => {
      this.status = 'compiling';
      this.statusMessage = msg || 'Compilando C++ Taskflow...';
      this.progressPercent = 10;
      this.startTimer();
      this.bus.emit('runner:state-changed', this.getState());
    });

    this.bus.on('pipeline:running', (msg: string) => {
      this.status = 'running';
      this.statusMessage = msg || 'Executando pipeline...';
      if (this.progressPercent < 25) this.progressPercent = 25;
      this.bus.emit('runner:state-changed', this.getState());
    });

    this.bus.on('pipeline:success', (data: { stdout: string; elapsedMs: number }) => {
      this.status = 'success';
      this.statusMessage = 'Execução concluída com sucesso!';
      this.progressPercent = 100;
      this.elapsedMs = data.elapsedMs || (this.startTime ? Date.now() - this.startTime : 0);
      this.stopTimer();

      // Marca todos os nós pendentes como sucesso
      this.nodes.forEach(n => {
        if (n.status === 'running' || n.status === 'waiting') {
          n.status = 'success';
        }
      });
      this.activeNodeId = null;
      this.bus.emit('runner:state-changed', this.getState());
    });

    this.bus.on('pipeline:error', (data: { stage: string; error: string }) => {
      this.status = 'error';
      this.statusMessage = `Erro na etapa [${data.stage}]: ${data.error}`;
      this.stopTimer();

      if (this.activeNodeId && this.nodes.has(this.activeNodeId)) {
        const n = this.nodes.get(this.activeNodeId)!;
        n.status = 'error';
      }

      this.diagnostics.errors.push({
        timestamp: new Date().toLocaleTimeString(),
        stage: data.stage,
        message: data.error
      });

      this.bus.emit('runner:state-changed', this.getState());
    });

    this.bus.on('pipeline:stopped', () => {
      this.status = 'stopped';
      this.statusMessage = 'Execução interrompida pelo operador.';
      this.stopTimer();
      this.bus.emit('runner:state-changed', this.getState());
    });

    // Tarefas do Taskflow Observer
    this.bus.on('telemetry:task-enter', (data: { taskId: string; worker?: number }) => {
      const node = this.findNodeByTaskName(data.taskId);
      if (node) {
        node.status = 'running';
        node.startedAt = Date.now();
        this.activeNodeId = node.id;
        this.statusMessage = `Executando: ${node.label}`;
        this.calculateProgress();
        this.bus.emit('runner:state-changed', this.getState());
      }
    });

    this.bus.on('telemetry:task-exit', (data: { taskId: string; worker?: number }) => {
      const node = this.findNodeByTaskName(data.taskId);
      if (node) {
        node.status = 'success';
        if (node.startedAt) {
          node.durationMs = Date.now() - node.startedAt;
        }
        if (this.activeNodeId === node.id) {
          this.activeNodeId = null;
        }
        this.calculateProgress();
        this.bus.emit('runner:state-changed', this.getState());
      }
    });

    // Telemetria de Agentes
    this.bus.on('agent:telemetry', (ev: any) => {
      this.handleAgentTelemetry(ev);
    });
  }

  private findNodeByTaskName(taskName: string): IRunnerNodeState | undefined {
    if (!taskName) return undefined;
    const clean = taskName.trim();
    for (const [, n] of this.nodes) {
      if (n.id === clean || n.label === clean || clean.includes(n.id) || clean.includes(n.label)) {
        return n;
      }
    }
    return undefined;
  }

  private calculateProgress(): void {
    if (this.nodes.size === 0) return;
    let completed = 0;
    this.nodes.forEach(n => {
      if (n.status === 'success') completed += 1;
      else if (n.status === 'running') completed += 0.5;
    });
    const calculated = Math.min(95, Math.max(15, Math.round((completed / this.nodes.size) * 100)));
    if (this.status === 'running') {
      this.progressPercent = calculated;
    }
  }

  private startTimer(): void {
    this.stopTimer();
    this.startTime = Date.now();
    this.timerInterval = setInterval(() => {
      if (this.startTime) {
        this.elapsedMs = Date.now() - this.startTime;
        this.bus.emit('runner:tick', { elapsedMs: this.elapsedMs });
      }
    }, 250);
  }

  private stopTimer(): void {
    if (this.timerInterval) {
      clearInterval(this.timerInterval);
      this.timerInterval = null;
    }
  }

  private handleAgentTelemetry(ev: any): void {
    if (!ev) return;

    // Tokens
    if (ev.type === 'usage' || (ev.usage && typeof ev.usage === 'object')) {
      const u = ev.usage || ev;
      const prompt = Number(u.prompt_tokens || u.promptTokens || 0);
      const completion = Number(u.completion_tokens || u.completionTokens || 0);
      const total = Number(u.total_tokens || u.totalTokens || (prompt + completion));
      this.diagnostics.promptTokens += prompt;
      this.diagnostics.completionTokens += completion;
      this.diagnostics.totalTokens += total;
    }

    // Thinking stream
    if (ev.type === 'thinking' && ev.thought) {
      this.diagnostics.thinkingLogs.push({
        id: `thk-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`,
        timestamp: new Date().toLocaleTimeString(),
        nodeId: ev.nodeId || this.activeNodeId || '',
        agentName: ev.agentName || 'Agente',
        thought: String(ev.thought)
      });
      if (this.diagnostics.thinkingLogs.length > 200) {
        this.diagnostics.thinkingLogs.shift();
      }
    }

    // Chamadas de ferramentas (Tool Call)
    if (ev.type === 'tool_call' || ev.type === 'tool_result') {
      this.diagnostics.toolCallsCount += 1;
      this.diagnostics.toolCalls.push({
        id: `tool-${Date.now()}-${Math.random().toString(36).substring(2, 6)}`,
        timestamp: new Date().toLocaleTimeString(),
        toolName: ev.toolName || ev.tool || 'ferramenta',
        nodeId: ev.nodeId || this.activeNodeId || '',
        success: ev.isError !== true && ev.error === undefined,
        durationMs: ev.durationMs || 0,
        argsSummary: ev.args ? (typeof ev.args === 'string' ? ev.args : JSON.stringify(ev.args)) : ''
      });
      if (this.diagnostics.toolCalls.length > 200) {
        this.diagnostics.toolCalls.shift();
      }
    }

    // Retries 429
    if (ev.type === 'rate_limit' || ev.status === 429 || ev.retry) {
      this.diagnostics.rateLimitRetries += 1;
    }

    this.bus.emit('runner:diagnostics-updated', this.diagnostics);
  }

  // Comandos de Operação
  public async runPipeline(graph: Graph): Promise<boolean> {
    this.initGraph(graph);
    return await PipelineRunner.run(graph);
  }

  public async stopPipeline(): Promise<void> {
    await PipelineRunner.stop();
  }

  public async restartPipeline(graph: Graph): Promise<boolean> {
    await this.stopPipeline();
    await new Promise(r => setTimeout(r, 400));
    return await this.runPipeline(graph);
  }

  // Parâmetros operacionais
  public setParameter(id: string, value: any): void {
    this.paramValues[id] = value;
  }

  public getParameter(id: string): any {
    return this.paramValues[id];
  }

  public getParameters(): Record<string, any> {
    return { ...this.paramValues };
  }

  // Getters de Estado
  public getState() {
    return {
      status: this.status,
      statusMessage: this.statusMessage,
      progressPercent: this.progressPercent,
      elapsedMs: this.elapsedMs,
      activeNodeId: this.activeNodeId,
      nodes: Array.from(this.nodes.values()),
      diagnostics: { ...this.diagnostics }
    };
  }

  public getDiagnostics(): IRunnerDiagnostics {
    return { ...this.diagnostics };
  }

  public resetDiagnostics(): void {
    this.diagnostics = {
      totalTokens: 0,
      promptTokens: 0,
      completionTokens: 0,
      toolCallsCount: 0,
      rateLimitRetries: 0,
      avgLatencyMs: 0,
      toolCalls: [],
      thinkingLogs: [],
      errors: []
    };
    this.bus.emit('runner:diagnostics-updated', this.diagnostics);
  }

  public static auditTextForSecrets(text: string): { ok: boolean; error?: string } {
    if (!text) return { ok: true };

    const tokenPatterns = [
      { name: 'chave sk-... (OpenAI/DeepSeek/Anthropic)', re: /\bsk-[A-Za-z0-9][A-Za-z0-9_-]{19,}/ },
      { name: 'chave Google (AIza...)', re: /\bAIza[0-9A-Za-z_-]{30,}/ },
      { name: 'chave Groq (gsk_...)', re: /\bgsk_[A-Za-z0-9]{30,}/ },
      { name: 'token GitHub', re: /\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})/ },
      { name: 'chave privada', re: /-----BEGIN [A-Z ]*PRIVATE KEY-----/ },
      { name: 'JWT', re: /\beyJ[A-Za-z0-9_-]{15,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}/ },
      { name: 'cabecalho Bearer com token', re: /\bBearer\s+[A-Za-z0-9._~+/=-]{24,}/ }
    ];

    const placeholderHints = /(test|fake|mock|dummy|example|sample|placeholder|changeme|your[-_]?|xxxx|\*{3,}|\.{3}|<[^>]+>|redacted)/i;
    const envRefRe = /^\$\{[A-Za-z_][A-Za-z0-9_]*\}$|^%[A-Za-z_][A-Za-z0-9_]*%$|^\$[A-Za-z_][A-Za-z0-9_]*$/;
    const envNameRe = /^[A-Z][A-Z0-9_]{2,}$/;

    for (const pat of tokenPatterns) {
      const match = text.match(pat.re);
      if (match) {
        const val = match[0];
        if (!placeholderHints.test(val) && !envRefRe.test(val) && !envNameRe.test(val)) {
          return {
            ok: false,
            error: `Segredo proibido detectado: ${pat.name} (${val.substring(0, 4)}... - ${val.length} caracteres)`
          };
        }
      }
    }

    const assignmentRe = /["']?\b([A-Za-z0-9_.-]*(?:api[_-]?key|secret|passw(?:or)?d|passwd|token|credential|private[_-]?key)[A-Za-z0-9_.-]*)["']?\s*[:=]\s*["']([^"'\r\n]{14,})["']/gi;
    let m: RegExpExecArray | null;
    while ((m = assignmentRe.exec(text)) !== null) {
      const val = m[2].trim();
      if (!placeholderHints.test(val) && !envRefRe.test(val) && !envNameRe.test(val)) {
        return {
          ok: false,
          error: `Atribuição de credencial/senha detectada: ${m[1]} (${val.substring(0, 4)}... - ${val.length} caracteres)`
        };
      }
    }

    return { ok: true };
  }

  // Exportação Standalone RPC
  public async exportStandaloneRunner(graph: Graph, config?: Partial<IProjectRunnerConfig>): Promise<IRunnerExportResult> {
    const cur = this.projectManager.getCurrentProject();
    const runnerConfig: IProjectRunnerConfig = {
      ...(cur.runner || {
        enabled: true,
        title: `${cur.name} Runner`,
        description: cur.description,
        version: cur.version,
        outputDir: 'dist/runner',
        outputName: 'runner_app.exe',
        autoStart: false,
        defaultTab: 'progress',
        allowRestart: true,
        parameters: []
      }),
      ...config
    };

    const payload = {
      runner: runnerConfig,
      graph: graph.toJSON()
    };
    const payloadStr = JSON.stringify(payload);

    // Auditoria preventiva imediata no frontend
    const audit = RunnerManager.auditTextForSecrets(payloadStr);
    if (!audit.ok) {
      return {
        ok: false,
        error: `Exportação bloqueada por política de segurança (fail-closed): ${audit.error}`
      };
    }

    // @ts-ignore
    if (typeof webui === 'undefined') {
      return {
        ok: false,
        error: 'Ponte WebUI C++ não disponível para exportação nativa.'
      };
    }

    try {
      // @ts-ignore
      const raw = await webui.call('export_standalone_runner', payloadStr);
      const res: IRunnerExportResult = typeof raw === 'string' && raw ? JSON.parse(raw) : { ok: false, error: 'Resposta nula do backend' };
      if (res.ok) {
        this.projectManager.updateProjectMetadata({ runner: runnerConfig });
        await this.projectManager.saveCurrentProject();
      }
      return res;
    } catch (e: any) {
      return {
        ok: false,
        error: e?.message || String(e)
      };
    }
  }

  public async launchStandaloneRunner(executablePath: string): Promise<boolean> {
    // @ts-ignore
    if (typeof webui === 'undefined') return false;
    try {
      // @ts-ignore
      const raw = await webui.call('launch_standalone_runner', JSON.stringify({ executablePath }));
      const res = typeof raw === 'string' && raw ? JSON.parse(raw) : { ok: false };
      return !!res.ok;
    } catch (_) {
      return false;
    }
  }
}
