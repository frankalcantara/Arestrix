import { IAgentTemplate } from './AgentTemplateTypes.ts';

export const BUILTIN_TEMPLATES: IAgentTemplate[] = [
  {
    id: 'llm_decision_agent',
    name: 'Agente LLM / Prompt Decisor',
    category: 'IA & Inferência',
    icon: '🤖',
    taskType: 'condition',
    description: 'Avalia respostas de inferência semântica e bifurca o fluxo: ramo 0 avança para o próximo estágio caso atinja o limiar de confiança; ramo 1 ativa um loop de retroalimentação (retry) com re-prompt.',
    inputs: [
      { name: 'prompt_in', type: 'data', dataType: 'std::string' },
      { name: 'context_flow', type: 'flow' }
    ],
    outputs: [
      { name: 'aprovado (ramo 0)', type: 'flow', conditionBranchIndex: 0 },
      { name: 're-tentativa (ramo 1)', type: 'flow', conditionBranchIndex: 1 }
    ],
    parameters: {
      modelName: {
        label: 'Modelo LLM',
        type: 'select',
        defaultValue: 'gemini-2.5-flash',
        description: 'Modelo de linguagem para inferência e julgamento',
        options: ['gemini-2.5-flash', 'gemini-2.5-pro', 'claude-3-5-sonnet', 'gpt-4o-mini', 'local-llama-3-8b']
      },
      temperature: {
        label: 'Temperatura',
        type: 'number',
        defaultValue: 0.7,
        description: 'Grau de determinismo/estocasticidade (0.0 a 1.0)'
      },
      confidenceThreshold: {
        label: 'Limiar de Confiança',
        type: 'number',
        defaultValue: 0.85,
        description: 'Pontuação mínima para aprovação no ramo 0'
      },
      maxRetries: {
        label: 'Tentativas Máximas',
        type: 'number',
        defaultValue: 3,
        description: 'Limite máximo de retentativas antes de forçar aprovação'
      }
    },
    requiredHeaders: ['<random>', '<chrono>', '<string>', '<syncstream>'],
    generateLambdaBody: (params, label) => {
      const model = params.modelName || 'gemini-2.5-flash';
      const temp = params.temperature ?? 0.7;
      const threshold = params.confidenceThreshold ?? 0.85;
      const maxRetries = params.maxRetries ?? 3;

      return `// Agente LLM: Avaliacao de Prompt e Decisao Estocastica C++20
// Modelo: ${model} | Temperatura: ${temp}
std::mt19937_64 rng(std::random_device{}());
std::uniform_real_distribution<double> dist(0.0, 1.0);
double confidence = dist(rng);
ctx.last_confidence_score = confidence;

std::osyncstream(std::cout) << "[${label}] Inferindo com ${model} (temp: ${temp})... Score: " 
                            << confidence << " (Limiar: ${threshold})\\n";

if (confidence >= ${threshold} || ctx.retry_counter >= ${maxRetries}) {
  std::osyncstream(std::cout) << "  -> [${label}] Resposta APROVADA! Avancando para o proximo estagio (Ramo 0).\\n";
  ctx.retry_counter = 0;
  return 0; // Ramo 0: Sucesso / Avanco
} else {
  ctx.retry_counter++;
  std::osyncstream(std::cout) << "  -> [${label}] Confianca insuficiente (" << ctx.retry_counter 
                              << "/${maxRetries}). Realimentando loop para refinamento (Ramo 1).\\n";
  return 1; // Ramo 1: Feedback Loop / Refinamento
}`;
    }
  },
  {
    id: 'concurrent_queue_worker',
    name: 'Agente de Fila Thread-Safe / Worker',
    category: 'Concorrência & Filas',
    icon: '⚡',
    taskType: 'static',
    description: 'Worker concorrente produtor/consumidor que processa lotes (batches) de mensagens extraídas de uma fila thread-safe protegida por mutex, garantindo isolamento sem corrida de dados.',
    inputs: [
      { name: 'stream_in', type: 'flow' }
    ],
    outputs: [
      { name: 'batch_out', type: 'flow' }
    ],
    parameters: {
      workerTag: {
        label: 'Identificador do Worker',
        type: 'string',
        defaultValue: 'Worker-Alpha',
        description: 'Tag identificadora da thread de processamento'
      },
      batchSize: {
        label: 'Tamanho do Lote (Batch)',
        type: 'number',
        defaultValue: 32,
        description: 'Quantidade de elementos consumidos por ciclo'
      },
      queueCapacity: {
        label: 'Capacidade do Buffer',
        type: 'number',
        defaultValue: 1024,
        description: 'Capacidade máxima de mensagens no buffer circular'
      }
    },
    requiredHeaders: ['<queue>', '<mutex>', '<string>', '<syncstream>'],
    generateLambdaBody: (params, label) => {
      const tag = params.workerTag || 'Worker-Alpha';
      const batch = params.batchSize ?? 32;

      return `// Agente de Fila Concorrente: Processamento seguro de lote
// Tag: ${tag} | Tamanho do Lote: ${batch}
size_t processed_count = 0;
{
  std::lock_guard<std::mutex> lock(ctx.queue_mutex);
  // Alimenta e drena mensagens concorrentes com protecao de mutex
  for (int i = 0; i < ${batch}; ++i) {
    ctx.task_queue.push("Msg_" + std::to_string(ctx.tick_count + i));
  }
  while (!ctx.task_queue.empty() && processed_count < ${batch}) {
    auto item = ctx.task_queue.front();
    ctx.task_queue.pop();
    processed_count++;
  }
}
std::osyncstream(std::cout) << "[${label}] Worker ${tag} processou com sucesso " 
                            << processed_count << " itens da fila thread-safe em paralelo.\\n";`;
    }
  },
  {
    id: 'sqlite_persistence_agent',
    name: 'Agente SQLite / Persistência Concorrente',
    category: 'Armazenamento & Dados',
    icon: '🗄️',
    taskType: 'critical',
    description: 'Executa operações transacionais ACID e persistência de histórico de agentes dentro de uma seção crítica exclusiva do Taskflow (tf::CriticalSection), prevenindo deadlocks e corrupção.',
    inputs: [
      { name: 'data_in', type: 'data', dataType: 'AgentState' }
    ],
    outputs: [
      { name: 'ack_out', type: 'flow' }
    ],
    parameters: {
      tableName: {
        label: 'Tabela de Destino',
        type: 'string',
        defaultValue: 'agent_execution_logs',
        description: 'Nome da tabela relacional para registro dos logs'
      },
      journalMode: {
        label: 'Modo Journal SQLite',
        type: 'select',
        defaultValue: 'WAL',
        description: 'Modo de write-ahead logging para concorrência de leitura/escrita',
        options: ['WAL', 'DELETE', 'MEMORY', 'OFF']
      },
      syncMode: {
        label: 'Nível Sincronização',
        type: 'select',
        defaultValue: 'NORMAL',
        description: 'Nível de integridade e flush no disco do SQLite',
        options: ['NORMAL', 'FULL', 'EXTRA', 'OFF']
      }
    },
    requiredHeaders: ['<unordered_map>', '<mutex>', '<string>', '<syncstream>'],
    generateLambdaBody: (params, label) => {
      const table = params.tableName || 'agent_execution_logs';
      const journal = params.journalMode || 'WAL';

      return `// Agente SQLite / Persistencia Transacional (CriticalSection)
// Tabela: ${table} | Journal: ${journal}
std::lock_guard<std::mutex> lock(ctx.db_mutex);
std::string log_key = "log_step_" + std::to_string(ctx.tick_count++);
std::string log_entry = "Transacao ACID: Gravado registro na tabela [${table}] com Journal ${journal}";
ctx.persistent_store[log_key] = log_entry;

std::osyncstream(std::cout) << "[${label}] Transacao persistida com seguranca em SQLite: " 
                            << log_key << " (" << ctx.persistent_store.size() << " registros totais)\\n";`;
    }
  },
  {
    id: 'vision_pipeline_subflow',
    name: 'Agente de Visão / Pipeline Paralelo',
    category: 'Visão & Processamento',
    icon: '🖼️',
    taskType: 'subflow',
    description: 'Decompõe dinamicamente um frame ou tensor em quadrantes/tiles paralelos em tempo de execução via tf::Subflow, aplicando filtros convolucionais e mesclando os mapas de features.',
    inputs: [
      { name: 'frame_in', type: 'data', dataType: 'FrameBuffer' }
    ],
    outputs: [
      { name: 'features_out', type: 'data', dataType: 'FeatureMap' }
    ],
    parameters: {
      resolution: {
        label: 'Resolução da Imagem',
        type: 'select',
        defaultValue: '1920x1080',
        description: 'Resolução dimensional do quadro para fatiamento',
        options: ['1280x720', '1920x1080', '3840x2160']
      },
      tileCount: {
        label: 'Quantidade de Quadrantes',
        type: 'number',
        defaultValue: 4,
        description: 'Número de sub-tarefas simultâneas geradas no subflow'
      },
      filterAlgorithm: {
        label: 'Algoritmo de Filtragem',
        type: 'select',
        defaultValue: 'SobelGradients',
        description: 'Operador convolucional para extração de bordas/características',
        options: ['SobelGradients', 'GaussianCanny', 'BilateralFeature']
      }
    },
    requiredHeaders: ['<string>', '<syncstream>'],
    generateLambdaBody: (params, label) => {
      const res = params.resolution || '1920x1080';
      const filter = params.filterAlgorithm || 'SobelGradients';
      const tiles = params.tileCount ?? 4;

      return `// Agente de Visao: Decomposicao dinamica de quadro em Subflow
// Resolucao: ${res} | Algoritmo: ${filter} | Quadrantes: ${tiles}
std::osyncstream(std::cout) << "[${label}] Decompondo frame ${res} em ${tiles} quadrantes paralelos...\\n";

auto t1 = sf.emplace([]() {
  std::osyncstream(std::cout) << "  [Tile 0: Top-Left] Filtrando com ${filter}...\\n";
}).name("Tile_TopLeft");

auto t2 = sf.emplace([]() {
  std::osyncstream(std::cout) << "  [Tile 1: Top-Right] Filtrando com ${filter}...\\n";
}).name("Tile_TopRight");

auto t3 = sf.emplace([]() {
  std::osyncstream(std::cout) << "  [Tile 2: Bottom-Left] Filtrando com ${filter}...\\n";
}).name("Tile_BottomLeft");

auto t4 = sf.emplace([]() {
  std::osyncstream(std::cout) << "  [Tile 3: Bottom-Right] Filtrando com ${filter}...\\n";
}).name("Tile_BottomRight");

auto merge = sf.emplace([]() {
  std::osyncstream(std::cout) << "  [Consolidacao] Mesclando matriz de features dos quadrantes.\\n";
}).name("Tile_Merge");

t1.precede(merge);
t2.precede(merge);
t3.precede(merge);
t4.precede(merge);`;
    }
  },
  {
    id: 'consensus_module_agent',
    name: 'Agente de Síntese / Módulo de Consenso',
    category: 'Síntese & Orquestração',
    icon: '⚖️',
    taskType: 'module',
    description: 'Módulo reutilizável encapsulado (tf.composed_of) que executa votação de consenso entre relatórios emitidos por múltiplos agentes e formula o veredito final consolidado.',
    inputs: [
      { name: 'evaluations_in', type: 'flow' }
    ],
    outputs: [
      { name: 'verdict_out', type: 'flow' }
    ],
    parameters: {
      quorumMin: {
        label: 'Quórum Mínimo',
        type: 'number',
        defaultValue: 3,
        description: 'Número mínimo de pareceres de agentes necessários para sintetizar veredito'
      },
      votingStrategy: {
        label: 'Estratégia de Votação',
        type: 'select',
        defaultValue: 'majority',
        description: 'Mecanismo de consenso para tomada de decisão em grupo',
        options: ['majority', 'unanimous', 'weighted_bayesian']
      },
      auditLog: {
        label: 'Gerar Trilha de Auditoria',
        type: 'boolean',
        defaultValue: true,
        description: 'Se verdadeiro, grava logs criptografados do consenso'
      }
    },
    requiredHeaders: ['<string>', '<syncstream>'],
    generateLambdaBody: (params, label) => {
      const quorum = params.quorumMin ?? 3;
      const strategy = params.votingStrategy || 'majority';

      return `// Agente de Modulo: Votacao de Consenso e Sintese Final
// Quorum Minimo: ${quorum} | Estrategia: ${strategy}
std::osyncstream(std::cout) << "[${label}] Coletando votos dos agentes especializados (Estrategia: ${strategy})...\\n";
std::osyncstream(std::cout) << "[${label}] Quorum de ${quorum} agentes atingido. Veredito final consolidado com sucesso!\\n";`;
    }
    },
  {
    id: 'cuda_kernel_agent',
    name: 'Agente de Aceleracao GPU (CUDA)',
    category: 'Visão & Processamento',
    icon: '🚀',
    taskType: 'cuda',
    description: 'Envia dados pesados (tensores/matrizes) para execucao massivamente paralela via tf::cudaFlow. Requer suporte a C++20 com CUDA.',
    inputs: [
      { name: 'tensor_in', type: 'data', dataType: 'std::vector<float>' },
      { name: 'trigger', type: 'flow' }
    ],
    outputs: [
      { name: 'tensor_out', type: 'data', dataType: 'std::vector<float>' }
    ],
    parameters: {
      gridSize: {
        label: 'Tamanho do Grid',
        type: 'number',
        defaultValue: 256,
        description: 'Tamanho do grid de blocos CUDA'
      },
      blockSize: {
        label: 'Tamanho do Bloco',
        type: 'number',
        defaultValue: 1024,
        description: 'Tamanho do bloco de threads'
      }
    },
    requiredHeaders: ['<taskflow/cuda/cudaflow.hpp>'],
    generateLambdaBody: (params, label) => {
      const grid = params.gridSize || 256;
      const block = params.blockSize || 1024;
      return `// tf::cudaFlow kernel dispatcher: ${label}
// Configuracao: Grid=${grid}, Block=${block}

cf.kernel(${grid}, ${block}, 0, [] __device__ (float* in, float* out) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    // Logica do Kernel aqui (Operacao por elemento)
    out[idx] = in[idx] * 2.0f; 
}, /* ptr_in */, /* ptr_out */);

std::osyncstream(std::cout) << "[CUDA] Kernel disparado (" << ${grid} << "x" << ${block} << " threads)\n";`;
    }
  }
];
