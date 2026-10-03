export type TaskflowNodeType = 
  | 'static'      // tf::Task - Tarefa determinística computacional
  | 'condition'   // tf::Task (retorna int) - Avaliação de branch estocástico/limiar
  | 'subflow'     // tf::Task (recebe tf::Subflow&) - Decomposição dinâmica em runtime
  | 'module'      // tf::Task (tf.composed_of) - Módulo hierárquico encapsulado
  | 'critical';   // tf::CriticalSection - Bloco com controle estrito de concorrência

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
