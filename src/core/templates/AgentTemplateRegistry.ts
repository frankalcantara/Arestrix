import { IAgentTemplate, ISerializedTemplatePlugin, ITemplateParameterDef } from './AgentTemplateTypes.ts';
import { BUILTIN_TEMPLATES } from './BuiltinTemplates.ts';
import { INodeData, IPort } from '../types.ts';

export type SaveNodeAsTemplateResult =
  | { status: 'created'; template: IAgentTemplate }
  | { status: 'duplicate'; existing: IAgentTemplate }
  | { status: 'unsupported'; reason: string };

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
  }

  public getAllTemplates(): IAgentTemplate[] {
    return Array.from(this.templates.values());
  }

  public getTemplate(id: string): IAgentTemplate | undefined {
    return this.templates.get(id);
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
    const tpl = this.templates.get(templateId);
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
      category: 'Plugins Customizados',
      icon: '🧩',
      taskType: node.type,
      description: `Template salvo a partir do vértice "${node.label}"`,
      inputs: node.inputs.map(p => ({ name: p.name, type: p.type as 'flow' | 'data', dataType: p.dataType })),
      outputs: node.outputs.map(p => ({
        name: p.name,
        type: p.type as 'flow' | 'data',
        dataType: p.dataType,
        conditionBranchIndex: p.conditionBranchIndex
      })),
      parameters,
      requiredHeaders: node.requiredHeaders,
      isCustomPlugin: true,
      author: 'Usuário Arestrix',
      version: '1.0.0',
      generateLambdaBody: (_params, label) => bodyTemplate.split('${label}').join(label)
    };
    this.registerTemplate(template);
    return { status: 'created', template };
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
            category: (item.category as any) || 'Plugins Customizados',
            icon: item.icon || '🧩',
            taskType: item.taskType,
            description: item.description || 'Plugin importado dinamicamente',
            inputs: item.inputs || [{ name: 'in', type: 'flow' }],
            outputs: item.outputs || (item.taskType === 'condition' ? [
              { name: 'ramo 0', type: 'flow', conditionBranchIndex: 0 },
              { name: 'ramo 1', type: 'flow', conditionBranchIndex: 1 }
            ] : [{ name: 'out', type: 'flow' }]),
            parameters: item.parameters || {},
            requiredHeaders: item.requiredHeaders || ['<syncstream>'],
            isCustomPlugin: true,
            author: item.author || 'Usuário Arestrix',
            version: item.version || '1.0.0',
            generateLambdaBody: (params, label) => {
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
