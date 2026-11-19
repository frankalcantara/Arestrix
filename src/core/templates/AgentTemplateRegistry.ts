import { IAgentTemplate, ISerializedTemplatePlugin, ITemplateParameterDef, AgentTemplateCategory, IPipelineInterface } from './AgentTemplateTypes.ts';
import { BUILTIN_TEMPLATES } from './BuiltinTemplates.ts';
import {
  BUNDLED_AGENT_VERTICES,
  getBundledAgentVertexById,
  type IBundledAgentVertexEntry
} from '../libraries/BuiltinLibraryCatalog.ts';
import type { LibraryService } from '../libraries/LibraryService.ts';
import { INodeData, INodeAgentSettings, IPort, IGraphSchema } from '../types.ts';
import type { Graph } from '../Graph.ts';

export type SaveNodeAsTemplateResult =
  | { status: 'created'; template: IAgentTemplate }
  | { status: 'duplicate'; existing: IAgentTemplate }
  | { status: 'unsupported'; reason: string };

function deepClone<T>(value: T): T {
  return value === undefined ? value : JSON.parse(JSON.stringify(value)) as T;
}

function stableValue(value: any): any {
  if (Array.isArray(value)) return value.map(stableValue);
  if (value && typeof value === 'object') {
    const out: Record<string, any> = {};
    Object.keys(value).sort().forEach(key => { out[key] = stableValue(value[key]); });
    return out;
  }
  return value;
}

function stableJson(value: any): string {
  return JSON.stringify(stableValue(value ?? null));
}

function defaultAgentSettings(): INodeAgentSettings {
  return {
    inheritSandbox: true,
    inheritModel: true,
    inheritCodeMode: true,
    inheritMemory: true
  };
}

function buildPipelineInterface(schema: IGraphSchema): IPipelineInterface {
  const outputs = schema.nodes
    .filter(node => node.type === 'output')
    .map(node => ({
      name: node.label || node.id,
      description: node.outputSettings?.artifactType
        ? `Saída (${node.outputSettings.artifactType})`
        : 'Saída do pipeline',
      sourcePath: node.outputSettings?.sourcePath
    }));
  return {
    input: { name: 'Entrada', description: 'Texto ou JSON enviado ao pipeline' },
    outputs: outputs.length > 0 ? outputs : [{ name: 'Resultado', description: 'Resultado do pipeline' }]
  };
}

function portSignature(ports: Array<{ name: string; type: string }>): string {
  // Compara nome, tipo e ordem; instantiateNode não preserva dataType nas saídas
  return JSON.stringify(ports.map(p => [p.name, p.type]));
}

function paramSignature(params: Record<string, unknown>): string {
  return JSON.stringify(Object.keys(params).sort());
}

const STORAGE_KEY = 'arestrix_custom_agent_plugins';
const LEGACY_STORAGE_KEY = 'taskforge_custom_agent_plugins';

export class AgentTemplateRegistry {
  private static instance: AgentTemplateRegistry | null = null;
  private templates: Map<string, IAgentTemplate> = new Map();

  constructor() {
    this.loadBuiltinTemplates();
    this.loadCustomPluginsFromStorage();
  }

  public static getInstance(): AgentTemplateRegistry {
    if (!AgentTemplateRegistry.instance) {
      AgentTemplateRegistry.instance = new AgentTemplateRegistry();
    }
    return AgentTemplateRegistry.instance;
  }

  private loadBuiltinTemplates(): void {
    BUILTIN_TEMPLATES.forEach(tpl => {
      this.templates.set(tpl.id, tpl);
    });
    for (const entry of BUNDLED_AGENT_VERTICES) {
      const tpl = this.bundledEntryToAgentTemplate(entry);
      if (!this.templates.has(tpl.id)) {
        this.templates.set(tpl.id, tpl);
      }
    }
  }

