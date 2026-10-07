export type TaskflowNodeType = 
  | 'static'      // tf::Task - Tarefa determinística computacional
  | 'condition'   // tf::Task (retorna int) - Avaliação de branch estocástico/limiar
  | 'subflow'     // tf::Task (recebe tf::Subflow&) - Decomposição dinâmica em runtime
  | 'module'      // tf::Task (tf.composed_of) - Módulo hierárquico encapsulado
  | 'critical'    // tf::CriticalSection - Bloco com controle estrito de concorrencia
  | 'cuda'        // tf::cudaFlow - Agente com Aceleracao CUDA C++20
  | 'agent';      // Agente Autônomo Code Mode em Sandbox/VM Isolada

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
  enableCuda: boolean; // tf::cudaFlow
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
}

export interface IGlobalPipelineDefaults {
  sandbox: ISandboxPolicy;
  model: IModelConfig;
  codeMode: ICodeModeConfig;
}

export interface INodeAgentSettings {
  inheritSandbox: boolean;
  sandbox?: Partial<ISandboxPolicy>;

  inheritModel: boolean;
  model?: Partial<IModelConfig>;

  inheritCodeMode: boolean;
  codeMode?: Partial<ICodeModeConfig>;
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
  graphName: string;
  description: string;
  targetStandard: 'C++20';
  compilerConfig: ICompilerConfig;
  version: string;
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
  taskflowIncludePath: 'include',
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
    readOnlyMounts: ['plano.md', '.agents/'],
    readWriteMounts: ['aulas/']
  },
  model: {
    provider: 'deepseek',
    model: 'deepseek-flash',
    skillPath: '.agents/redigir-math/SKILL.md',
    promptTemplate: 'Processe o artigo  conforme as diretrizes do plano.md'
  },
  codeMode: {
    allowedTools: ['write', 'edit', 'read', 'bash', 'validate_svg'],
    customScripts: ['python scripts/audit_exercises.py']
  }
};
