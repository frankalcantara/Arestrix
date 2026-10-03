import { TaskflowNodeType, IGraphSchema } from '../types.ts';

export interface ITemplateParameterDef {
  label: string;
  type: 'string' | 'number' | 'boolean' | 'select';
  defaultValue: string | number | boolean;
  description: string;
  options?: string[]; // Para tipo select
}

export interface IAgentTemplate {
  id: string;
  name: string;
  category: 'IA & Inferência' | 'Concorrência & Filas' | 'Armazenamento & Dados' | 'Visão & Processamento' | 'Síntese & Orquestração' | 'Plugins Customizados';
  icon: string;
  taskType: TaskflowNodeType;
  description: string;
  inputs: Array<{ name: string; type: 'flow' | 'data'; dataType?: string }>;
  outputs: Array<{ name: string; type: 'flow' | 'data'; dataType?: string; conditionBranchIndex?: number }>;
  parameters: Record<string, ITemplateParameterDef>;
  requiredHeaders?: string[];
  generateLambdaBody: (params: Record<string, any>, label: string) => string;
  createChildGraph?: (label: string, params: Record<string, any>) => IGraphSchema;
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
  inputs: Array<{ name: string; type: 'flow' | 'data'; dataType?: string }>;
  outputs: Array<{ name: string; type: 'flow' | 'data'; conditionBranchIndex?: number }>;
  parameters?: Record<string, ITemplateParameterDef>;
  requiredHeaders?: string[];
  lambdaBodyTemplate: string;
  author?: string;
  version?: string;
}