  private bundledEntryToAgentTemplate(entry: IBundledAgentVertexEntry): IAgentTemplate {
    const t = entry.template;
    const lambdaBody = t.lambdaBodyTemplate || '';
    return {
      id: t.id,
      name: t.name,
      category: (t.category as AgentTemplateCategory) || 'Agentes',
      icon: t.icon || entry.icon || '🤖',
      taskType: t.taskType || 'agent',
      description: t.description || entry.description || '',
      inputs: deepClone(t.inputs || [{ name: 'in', type: 'flow' }]),
      outputs: deepClone(t.outputs || [{ name: 'out', type: 'flow' }]),
      parameters: deepClone(t.parameters || {}),
      requiredHeaders: t.requiredHeaders ? [...t.requiredHeaders] : [],
      agentSettings: t.agentSettings ? deepClone(t.agentSettings) : defaultAgentSettings(),
      assetKind: t.assetKind || 'agent',
      presentation: t.presentation || 'agent',
      presetLocked: t.presetLocked !== false,
      isCustomPlugin: false,
      domain: entry.domain,
      subdomain: entry.subdomain,
      bundledInProduct: true,
      origin: deepClone(entry.origin),
      author: t.author || entry.origin?.repo || 'Arestrix',
      version: t.version || entry.origin?.installVersion || '1.0.0',
      generateLambdaBody: () => lambdaBody
    };
  }

  public getAllTemplates(): IAgentTemplate[] {
    return Array.from(this.templates.values());
  }

  public getTemplate(id: string): IAgentTemplate | undefined {
    if (!id) return undefined;
    const direct = this.templates.get(id);
    if (direct) return direct;
    const bundled = getBundledAgentVertexById(id);
    if (bundled) return this.bundledEntryToAgentTemplate(bundled);
    return undefined;
  }

  /**
   * Retorna um snapshot imutável de fábrica para um template embutido
   * (`bundledAgentVertices.json` ou `BUILTIN_TEMPLATES`), isolado de mutações em runtime.
   */
  public getInstallDefaultTemplate(templateId: string): IAgentTemplate | null {
    if (!templateId) return null;
    const bundled = getBundledAgentVertexById(templateId);
    if (bundled) {
      return this.bundledEntryToAgentTemplate(bundled);
    }
    const builtin = BUILTIN_TEMPLATES.find(t => t.id === templateId);
    if (!builtin) return null;
    return {
      ...builtin,
      inputs: deepClone(builtin.inputs),
      outputs: deepClone(builtin.outputs),
      parameters: deepClone(builtin.parameters),
      requiredHeaders: builtin.requiredHeaders ? [...builtin.requiredHeaders] : undefined,
      agentSettings: builtin.agentSettings ? deepClone(builtin.agentSettings) : undefined,
      childGraph: builtin.childGraph ? deepClone(builtin.childGraph) : undefined,
      pipelineInterface: builtin.pipelineInterface ? deepClone(builtin.pipelineInterface) : undefined,
      origin: builtin.origin ? deepClone(builtin.origin) : undefined
    };
  }

  private buildDefaultPortsAndParams(
    nodeId: string,
    tpl: Pick<IAgentTemplate, 'inputs' | 'outputs' | 'parameters'> | Pick<ISerializedTemplatePlugin, 'inputs' | 'outputs' | 'parameters'>
  ): { inputs: IPort[]; outputs: IPort[]; parameters: Record<string, string | number | boolean> } {
    const inputs: IPort[] = (tpl.inputs || [{ name: 'in', type: 'flow' }]).map((inp, idx) => ({
      id: `${nodeId}_in_${idx}`,
      name: inp.name,
      direction: 'input',
      type: inp.type,
      dataType: inp.dataType
    }));
    const outputs: IPort[] = (tpl.outputs || [{ name: 'out', type: 'flow' }]).map((out, idx) => ({
      id: `${nodeId}_out_${idx}`,
      name: out.name,
      direction: 'output',
      type: out.type,
      conditionBranchIndex: out.conditionBranchIndex !== undefined ? out.conditionBranchIndex : idx
    }));
    const parameters: Record<string, string | number | boolean> = {};
    Object.entries(tpl.parameters || {}).forEach(([key, paramDef]) => {
      parameters[key] = paramDef.defaultValue;
    });
    return { inputs, outputs, parameters };
  }

