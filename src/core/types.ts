export type TaskflowNodeType = 
  | 'static'      // tf::Task - Tarefa determinística computacional
  | 'condition'   // tf::Task (retorna int) - Avaliação de branch estocástico/limiar
  | 'subflow'     // tf::Task (recebe tf::Subflow&) - Decomposição dinâmica em runtime
  | 'module'      // tf::Task (tf.composed_of) - Módulo hierárquico encapsulado
  | 'critical'    // tf::CriticalSection - Bloco com controle estrito de concorrencia
  | 'cuda'        // tf::cudaGraph / cudaGraphExec - Agente com Aceleracao CUDA C++20 (Taskflow 4.1+)
  | 'agent'       // Agente Autônomo Code Mode em Sandbox/VM Isolada
  | 'fork'        // tf::Task - Despachante Concorrente / Disparo Paralelo
  | 'join'        // tf::Task - Barreira de Sincronização / Consolidação Concorrente
  | 'switch'      // tf::Task (retorna int) - Seleção multivia: um caso por porta de saída; o último é o 'default'
  | 'mcp';        // tf::Task - Chama UMA ferramenta de um servidor MCP do projeto (cliente C++ nativo, sessão persistente)

export type PortDirection = 'input' | 'output';
export type PortType = 'flow' | 'data';

export type TargetCompiler = 'msvc' | 'gcc' | 'clang';
export type BuildSystem = 'cmake' | 'makefile' | 'direct_script';

export interface ICompilerConfig {
  compiler: TargetCompiler;
  cppStandard: 'C++20';
  buildSystem: BuildSystem;
  taskflowIncludePath: string;
  outputBinaryName: string;
  enableOpenMP: boolean;
  enableCuda: boolean; // tf::cudaGraph / cudaGraphExec (Taskflow 4.1+)
  extraFlags: string[];
}

export interface IPort {
  id: string;
  name: string;
  direction: PortDirection;
  type: PortType;
  dataType?: string;     // Ex: 'std::string', 'AgentState', 'double'
  conditionBranchIndex?: number; // Para nós de condição (0, 1, 2...)
}


// --- Configuracoes de Agentes, Code Mode e Sandbox/VM ---

export type LLMProvider = 'anthropic' | 'deepseek' | 'google' | 'openai' | 'jev' | 'local';

export type SandboxIsolationLevel = 'container' | 'microvm' | 'host_direct';
export type SandboxNetworkPolicy = 'offline' | 'egress_llm_only' | 'full';

export interface ISandboxPolicy {
  isolationLevel: SandboxIsolationLevel;
  network: SandboxNetworkPolicy;
  maxMemoryMB: number;
  timeoutSeconds: number;
  readOnlyMounts: string[];
  readWriteMounts: string[];
}

export interface IModelConfig {
  provider: LLMProvider;
  model: string;
  skillPath?: string;
  promptTemplate?: string;
  temperature?: number;
}

export type CodeModeTool = 'write' | 'edit' | 'read' | 'bash' | 'validate_svg';

export interface ICodeModeConfig {
  allowedTools: CodeModeTool[];
  customScripts: string[];
  mcpServers?: string[];      // IDs dos servidores MCP ativos
  allowedMcpTools?: string[]; // Lista de nomes de ferramentas MCP autorizadas para o agente
}

// --- Integração Model Context Protocol (MCP) (Fase 13) ---
export type McpTransportType = 'stdio' | 'sse';

export interface IMcpToolDefinition {
  name: string;
  description: string;
  inputSchema?: Record<string, any>;
  serverId: string;
}

export interface IMcpServerConfig {
  id: string;
  name: string;
  transport: McpTransportType;
  command?: string;
  args?: string[];
  env?: Record<string, string>;
  url?: string;
  isEnabled: boolean;
  cachedTools?: IMcpToolDefinition[];
}

export interface IMcpCallResult {
  success: boolean;
  content: string;
  isError: boolean;
  errorMessage?: string;
  errorCode?: string;    // codigo estavel para o i18n (mcp_errors.<codigo>)
  errorDetail?: string;  // parte dinamica (id do servidor, host, variavel...)
  durationSeconds?: number;
}

