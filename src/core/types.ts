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
  | 'mcp'         // tf::Task - Chama UMA ferramenta de um servidor MCP do projeto (cliente C++ nativo, sessão persistente)
  | 'interaction' // Interação humana: formulário seguro, canal Studio/Runner/CLI (Fase 2)
  | 'call'        // Vértice Call: executa um alvo chamável dentro do fluxo (Fase 4C)
  | 'output';     // Vértice de Entrega / Coletor de Artefatos (SVG, planilha, PDF, Markdown, texto, JSON...)

export type PortDirection = 'input' | 'output';
export type PortType = 'flow' | 'data' | 'call';

/**
 * Semântica da aresta:
 *  - flow: dependência normal de execução (sólida);
 *  - feedback: retorno de um laço condicional (tracejada âmbar);
 *  - call: declaração de chamada/delegação, não agenda o destino (tracejada violeta).
 * Arestas antigas sem `kind` são tratadas como `flow`.
 */
export type EdgeKind = 'flow' | 'feedback' | 'call';

export interface ICallSettings {
  /** Máximo de invocações desta aresta por execução do chamador. */
  maxCalls: number;
  /** 0 = sem timeout próprio; o chamador ainda tem maxTurns. */
  timeoutSeconds?: number;
  /** fail: devolve erro ao chamador; abort: derruba o pipeline. */
  onError?: 'fail' | 'abort';
  /** Texto injetado na descrição da ferramenta, para o modelo saber quando usar. */
  description?: string;
  /** Emite uma notificação assíncrona quando a chamada conclui ou falha. */
  notify?: boolean;
  /** Permite que o modelo inicie esta chamada com async=true. */
  allowAsync?: boolean;
}

export function defaultCallSettings(): ICallSettings {
  return { maxCalls: 1, timeoutSeconds: 0, onError: 'fail', description: '', notify: true, allowAsync: false };
}

/**
 * Configuração do vértice Call (chamada em fluxo, Fase 4C).
 * O alvo vem da única aresta `call` que sai da porta CALL do vértice.
 */
export interface ICallVertexSettings {
  /** Payload fixo. Quando vazio, o input é coletado conforme `source`. */
  input?: string;
  /** De onde vem o input quando `input` está vazio. */
  source?: { kind: 'previous' | 'file' | 'none'; path?: string };
}

export function defaultCallVertexSettings(): ICallVertexSettings {
  return { input: '', source: { kind: 'previous', path: '' } };
}

export interface ICallLimits {
  maxDepth: number;
  maxTotalCalls: number;
}

export const DEFAULT_CALL_LIMITS: ICallLimits = {
  maxDepth: 3,
  maxTotalCalls: 50
};

// --- Interação humana (Fase 2) -------------------------------------------------------------

export type FormFieldType =
  | 'text' | 'textarea' | 'number' | 'boolean'
  | 'choice' | 'multi_choice' | 'date';

export interface IFormOption {
  value: string;
  label: string;
}

export interface IFormField {
  id: string;
  type: FormFieldType;
  label: string;
  required?: boolean;
  options?: IFormOption[];
  defaultValue?: unknown;
  help?: string;
  placeholder?: string;
  multiline?: boolean;
  maxLength?: number;
  min?: number;
  max?: number;
}

export interface IFormContext {
  markdown?: string;
  artifacts?: Array<{ path: string; type: string; label?: string }>;
}

export interface IFormSchema {
  title?: string;
  description?: string;
  context?: IFormContext;
  fields: IFormField[];
  submitLabel?: string;
  cancelLabel?: string;
}

export type InteractionMode = 'form' | 'choice' | 'approval' | 'free_text';
export type InteractionSchemaSource = 'static' | 'vertex' | 'caller';
export type InteractionChannel = 'studio' | 'runner' | 'cli' | 'any';
export type InteractionTimeoutPolicy = 'fail' | 'default' | 'abort';

export interface IInteractionSettings {
  mode: InteractionMode;
  schemaSource: InteractionSchemaSource;
  form: IFormSchema;
  /** Só para schemaSource = 'vertex'. */
  generator?: {
    provider: string;
    model: string;
    temperature?: number;
    /** fail: aborta se o modelo/chave/schema falhar; free_text: cai para uma pergunta livre. */
    fallback?: 'fail' | 'free_text';
  };
  channel: InteractionChannel;
  /** v1: sempre true. O campo existe para o v2. */
  wait: true;
  timeoutSeconds: number;
  onTimeout: InteractionTimeoutPolicy;
  defaultAnswers?: Record<string, unknown>;
  who?: 'operator' | 'end_user' | 'any';
  sensitive?: boolean;
}

export function defaultInteractionSettings(label: string = 'Pergunta'): IInteractionSettings {
  return {
    mode: 'form',
    schemaSource: 'static',
    form: {
      title: label,
      description: '',
      context: { markdown: '' },
      fields: [
        { id: 'resposta', type: 'textarea', label: 'Resposta', required: true, multiline: true }
      ],
      submitLabel: 'Continuar',
      cancelLabel: 'Cancelar'
    },
    channel: 'any',
    wait: true,
    timeoutSeconds: 300,
    onTimeout: 'fail',
    who: 'any',
    sensitive: false
  };
}

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