  /**
   * Constrói um patch de restauração aos padrões originais de instalação para
   * um vértice modificado ou derivado, preservando a identidade de origem (`origin`).
   */
  public buildInstallDefaultNodePatch(
    node: INodeData,
    libraryService?: LibraryService
  ): Partial<INodeData> | null {
    const candidateIds: string[] = [];
    const addCandidate = (value?: string) => {
      if (value && !candidateIds.includes(value)) candidateIds.push(value);
    };

    if (libraryService && node.librarySource?.assetId) {
      const currentAsset = libraryService.getAsset(node.librarySource.assetId);
      addCandidate(currentAsset?.derivedFrom?.assetId);
      addCandidate(currentAsset?.origin?.assetId);
    }

    addCandidate(node.origin?.assetId);
    addCandidate(node.origin?.templateId);
    addCandidate(node.librarySource?.assetId);
    addCandidate(node.templateId);
    addCandidate(node.presetId);
    addCandidate(node.origin?.canonicalKey);

    for (const candidate of candidateIds) {
      if (libraryService) {
        const defaultAsset = libraryService.getInstallDefaultAsset(candidate);
        if (defaultAsset?.payload.kind === 'agent-template' && defaultAsset.origin) {
          const tpl = defaultAsset.payload.template;
          const originCopy = deepClone(defaultAsset.origin);
          const { inputs, outputs, parameters } = this.buildDefaultPortsAndParams(node.id, tpl);
          return {
            label: tpl.name,
            agentSettings: tpl.agentSettings ? deepClone(tpl.agentSettings) : defaultAgentSettings(),
            inputs,
            outputs,
            parameters,
            presetLocked: true,
            presetId: tpl.id,
            templateId: tpl.id,
            librarySource: {
              libraryId: originCopy.libraryId || defaultAsset.libraryId,
              assetId: defaultAsset.id,
              hash: originCopy.installHash || defaultAsset.audit.hash,
              version: originCopy.installVersion || defaultAsset.version,
              bundledInProduct: true,
              originRepo: originCopy.repo,
              originUrl: originCopy.url,
              originLicense: originCopy.license
            },
            origin: originCopy
          };
        }
      }

      const bundledEntry = getBundledAgentVertexById(candidate);
      if (bundledEntry) {
        const tpl = bundledEntry.template;
        const originCopy = deepClone(bundledEntry.origin);
        const { inputs, outputs, parameters } = this.buildDefaultPortsAndParams(node.id, tpl);
        return {
          label: tpl.name,
          agentSettings: tpl.agentSettings ? deepClone(tpl.agentSettings) : defaultAgentSettings(),
          inputs,
          outputs,
          parameters,
          presetLocked: true,
          presetId: tpl.id,
          templateId: tpl.id,
          librarySource: {
            libraryId: originCopy.libraryId || 'library-arestrix',
            assetId: bundledEntry.id,
            hash: originCopy.installHash,
            version: originCopy.installVersion || tpl.version || '1.0.0',
            bundledInProduct: true,
            originRepo: originCopy.repo,
            originUrl: originCopy.url,
            originLicense: originCopy.license
          },
          origin: originCopy
        };
      }
    }

    const fallbackId = node.templateId || node.presetId;
    const builtinTpl = fallbackId ? this.getInstallDefaultTemplate(fallbackId) : null;
    if (!builtinTpl) return null;

    const { inputs, outputs, parameters } = this.buildDefaultPortsAndParams(node.id, builtinTpl);
    return {
      label: builtinTpl.name,
      agentSettings: builtinTpl.agentSettings ? deepClone(builtinTpl.agentSettings) : undefined,
      inputs,
      outputs,
      parameters,
      presetLocked: builtinTpl.presetLocked !== false,
      presetId: builtinTpl.id,
      templateId: builtinTpl.id,
      librarySource: node.librarySource ? { ...node.librarySource } : undefined,
      origin: builtinTpl.origin ? deepClone(builtinTpl.origin) : undefined
    };
  }

  public getTemplatesByCategory(): Map<string, IAgentTemplate[]> {
    const map = new Map<string, IAgentTemplate[]>();
    this.templates.forEach(tpl => {
      const cat = tpl.category || 'Outros';
      if (!map.has(cat)) {
        map.set(cat, []);
      }
      map.get(cat)!.push(tpl);
    });
    return map;
  }

