import { TaskflowNodeType, IGraphSchema, INodeAgentSettings } from '../types.ts';

export interface ITemplateParameterDef {
  label: string;
  type: 'string' | 'number' | 'boolean' | 'select';
  defaultValue: string | number | boolean;
  description: string;
  options?: string[]; // Para tipo select
}

export type AgentTemplateCategory =
  | 'Agentes'
  | 'IA & Inferência'
  | 'Concorrência & Filas'
  | 'Armazenamento & Dados'
  | 'Visão & Processamento'
  | 'Síntese & Orquestração'
  | 'Plugins Customizados';

export interface IPipelineInterface {
  input: { name: string; description?: string };
  /** Campos adicionais; o runtime recebe { input, parameters } em JSON quando houver algum. */
  inputs?: Array<{ name: string; description?: string; required?: boolean; defaultValue?: string }>;
  outputs: Array<{ name: string; description?: string; sourcePath?: string }>;
}

export interface IAgentTemplate {
  id: string;
  name: string;
  category: AgentTemplateCategory;
  icon: string;
  taskType: TaskflowNodeType;
  description: string;
  inputs: Array<{ name: string; type: 'flow' | 'data' | 'call'; dataType?: string }>;
  outputs: Array<{ name: string; type: 'flow' | 'data' | 'call'; dataType?: string; conditionBranchIndex?: number }>;
  parameters: Record<string, ITemplateParameterDef>;
  requiredHeaders?: string[];
  generateLambdaBody: (params: Record<string, any>, label: string) => string;
  createChildGraph?: (label: string, params: Record<string, any>) => IGraphSchema;
  /** Configuração completa do agente; obrigatória para taskType === 'agent'. */
  agentSettings?: INodeAgentSettings;
  /** Pipeline/projeto empacotado como ativo de biblioteca. */
  assetKind?: 'vertex' | 'agent' | 'pipeline';
  childGraph?: IGraphSchema;
  pipelineInterface?: IPipelineInterface;
  /** Apresentação visual: pipeline aparece como agente no canvas. */
  presentation?: 'agent';
  /** Agente de biblioteca pronto: configuração travada na interface. */
  presetLocked?: boolean;
  isCustomPlugin?: boolean;
  author?: string;
  version?: string;
}

export interface ISerializedTemplatePlugin {
  id: string;
  name: string;
  category?: string;
  icon?: string;
  taskType: TaskflowNodeType;
  description: string;
  inputs: Array<{ name: string; type: 'flow' | 'data' | 'call'; dataType?: string }>;
  outputs: Array<{ name: string; type: 'flow' | 'data' | 'call'; conditionBranchIndex?: number }>;
  parameters?: Record<string, ITemplateParameterDef>;
  requiredHeaders?: string[];
  lambdaBodyTemplate: string;
  /** Configuração do agente preservada no round-trip catálogo → biblioteca → grafo. */
  agentSettings?: INodeAgentSettings;
  assetKind?: 'vertex' | 'agent' | 'pipeline';
  childGraph?: IGraphSchema;
  pipelineInterface?: IPipelineInterface;
  presentation?: 'agent';
  /** Agente de biblioteca pronto: configuração travada na interface. */
  presetLocked?: boolean;
  author?: string;
  version?: string;
}