export type LLMProvider = 'anthropic' | 'deepseek' | 'google' | 'mistral' | 'openai' | 'jev' | 'local';

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
  /** Headers HTTP (ex.: X-API-Key). Valores devem usar ${VARIAVEL}; segredos nunca ficam no arquivo. */
  headers?: Record<string, string>;
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
  allowedKeys?: string[];    // Chaves específicas do Blackboard (vazio = todas autorizadas para leitura)
  writeKeys?: string[];      // Chaves que o vértice pode escrever (vazio = todas)
  domains?: string[];        // Domínios semânticos (ex: ["svg_typography", "validation"])
  contextBudgetTokens?: number; // Orçamento aproximado do brief injetado (padrão 800; 0 = desativado)
  /** Máximo de lições que o agente pode gravar em uma execução (padrão 20). */
  maxLessonsPerRun?: number;
  /** Tamanho máximo, em caracteres, de uma lição/trigger (padrão 4000). */
  maxLessonChars?: number;
  /** Retenção de telemetria (episódios/hits/usage) em dias; 0 = manter. Nível de projeto. */
  retentionDays?: number;
  /** normal = usa a configuração dos vértices; memory_off = força OFF no projeto (A/B). */
  measurementMode?: 'normal' | 'memory_off';
  /** Mantém a gravação de memory_usage mesmo quando a memória está OFF. */
  recordMetrics?: boolean;
  /**
   * Embeddings L2/M2 (nível de projeto; ignorado em configurações de vértice).
   * Desligados por padrão: a recuperação continua 100% lexical.
   */
  embeddingEnabled?: boolean;
  embeddingProvider?: 'local' | 'none';
  /** Modelo OpenAI-compatible de embeddings. Padrão: EmbeddingGemma 2 (768d). */
  embeddingModel?: string;
  /** Endpoint base, ex.: http://127.0.0.1:11434/v1. Vazio usa ARESTRIX_EMBEDDING_BASE_URL/LOCAL_LLM_BASE_URL. */
  embeddingBaseUrl?: string;
  embeddingTimeoutSeconds?: number;
  embeddingBatchSize?: number;
  embeddingMaxChars?: number;
  /** 0 = descobre a dimensão na primeira resposta. */
  embeddingDimensions?: number;
}