  public registerTemplate(template: IAgentTemplate): void {
    this.templates.set(template.id, template);
    if (template.isCustomPlugin) {
      this.saveCustomPluginsToStorage();
    }
  }

  public unregisterTemplate(id: string): boolean {
    const tpl = this.templates.get(id);
    if (!tpl) return false;
    
    // Não permite apagar built-ins se não quiser, ou permite apenas custom
    if (!tpl.isCustomPlugin) {
      console.warn(`Tentativa de remover template built-in: ${id}`);
    }
    const res = this.templates.delete(id);
    this.saveCustomPluginsToStorage();
    return res;
  }

  /**
   * Instancia um nó do Arestrix a partir de um template.
   */
  public instantiateNode(
    templateId: string, 
    customLabel?: string, 
    x: number = 0, 
    y: number = 0,
    customParams?: Record<string, any>
  ): INodeData {
    const tpl = this.getTemplate(templateId);
    if (!tpl) {
      throw new Error(`Template com id "${templateId}" não encontrado no catálogo.`);
    }

    const uniqueSuffix = Date.now().toString().slice(-6);
    const id = `${tpl.id}_${uniqueSuffix}`;
    const label = customLabel || `${tpl.name} ${uniqueSuffix}`;

    // Parâmetros mesclados com padrões
    const parameters: Record<string, string | number | boolean> = {};
    Object.entries(tpl.parameters).forEach(([key, paramDef]) => {
      parameters[key] = paramDef.defaultValue;
    });
    if (customParams) {
      Object.assign(parameters, customParams);
    }

    // Portas de Entrada
    const inputs: IPort[] = tpl.inputs.map((inp, idx) => ({
      id: `${id}_in_${idx}`,
      name: inp.name,
      direction: 'input',
      type: inp.type,
      dataType: inp.dataType
    }));

    // Portas de Saída
    const outputs: IPort[] = tpl.outputs.map((out, idx) => ({
      id: `${id}_out_${idx}`,
      name: out.name,
      direction: 'output',
      type: out.type,
      conditionBranchIndex: out.conditionBranchIndex !== undefined ? out.conditionBranchIndex : idx
    }));

    // Se for condition e tiver menos de 2 portas, garante 2 portas
    if (tpl.taskType === 'condition' && outputs.length < 2) {
      outputs.push({
        id: `${id}_out_1`,
        name: 'ramo 1',
        direction: 'output',
        type: 'flow',
        conditionBranchIndex: 1
      });
    }

    const lambdaBody = tpl.generateLambdaBody(parameters, label);

    const nodeData: INodeData = {
      id,
      label,
      type: tpl.taskType,
      position: { x, y, z: 0 },
      inputs,
      outputs,
      parameters,
      customLambdaBody: lambdaBody,
      templateId: tpl.id,
      requiredHeaders: tpl.requiredHeaders
    };

    if (tpl.taskType === 'agent') {
      nodeData.agentSettings = deepClone(tpl.agentSettings) ?? defaultAgentSettings();
      nodeData.presetId = tpl.id;
      nodeData.presetLocked = tpl.presetLocked !== false;
    }
    if (tpl.childGraph) nodeData.childGraph = deepClone(tpl.childGraph);
    if (tpl.pipelineInterface) nodeData.pipelineInterface = deepClone(tpl.pipelineInterface);
    if (tpl.presentation === 'agent') nodeData.presentation = 'agent';
    if (tpl.origin) {
      nodeData.origin = deepClone(tpl.origin);
      if (tpl.bundledInProduct || tpl.origin.bundledInProduct) {
        const bundledEntry = getBundledAgentVertexById(tpl.origin.assetId || tpl.id);
        nodeData.librarySource = {
          libraryId: tpl.origin.libraryId || 'library-arestrix',
          assetId: bundledEntry?.id || tpl.origin.assetId,
          hash: tpl.origin.installHash,
          version: tpl.origin.installVersion || tpl.version || '1.0.0',
          bundledInProduct: true,
          originRepo: tpl.origin.repo,
          originUrl: tpl.origin.url,
          originLicense: tpl.origin.license
        };
      }
    }

    if (tpl.createChildGraph) {
      nodeData.childGraph = tpl.createChildGraph(label, parameters);
    }

    return nodeData;
  }