// --- Arquitetura de Memória Heterogênea (Fase 12) ---
// Memória NÃO é padrão e nem obrigatória (Opt-in seletivo por agente em cada projeto)
export interface INodeMemoryConfig {
  enabled: boolean;          // Padrão: false (Não é padrão e nem obrigatória)
  persistent: boolean;       // false = efêmera (apenas sessão/RAM); true = persistente entre sessões (disco)
  projectId?: string;        // Identificador / namespace do projeto para isolamento estrito
  readBlackboard: boolean;   // Leitura da memória de trabalho intra-pipeline
  writeBlackboard: boolean;  // Escrita de deltas no Blackboard
  semanticLessons: boolean;  // Consulta de lições aprendidas (semântica)
  recordLessons: boolean;    // Registro de novas lições derivadas de falhas/auditorias
  allowedKeys?: string[];    // Chaves específicas do Blackboard (vazio = todas autorizadas)
  domains?: string[];        // Domínios semânticos (ex: ["svg_typography", "validation"])
}

export const DEFAULT_NODE_MEMORY_CONFIG: INodeMemoryConfig = {
  enabled: false,
  persistent: false,
  readBlackboard: false,
  writeBlackboard: false,
  semanticLessons: false,
  recordLessons: false,
  allowedKeys: [],
  domains: []
};

export interface IGlobalPipelineDefaults {
  sandbox: ISandboxPolicy;
  model: IModelConfig;
  codeMode: ICodeModeConfig;
  memory?: INodeMemoryConfig;
}

export interface INodeSkill {
  id: string;
  name: string;
  description: string;
  markdownContent: string;  // Conteúdo Markdown completo do SKILL.md
  systemPrompt: string;     // Prompt operacional/regras do sistema
  tools: string[];          // Ferramentas autorizadas
  license?: string;         // Licença (MIT, Apache-2.0, etc.)
  sourceUrl?: string;       // URL do repositório/arquivo no GitHub
  updatedAt?: string;       // Timestamp da última edição
}

export interface IGitHubSkillRepo {
  id: string;
  repo: string;             // ex: "anthropics/skills-hub"
  name: string;
  description: string;
  license: string;          // ex: "MIT"
  branch: string;           // "main"
  isDefault: boolean;
  isEnabled: boolean;
}

export interface IGitHubSkillItem {
  id: string;
  name: string;
  description: string;
  repoId: string;
  repoName: string;
  category: string;
  markdownContent: string;
  systemPrompt: string;
  tools: string[];
  license: string;
  githubUrl?: string;
}

export interface INodeAgentSettings {
  inheritSandbox: boolean;
  sandbox?: Partial<ISandboxPolicy>;

  inheritModel: boolean;
  model?: Partial<IModelConfig>;

  inheritCodeMode: boolean;
  codeMode?: Partial<ICodeModeConfig>;

  inheritMemory?: boolean;
  memory?: Partial<INodeMemoryConfig>;

  // Habilidades e Skills completas do Agente (Conteúdo Markdown & Prompts)
  skills?: INodeSkill[];
  activeSkillId?: string;

  // Validação de Artefato & Resume Engine (Fase 11)
  validationRules?: IArtifactValidationRule[];
  isSerialAcrossBatch?: boolean; // Se esta etapa roda sequencialmente entre os itens do lote
}

// --- Pipelines Parametrizados em Lote & Resume Engine (Fase 11) ---

export interface IArtifactValidationRule {
  targetPathPattern: string;      // ex: "aulas/{itemId}.md"
  minSizeBytes?: number;           // ex: 1000
  contentPattern?: string;        // ex: "^---[\\s\\S]+---"
  checkLogExitCode?: boolean;     // true procura "EXIT CODE: 0"
}

export type StageCheckpointStatus = 'pendente' | 'executando' | 'concluido' | 'erro';

export interface IStageCheckpointInfo {
  status: StageCheckpointStatus;
  completedAt?: string;
  startedAt?: string;
  logFile?: string;
  artifactPath?: string;
  recoveredFromArtifact?: boolean;
  errorMessage?: string;
}