export const DEFAULT_NODE_MEMORY_CONFIG: INodeMemoryConfig = {
  enabled: false,
  persistent: false,
  readBlackboard: false,
  writeBlackboard: false,
  semanticLessons: false,
  recordLessons: false,
  allowedKeys: [],
  writeKeys: [],
  domains: [],
  contextBudgetTokens: 800,
  maxLessonsPerRun: 20,
  maxLessonChars: 4000,
  retentionDays: 180,
  measurementMode: 'normal',
  recordMetrics: false,
  embeddingEnabled: false,
  embeddingProvider: 'local',
  embeddingModel: 'embeddinggemma-2',
  embeddingBaseUrl: '',
  embeddingTimeoutSeconds: 60,
  embeddingBatchSize: 8,
  embeddingMaxChars: 8000,
  embeddingDimensions: 768
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
  /** Campo legado da ribbon contextual; o gerador C++ usa source/rules. */
  variableName?: string;
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

/** Tipo de formato do artefato produzido para renderização no visualizador integrado. */
export type OutputArtifactType = 'auto' | 'svg' | 'sheet' | 'pdf' | 'markdown' | 'text' | 'json' | 'image' | 'binary';

/** Vértice de Entrega / Saída: define onde salvar e registrar os entregáveis gerados pelo pipeline. */
export interface IOutputSettings {
  sourcePath: string;               // ex: 'workspace/scene.svg'
  destinationPath?: string;          // ex: 'exports/scene.svg'
  artifactType: OutputArtifactType;  // Tipo/formato do artefato para visualização rica
  displayName?: string;             // Nome amigável exibido no painel de Artefatos
  autoOpenPreview?: boolean;        // Se deve abrir automaticamente a visualização ao concluir
  description?: string;             // Descrição do entregável
  /** Campo legado da ribbon para padrões de coleta; o gerador C++ usa sourcePath/destinationPath. */
  pattern?: string;
}

export function defaultOutputSettings(): IOutputSettings {
  return {
    sourcePath: '',
    destinationPath: '',
    artifactType: 'auto',
    displayName: '',
    autoOpenPreview: true,
    description: ''
  };
}

export interface IArtifactItem {
  id: string;
  nodeId?: string;
  name: string;
  path: string;
  relativePath: string;
  type: OutputArtifactType;
  sizeBytes?: number;
  modifiedAt?: string;
  content?: string;
  description?: string;
}

/** Configurações de execução usadas pela ribbon contextual (algumas ainda não são lidas pelo gerador C++). */
export interface IRetryPolicy {
  maxRetries: number;
  backoffMs?: number;
}

export interface IConditionSettings {
  expression?: string;
  stochasticProbability?: number;
}

export interface ICudaSettings {
  asyncStream?: boolean;
}

export interface ISubflowSettings {
  maxRecursionDepth?: number;
}

export interface ICriticalSettings {
  mutexKey?: string;
  maxConcurrency?: number;
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
  outputSettings?: IOutputSettings; // só para vértices do tipo 'output'
  interactionSettings?: IInteractionSettings; // só para vértices do tipo 'interaction'
  callSettings?: ICallVertexSettings; // só para vértices do tipo 'call'
  // Campos da ribbon contextual. Permanecem opcionais para compatibilidade com
  // grafos antigos; nem todos são consumidos pelo gerador C++ atual.
  retryPolicy?: IRetryPolicy;
  conditionSettings?: IConditionSettings;
  cudaSettings?: ICudaSettings;
  subflowSettings?: ISubflowSettings;
  criticalSettings?: ICriticalSettings;
}

export interface IBreadcrumbItem {
  nodeId?: string;
  title: string;
}

/**
 * Offsets relativos em coordenadas de mundo (Three.js: +X direita, +Y cima)
 * para as alças de controle de curva de Bézier cúbica estilo Inkscape.
 * cp1Offset é relativo ao ponto inicial (porta de saída) e cp2Offset ao ponto final (porta de entrada).
 */
export interface IEdgeControlPoints {
  cp1Offset: { x: number; y: number };
  cp2Offset: { x: number; y: number };
}

export interface IEdgeData {
  id: string;
  sourceNodeId: string;
  sourcePortId: string;
  targetNodeId: string;
  targetPortId: string;
  /** flow (padrão), feedback ou call. */
  kind?: EdgeKind;
  /** Só para kind = call. */
  callSettings?: ICallSettings;
  isFeedbackLoop?: boolean; // True quando for um retorno condicional
  controlPoints?: IEdgeControlPoints; // Alças Bézier customizadas pelo usuário (estilo Inkscape)
}

export interface IGraphMetadata {
  projectId?: string;
  graphName: string;
  description: string;
  targetStandard: 'C++20';
  compilerConfig: ICompilerConfig;
  version: string;
  globalDefaults?: IGlobalPipelineDefaults;
  callLimits?: ICallLimits;
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

export interface IProjectGitConfig {
  enabled: boolean;
  provider: 'github' | 'gitlab' | 'gitea' | 'custom';
  serverUrl: string;
  username: string; // apenas identificador (sem senhas/tokens)
  repositoryName: string; // nome do repositorio remoto (privado)
  remoteUrl: string; // URL do repositorio remoto
  isPrivate: boolean; // repositorios criados pelo Arestrix sao privados
  autoCommitOnRun: boolean; // commit local a cada execucao (F5)
  autoCommitOnShutdown: boolean; // commit e push ao encerrar o sistema
  timedPushEnabled?: boolean; // push temporizado automatico (padrao true)
  timedPushIntervalMinutes?: number; // intervalo em minutos (padrao 10)
  backupMode?: 'ddl_and_data' | 'schema_only'; // modo de backup do banco de dados (ddl_and_data ou schema_only)
  lastSyncAt?: string;
  lastCommitHash?: string;
}

export interface IProjectRunnerParameter {
  id: string;
  label: string;
  type: 'string' | 'number' | 'boolean' | 'file';
  defaultValue?: any;
  description?: string;
}

export interface IRunnerSignatureConfig {
  enabled: boolean;
  algorithm: 'hmac-sha256' | 'ed25519' | 'sha256-rsa';
  keyId?: string;
  publicKey?: string;
  requiredForExecution: boolean;
  signature?: string;
  signedAt?: string;
  issuer?: string;
}

export interface IRunnerVaultConfig {
  provider: 'windows-credential-vault' | 'azure-key-vault' | 'hybrid' | 'none';
  azureKeyVaultUrl?: string;
  azureAuthMethod?: 'managed-identity' | 'service-principal' | 'azure-cli';
  azureTenantId?: string;
  azureClientId?: string;
  secretPrefix?: string;
  fallbackToEnvironment?: boolean;
  autoPurgeMemory?: boolean;
}

export interface IRunnerPackagingConfig {
  format: 'standalone-folder' | 'msi-installer' | 'msix-package';
  appId?: string;
  publisher?: string;
  upgradeCode?: string;
  silentInstallArgs?: string;
  createDesktopShortcut?: boolean;
  createStartMenuShortcut?: boolean;
}

export interface IProjectRunnerConfig {
  enabled: boolean;
  title?: string;
  description?: string;
  version?: string;
  outputDir?: string;
  outputName?: string;
  autoStart?: boolean;
  defaultTab?: 'progress' | 'diagnostics' | 'operations';
  allowRestart?: boolean;
  parameters?: IProjectRunnerParameter[];
  signature?: IRunnerSignatureConfig;
  vault?: IRunnerVaultConfig;
  packaging?: IRunnerPackagingConfig;
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
  git?: IProjectGitConfig;
  runner?: IProjectRunnerConfig;
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
  type: 'cycle' | 'orphan' | 'invalid_port' | 'missing_branch' | 'invalid_call' | 'call_cycle' | 'invalid_interaction' | 'invalid_skill' | 'invalid_mcp';
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