  /**
   * Procura no catálogo um template equivalente ao vértice: mesmo tipo, mesmas portas,
   * mesmos parâmetros e o mesmo corpo C++ que o template geraria para o vértice.
   */
  public findEquivalentTemplate(node: INodeData): IAgentTemplate | undefined {
    const inSig = portSignature(node.inputs);
    const outSig = portSignature(node.outputs);
    const pSig = paramSignature(node.parameters);
    for (const tpl of this.templates.values()) {
      if (tpl.taskType !== node.type) continue;
      if (portSignature(tpl.inputs) !== inSig || portSignature(tpl.outputs) !== outSig) continue;
      if (paramSignature(tpl.parameters) !== pSig) continue;
      if (tpl.taskType === 'agent' &&
          stableJson(tpl.agentSettings ?? null) !== stableJson(node.agentSettings ?? null)) continue;
      try {
        if (tpl.generateLambdaBody({ ...node.parameters }, node.label) === (node.customLambdaBody ?? '')) {
          return tpl;
        }
      } catch {
        // template incapaz de gerar corpo com estes parâmetros: não é equivalente
      }
    }
    return undefined;
  }

  /**
   * Salva um vértice do grafo como template em "Plugins Customizados".
   * Não inclui se já existir template equivalente no catálogo.
   */
  public saveNodeAsTemplate(node: INodeData): SaveNodeAsTemplateResult {
    if (node.childGraph && node.childGraph.nodes?.length) {
      return { status: 'unsupported', reason: 'Vértices com subgrafo interno ainda não podem virar template.' };
    }
    const existing = this.findEquivalentTemplate(node);
    if (existing) return { status: 'duplicate', existing };

    const lambda = node.customLambdaBody ?? '';
    const bodyTemplate = node.label ? lambda.split(node.label).join('${label}') : lambda;
    const isAgent = node.type === 'agent';

    const parameters: Record<string, ITemplateParameterDef> = {};
    Object.entries(node.parameters).forEach(([k, v]) => {
      parameters[k] = {
        label: k,
        type: typeof v === 'number' ? 'number' : typeof v === 'boolean' ? 'boolean' : 'string',
        defaultValue: v,
        description: ''
      };
    });

    let id: string;
    do {
      id = `custom_${node.type}_${Date.now().toString(36)}${Math.random().toString(36).slice(2, 5)}`;
    } while (this.templates.has(id));

    const template: IAgentTemplate = {
      id,
      name: node.label,
      category: isAgent ? 'Agentes' : 'Plugins Customizados',
      icon: isAgent ? '🤖' : '🧩',
      taskType: node.type,
      description: isAgent
        ? `Agente salvo a partir do vértice "${node.label}"`
        : `Template salvo a partir do vértice "${node.label}"`,
      inputs: node.inputs.map(p => ({ name: p.name, type: p.type as 'flow' | 'data' | 'call', dataType: p.dataType })),
      outputs: node.outputs.map(p => ({
        name: p.name,
        type: p.type as 'flow' | 'data' | 'call',
        dataType: p.dataType,
        conditionBranchIndex: p.conditionBranchIndex
      })),
      parameters,
      requiredHeaders: node.requiredHeaders,
      agentSettings: isAgent ? (deepClone(node.agentSettings) ?? defaultAgentSettings()) : undefined,
      assetKind: isAgent ? 'agent' : 'vertex',
      presetLocked: isAgent ? true : undefined,
      isCustomPlugin: true,
      bundledInProduct: node.origin?.bundledInProduct ?? node.librarySource?.bundledInProduct,
      origin: node.origin ? deepClone(node.origin) : undefined,
      author: 'Usuário Arestrix',
      version: '1.0.0',
      generateLambdaBody: (_params, label) => bodyTemplate.split('${label}').join(label)
    };
    this.registerTemplate(template);
    return { status: 'created', template };
  }

  /**
   * Empacota o grafo inteiro como um ativo de pipeline. Na interface ele é
   * apresentado como um único vértice agente; internamente continua sendo um
   * subflow composto.
   */
  public saveGraphAsPipelineTemplate(graph: Graph, name?: string): IAgentTemplate {
    return this.importGraphAsPipelineTemplate(graph.toJSON(), name);
  }