export interface IItemCheckpointInfo {
  itemId: string;
  status: 'pendente' | 'em_progresso' | 'concluido' | 'erro';
  stages: Record<string, IStageCheckpointInfo>;
}

export interface IPipelineCheckpointState {
  pipelineId: string;
  lastUpdated: string;
  items: Record<string, IItemCheckpointInfo>;
}

export interface IBatchPipelineConfig {
  items: string[];
  serializedStages: string[];
  maxConcurrency: number;
  cooldownMs: number;
  stateFilePath: string;
  autoResume: boolean;
}


// --- Telemetria de Agentes em Tempo Real (Fase 10) ---

export type AgentExecutionStatus = 'idle' | 'running' | 'thinking' | 'tool_call' | 'settled' | 'error' | 'success';

export interface IAgentTelemetryPayload {
  thinkingChars?: number;
  thinkingPreview?: string;
  toolName?: string;
  toolArgs?: string;
  toolSuccess?: boolean;
  retrySeconds?: number;
  status?: AgentExecutionStatus;
  detail?: string;
  logLine?: string;
}

export interface IAgentTelemetryEvent {
  nodeId: string;
  type: 'thinking' | 'tool_start' | 'tool_end' | 'rate_limit' | 'status' | 'log';
  timestamp: number;
  payload: IAgentTelemetryPayload;
}

/** Como um vértice switch escolhe o caminho: sem código (modelo de IA ou palavras) ou com código C++. */
export type SwitchMode = 'model' | 'rules' | 'code';

export interface ISwitchSettings {
  mode: SwitchMode;
  /** De onde vem o texto analisado: resposta final do vértice anterior ou um arquivo do projeto. */
  source: { kind: 'previous' | 'file'; path?: string };
  /** Modo 'rules': palavras de cada caso (por id da porta), separadas por vírgula. */
  rules: Record<string, string>;
  /** Modo 'model': orientação extra para o modelo ("escolha alto se houver dados pessoais..."). */
  instruction?: string;
}

export function defaultSwitchSettings(mode: SwitchMode = 'model'): ISwitchSettings {
  return { mode, source: { kind: 'previous' }, rules: {}, instruction: '' };
}

/** Vértice MCP: uma chamada de ferramenta, sem modelo de IA. Os argumentos aceitam {{input}} e {{input.campo}}. */
export interface IMcpVertexSettings {
  /** id de um servidor configurado em .workspace/mcps do projeto */
  server: string;
  tool: string;
  arguments: Record<string, unknown>;
  /** De onde vem o texto que alimenta {{input}}: saída do vértice anterior, um arquivo do projeto ou nada. */
  source: { kind: 'previous' | 'file' | 'none'; path?: string };
  /** Além de .arestrix/out/<id>.txt (lido pelos vértices seguintes), grava o resultado neste arquivo do projeto. */
  output: { file?: string };
}

export function defaultMcpSettings(): IMcpVertexSettings {
  return { server: '', tool: '', arguments: {}, source: { kind: 'previous' }, output: {} };
}

export interface INodeData {
  id: string;
  label: string;
  type: TaskflowNodeType;
  position: { x: number; y: number; z?: number };
  inputs: IPort[];
  outputs: IPort[];
  parameters: Record<string, string | number | boolean>;
  customLambdaBody?: string;
  criticalSectionId?: string; // ID da CriticalSection associada, se houver
  childGraph?: IGraphSchema;  // Subgrafo hierárquico aninhado (Subflow / Module)
  templateId?: string;        // ID do template de origem (opcional)
  requiredHeaders?: string[]; // Headers C++20 adicionais requeridos por este agente
  agentSettings?: INodeAgentSettings;
  switchSettings?: ISwitchSettings; // só para vértices do tipo 'switch'
  mcpSettings?: IMcpVertexSettings; // só para vértices do tipo 'mcp'
}

export interface IBreadcrumbItem {
  nodeId?: string;
  title: string;
}

export interface IEdgeData {
  id: string;
  sourceNodeId: string;
  sourcePortId: string;
  targetNodeId: string;
  targetPortId: string;
  isFeedbackLoop?: boolean; // True quando for um retorno condicional
}