  /**
   * Importa um `pipelines/*.json` de outro projeto como ativo de pipeline.
   * O grafo é validado estruturalmente e guardado como snapshot imutável.
   */
  public importGraphAsPipelineTemplate(schema: IGraphSchema, name?: string): IAgentTemplate {
    if (!schema || typeof schema !== 'object' ||
        !Array.isArray(schema.nodes) || !Array.isArray(schema.edges)) {
      throw new Error('Pipeline inválido: o JSON precisa conter arrays nodes e edges.');
    }
    if (schema.nodes.length === 0) {
      throw new Error('Pipeline vazio: não há vértices para importar.');
    }
    const metadata = schema.metadata;
    let id: string;
    do {
      id = `pipeline_${Date.now().toString(36)}${Math.random().toString(36).slice(2, 5)}`;
    } while (this.templates.has(id));

    const template: IAgentTemplate = {
      id,
      name: name?.trim() || metadata?.graphName || 'Pipeline',
      category: 'Agentes',
      icon: '🧠',
      taskType: 'subflow',
      description: metadata?.description || 'Pipeline completo empacotado como agente visual.',
      inputs: [{ name: 'in', type: 'flow' }],
      outputs: [{ name: 'out', type: 'flow' }],
      parameters: {},
      childGraph: deepClone(schema),
      pipelineInterface: buildPipelineInterface(schema),
      assetKind: 'pipeline',
      presentation: 'agent',
      presetLocked: true,
      isCustomPlugin: true,
      author: 'Usuário Arestrix',
      version: '1.0.0',
      generateLambdaBody: () => ''
    };
    this.registerTemplate(template);
    return template;
  }

  /**
   * Exporta os plugins customizados em formato JSON para distribuição.
   */
  public exportCustomPluginsAsJSON(): string {
    const customList: ISerializedTemplatePlugin[] = [];
    this.templates.forEach(tpl => {
      if (tpl.isCustomPlugin) {
        customList.push(this.serializeTemplate(tpl));
      }
    });
    return JSON.stringify(customList, null, 2);
  }

  /**
   * Exporta todo o catálogo em formato JSON.
   */
  public exportAllTemplatesAsJSON(): string {
    const list: ISerializedTemplatePlugin[] = [];
    this.templates.forEach(tpl => {
      list.push(this.serializeTemplate(tpl));
    });
    return JSON.stringify(list, null, 2);
  }

  /** Exporta um template individual no formato serializado usado pela biblioteca. */
  public exportTemplateAsJSON(id: string): string | null {
    const tpl = this.templates.get(id);
    return tpl ? JSON.stringify(this.serializeTemplate(tpl), null, 2) : null;
  }