export interface IGraphMetadata {
  projectId?: string;
  graphName: string;
  description: string;
  targetStandard: 'C++20';
  compilerConfig: ICompilerConfig;
  version: string;
  globalDefaults?: IGlobalPipelineDefaults;
}

// --- Conceito de Projetos & Workspaces Isolados (Fase 14) ---

export interface IProjectPipelineRef {
  id: string;
  name: string;
  file: string; // caminho relativo a raiz do workspace (ex: "pipelines/main.json")
  isPrimary: boolean;
  description?: string;
}

export interface IProjectDatabaseConfig {
  type: 'sqlite' | 'postgres';
  filePath?: string;
  url?: string;
}

export interface IProjectManifest {
  projectId: string;
  name: string;
  version: string;
  description: string;
  createdAt: string;
  updatedAt: string;
  rootPath: string;
  outputDir?: string;
  pipelines: IProjectPipelineRef[];
  globalDefaults?: IGlobalPipelineDefaults;
  database?: IProjectDatabaseConfig;
  environment?: Record<string, string>;
}

export interface IRecentProjectItem {
  projectId: string;
  name: string;
  rootPath: string;
  manifestPath: string;
  lastOpened: string;
}

// Itens de uma pasta na tela Abrir (list_openable_entries do backend C++).
export type OpenableEntryKind = 'folder' | 'project' | 'package' | 'manifest' | 'json';

export interface IOpenableEntry {
  name: string;
  path: string;
  kind: OpenableEntryKind;
  modified?: number; // epoch em ms
}

export interface IOpenableListing {
  path: string;
  parent: string; // vazio na raiz de uma unidade
  exists: boolean;
  truncated: boolean;
  entries: IOpenableEntry[];
  available: boolean; // false fora do aplicativo desktop (sem ponte WebUI)
}

export interface IFilePickFilter {
  name: string;
  pattern: string; // padroes separados por ';' (ex: "*.arestrix;*.json")
}

export interface IOpenOptions {
  // Pergunta antes de um grafo avulso (fora de uma pasta de projeto) substituir o canvas do projeto atual.
  confirmLooseGraph?: (fileName: string) => boolean;
}

export interface IOpenResult {
  ok: boolean;
  kind?: 'project' | 'package' | 'graph';
  path?: string;
  reason?: 'cancelled' | 'invalid' | 'unsupported';
}

export interface IArestrixBundle {
  format: 'arestrix-package';
  version: string;
  generator?: string;
  exportedAt: string;
  manifest: IProjectManifest;
  pipelines: Record<string, IGraphSchema | any>;
  environment?: Record<string, string>;
  globalDefaults?: IGlobalPipelineDefaults;
}

export interface IGraphSchema {
  version: string;
  metadata: IGraphMetadata;
  nodes: INodeData[];
  edges: IEdgeData[];
}

export interface IValidationError {
  type: 'cycle' | 'orphan' | 'invalid_port' | 'missing_branch';
  message: string;
  nodeIds?: string[];
  edgeIds?: string[];
  severity: 'error' | 'warning';
}

export interface IValidationResult {
  isValid: boolean;
  errors: IValidationError[];
}

export const DEFAULT_COMPILER_CONFIG: ICompilerConfig = {
  compiler: 'msvc',
  cppStandard: 'C++20',
  buildSystem: 'cmake',
  taskflowIncludePath: 'third_party/taskflow',
  outputBinaryName: 'pipeline_app',
  enableOpenMP: false,
  enableCuda: false,
  extraFlags: ['/std:c++20', '/EHsc', '/O2']
};


export const DEFAULT_PIPELINE_DEFAULTS: IGlobalPipelineDefaults = {
  sandbox: {
    isolationLevel: 'container',
    network: 'egress_llm_only',
    maxMemoryMB: 512,
    timeoutSeconds: 300,
    readOnlyMounts: [],
    readWriteMounts: []
  },
  model: {
    provider: 'deepseek',
    model: 'deepseek-flash',
    skillPath: '',
    promptTemplate: ''
  },
  codeMode: {
    allowedTools: ['write', 'edit', 'read', 'bash', 'validate_svg'],
    customScripts: []
  },
  memory: {
    ...DEFAULT_NODE_MEMORY_CONFIG
  }
};