  /**
   * Importa plugins a partir de uma string JSON.
   */
  public importTemplatesFromJSON(jsonString: string): { success: number; errors: string[] } {
    const errors: string[] = [];
    let success = 0;

    try {
      const parsed = JSON.parse(jsonString);
      const items: ISerializedTemplatePlugin[] = Array.isArray(parsed) ? parsed : [parsed];

      items.forEach((item, index) => {
        if (!item.id || !item.name || !item.taskType) {
          errors.push(`Item #${index + 1}: Faltam campos obrigatórios (id, name, taskType).`);
          return;
        }

        try {
          const tpl: IAgentTemplate = {
            id: item.id,
            name: item.name,
            category: (item.category as AgentTemplateCategory) ||
              (item.taskType === 'agent' ? 'Agentes' : 'Plugins Customizados'),
            icon: item.icon || (item.taskType === 'agent' ? '🤖' : '🧩'),
            taskType: item.taskType,
            description: item.description || 'Plugin importado dinamicamente',
            inputs: item.inputs || [{ name: 'in', type: 'flow' }],
            outputs: item.outputs || (item.taskType === 'condition' ? [
              { name: 'ramo 0', type: 'flow', conditionBranchIndex: 0 },
              { name: 'ramo 1', type: 'flow', conditionBranchIndex: 1 }
            ] : [{ name: 'out', type: 'flow' }]),
            parameters: item.parameters || {},
            requiredHeaders: item.requiredHeaders || ['<syncstream>'],
            agentSettings: item.agentSettings
              ? deepClone(item.agentSettings)
              : (item.taskType === 'agent' ? defaultAgentSettings() : undefined),
            assetKind: item.assetKind,
            childGraph: item.childGraph ? deepClone(item.childGraph) : undefined,
            pipelineInterface: item.pipelineInterface ? deepClone(item.pipelineInterface) : undefined,
            presentation: item.presentation,
            presetLocked: item.taskType === 'agent' || item.assetKind === 'pipeline'
              ? (item.presetLocked !== false) : undefined,
            isCustomPlugin: !item.bundledInProduct,
            domain: item.domain,
            subdomain: item.subdomain,
            bundledInProduct: item.bundledInProduct,
            origin: item.origin ? deepClone(item.origin) : undefined,
            author: item.author || 'Usuário Arestrix',
            version: item.version || '1.0.0',
            generateLambdaBody: (params, label) => {
              if ((item.taskType === 'agent' || item.assetKind === 'pipeline') && !item.lambdaBodyTemplate) return '';
              let body = item.lambdaBodyTemplate || '// Plugin de Tarefa Customizado\nstd::cout << "[Plugin] ' + label + '\\n";';
              // Substitui placeholders ${params.xxx} ou ${label}
              body = body.replace(/\${label}/g, label);
              Object.entries(params).forEach(([k, v]) => {
                const rx = new RegExp(`\\\${params\\.${k}}`, 'g');
                body = body.replace(rx, String(v));
              });
              return body;
            }
          };

          this.registerTemplate(tpl);
          success++;
        } catch (e: any) {
          errors.push(`Erro ao carregar "${item.name}": ${e.message}`);
        }
      });
    } catch (e: any) {
      errors.push(`JSON Inválido: ${e.message}`);
    }

    return { success, errors };
  }

  private serializeTemplate(tpl: IAgentTemplate): ISerializedTemplatePlugin {
    // Para lambda, gera com parâmetros padrão
    const dummyParams: Record<string, any> = {};
    Object.keys(tpl.parameters).forEach(k => {
      dummyParams[k] = `\${params.${k}}`;
    });
    const sampleLambda = tpl.generateLambdaBody(dummyParams, '${label}');

    return {
      id: tpl.id,
      name: tpl.name,
      category: tpl.category,
      icon: tpl.icon,
      taskType: tpl.taskType,
      description: tpl.description,
      inputs: tpl.inputs,
      outputs: tpl.outputs,
      parameters: tpl.parameters,
      requiredHeaders: tpl.requiredHeaders,
      lambdaBodyTemplate: sampleLambda,
      agentSettings: tpl.agentSettings ? deepClone(tpl.agentSettings) : undefined,
      assetKind: tpl.assetKind,
      childGraph: tpl.childGraph ? deepClone(tpl.childGraph) : undefined,
      pipelineInterface: tpl.pipelineInterface ? deepClone(tpl.pipelineInterface) : undefined,
      presentation: tpl.presentation,
      presetLocked: tpl.presetLocked,
      domain: tpl.domain,
      subdomain: tpl.subdomain,
      bundledInProduct: tpl.bundledInProduct,
      origin: tpl.origin ? deepClone(tpl.origin) : undefined,
      author: tpl.author,
      version: tpl.version
    };
  }

  private saveCustomPluginsToStorage(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      const customPlugins: ISerializedTemplatePlugin[] = [];
      this.templates.forEach(tpl => {
        if (tpl.isCustomPlugin) {
          customPlugins.push(this.serializeTemplate(tpl));
        }
      });
      localStorage.setItem(STORAGE_KEY, JSON.stringify(customPlugins));
    } catch (e) {
      console.error('Falha ao salvar plugins customizados no localStorage:', e);
    }
  }

  private loadCustomPluginsFromStorage(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      let raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) {
        raw = localStorage.getItem(LEGACY_STORAGE_KEY);
      }
      if (raw) {
        this.importTemplatesFromJSON(raw);
      }
    } catch (e) {
      console.error('Falha ao restaurar plugins customizados do localStorage:', e);
    }
  }
}
