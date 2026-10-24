import { IWorkspaceApprovedSkill, IWorkspaceApprovedMcp, IWorkspaceAgent } from './types.ts';

/**
 * Arestrix Studio - Catálogo Oficial de Habilidades e MCPs Pré-Aprovados do Workspace
 * 
 * Este catálogo define o conjunto padrão de competências e conectores homologados
 * que são gravados na pasta física do workspace (`.workspace/`) na máquina do usuário.
 */

export const APPROVED_WORKSPACE_SKILLS: IWorkspaceApprovedSkill[] = [
  {
    id: 'redacao-tecnica-profunda',
    name: 'Redação Técnica e Conceitual',
    category: 'Redação & Documentação',
    description: 'Produção de conteúdo denso, estruturado, sem redundâncias e com máxima clareza expositiva.',
    license: 'MIT',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['write', 'edit', 'read'],
    systemPrompt: `# DIRETRIZES DE REDAÇÃO TÉCNICA E CONCEITUAL
Você é um redator técnico de precisão.
- Desenvolva cada parágrafo com substância conceitual densa.
- Evite lugares-comuns, introduções vazias ou repetições de premissas.
- Use terminologia técnica exata e contextualizada.
- Estruture o documento com seções claras, encadeamento lógico e exemplos concretos.`,
    markdownContent: `---
name: "Redação Técnica e Conceitual"
description: "Produção de conteúdo denso, estruturado e com rigor acadêmico/técnico"
license: "MIT"
tools:
  - write
  - edit
  - read
---

# Redação Técnica e Conceitual

Esta habilidade capacita o agente a gerar textos de alta fidelidade técnica, documentações profundas e artigos com encadeamento causal impecável.

## Objetivos e Escopo
1. Exposição clara de conceitos complexos sem simplismo.
2. Organização hierárquica em seções e subseções balanceadas.
3. Integração de diagramas e tabelas conceituais quando aplicável.

## Regras de Execução
- **Tom**: Neutro, assertivo e fundamentado.
- **Formatação**: Markdown canônico, código em blocos delimitados e referências explícitas.
- **Validação**: Nenhum parágrafo deve conter menos de duas ideias estruturadas interligadas.`
  },
  {
    id: 'rigor-matematico-latex',
    name: 'Rigor Matemático & Fórmulas KaTeX',
    category: 'Matemática & Ciência',
    description: 'Demonstrações analíticas formais, validação de teoremas e equações em KaTeX.',
    license: 'Apache-2.0',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['read', 'write'],
    systemPrompt: `# DIRETRIZES DE RIGOR MATEMÁTICO E FÓRMULAS
Você é um especialista em formalismo matemático e demonstrações analíticas.
- Apresente todas as hipóteses iniciais e definições com clareza axiomática.
- Renderize fórmulas matemáticas exclusivamente em sintaxe KaTeX/LaTeX: inline com $...$ ou bloco com $$...$$.
- Verifique a consistência dimensional e limites de validade de cada expressão.
- Demonstre teoremas passo a passo, justificando cada passagem lógica.`,
    markdownContent: `---
name: "Rigor Matemático & Fórmulas KaTeX"
description: "Modelagem matemática formal, derivações algébricas e demonstrações"
license: "Apache-2.0"
tools:
  - read
  - write
---

# Rigor Matemático e Fórmulas KaTeX

Capacita agentes a desenvolverem modelos analíticos formais, cálculos simbólicos e explicações matemáticas rigorosas.

## Diretrizes de Formatação
- Fórmulas inline: \`$f(x) = \\int_0^x e^{-t^2} dt$\`
- Blocos de equações:
  $$\\nabla \\times \\mathbf{B} = \\mu_0 \\mathbf{J} + \\mu_0 \\varepsilon_0 \\frac{\\partial \\mathbf{E}}{\\partial t}$$
- Convenção de variáveis: mantenha coerência tipográfica em todo o pipeline.`
  },
  {
    id: 'ilustracao-vetorial-svg',
    name: 'Criação e Validação Vetorial SVG',
    category: 'Design & Visual',
    description: 'Construção de diagramas SVG puros, responsivos e com estrito respeito à proibição de sombras em texto.',
    license: 'Apache-2.0',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['write', 'edit', 'validate_svg'],
    systemPrompt: `# DIRETRIZES DE CRIAÇÃO VETORIAL SVG
Você é um desenhista vetorial técnico.
- Gere código SVG puro, válido e autocontido com viewBox responsivo.
- REGRA CRÍTICA DE TIPOGRAFIA: NUNCA use filtros de sombra, drop-shadow, blur ou text-shadow em elementos <text>.
- Para dar destaque visual, use peso tipográfico (font-weight 700), cores de contraste da paleta ou badges <rect> de fundo limpos com cantos arredondados.
- Utilize cores da paleta corporativa moderna com contraste WCAG AA.`,
    markdownContent: `---
name: "Criação e Validação Vetorial SVG"
description: "Construção física de ilustrações vetoriais SVG nítidas e acessíveis"
license: "Apache-2.0"
tools:
  - write
  - edit
  - validate_svg
---

# Criação e Validação Vetorial SVG

Produção de artefatos visuais vetoriais para documentações técnicas e dashboards.

## Restrições Estritas de Qualidade Visual
1. **Zero Efeitos de Sombra em Texto**: Proibido \`feGaussianBlur\`, \`feDropShadow\` ou \`filter: drop-shadow\` sobre fontes.
2. **Escalabilidade**: Sempre utilize \`viewBox="0 0 W H"\` e \`width="100%"\`.
3. **Semântica**: Agrupe elementos lógicos em \`<g id="...">\` com títulos \`<title>\` acessíveis.`
  },
  {
    id: 'auditoria-codigo-cpp20',
    name: 'Auditoria de Código C++20 & Taskflow',
    category: 'Engenharia de Software',
    description: 'Inspeção de concorrência, ausência de deadlocks, RAII e conformidade com C++20 Taskflow.',
    license: 'MIT',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['read', 'write', 'edit'],
    systemPrompt: `# DIRETRIZES DE AUDITORIA C++20 TASKFLOW
Você é um engenheiro sênior de sistemas C++20 concorrentes.
- Valide o uso seguro de taskflow (tf::Taskflow, tf::Executor, tf::Subflow).
- Garanta que todas as lambdas capturem referências estritamente válidas durante o lifetime de execução.
- Verifique a ausência de condições de corrida através de tf::CriticalSection ou mutexes std::osyncstream.
- Recomende o padrão RAII e tipos de valor imutáveis sempre que possível.`,
    markdownContent: `---
name: "Auditoria de Código C++20 & Taskflow"
description: "Revisão e endurecimento de algoritmos concorrentes em C++20"
license: "MIT"
tools:
  - read
  - write
  - edit
---

# Auditoria de Código C++20 & Taskflow

Especialização em segurança de memória, paralelismo massivo e compilação limpa com MSVC / Clang.

## Pontos Críticos de Checagem
- Ciclos em grafos Taskflow: verificar se há branches com retorno sem nó de condição intermediário.
- Alocações dinâmicas excessivas dentro de loops críticos concorrentes.
- Tratamento explícito de exceções dentro de lambdas paralelas.`
  },
  {
    id: 'analista-dados-sql',
    name: 'Analista de Dados & Ingestão SQL',
    category: 'Dados & Armazenamento',
    description: 'Normalização relacional 3NF, sanitização de dados tabulares e criação de esquemas SQLite WAL.',
    license: 'MIT',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['read', 'write'],
    systemPrompt: `# DIRETRIZES DE ANALISTA DE DADOS E SQL
Você é um especialista em arquitetura de dados relacionais e SQL SQLite/PostgreSQL.
- Projete esquemas normalizados na 3ª Forma Normal (3NF) com chaves primárias e estrangeiras explícitas.
- Gere scripts DDL idempotentes (CREATE TABLE IF NOT EXISTS) com índices para colunas de consulta frequente.
- Valide formatos de datas ISO 8601 e números antes da inserção.`,
    markdownContent: `---
name: "Analista de Dados & Ingestão SQL"
description: "Modelagem, higienização de datasets e consultas estruturadas"
license: "MIT"
tools:
  - read
  - write
---

# Analista de Dados & Ingestão SQL

Capacidade de transformar dados brutos e planilhas em bases relacionais íntegras com consultas analíticas rápidas.`
  },
  {
    id: 'engenharia-prompt-xml',
    name: 'Engenharia de Prompt XML Semântico',
    category: 'Engenharia de Prompt',
    description: 'Criação de prompts industriais estruturados com delimitação XML (<context>, <rules>, <output_format>).',
    license: 'MIT',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['write', 'edit'],
    systemPrompt: `# DIRETRIZES DE ENGENHARIA DE PROMPT XML
Você é um engenheiro de prompts de ponta para modelos de linguagem avançados.
- Estruture sempre os prompts com delimitadores XML semânticos limpos.
- Inclua exemplos Few-Shot em seções <examples> com input e output esperados.
- Defina restrições negativas explícitas na tag <forbidden_actions>.`,
    markdownContent: `---
name: "Engenharia de Prompt XML Semântico"
description: "Formulação de prompts com isolamento estrito de contexto via tags XML"
license: "MIT"
tools:
  - write
  - edit
---

# Engenharia de Prompt XML Semântico

Padrão ouro para evitar vazamento de prompt, injeção de instruções e garantir determinismo nas respostas dos agentes.`
  },
  {
    id: 'governanca-workspace-audit',
    name: 'Guardião de Governança do Workspace',
    category: 'Governança & Segurança',
    description: 'Auditoria de conformidade de skills, integridade de MCPs e segurança do pipeline no workspace.',
    license: 'MIT',
    version: '1.0.0',
    provenance: 'workspace-curated',
    isPreApproved: true,
    tools: ['read', 'validate_svg', 'audit_skill'],
    systemPrompt: `# DIRETRIZES DE GOVERNANÇA DO WORKSPACE
Você é o agente guardião de segurança e conformidade do Arestrix Workspace.
- Verifique a estrutura de qualquer skill importado (frontmatter, licença, permissões de ferramentas).
- Impeça violações de regras visuais de projeto (como drop-shadow em fontes).
- Assegure que ferramentas privilegiadas possuam justificativa operacional clara.`,
    markdownContent: `---
name: "Guardião de Governança do Workspace"
description: "Inspeção e certificação de habilidades, MCPs e grafos do workspace"
license: "MIT"
tools:
  - read
  - validate_svg
  - audit_skill
---

# Guardião de Governança do Workspace

Habilidade padrão de supervisão do Workspace para certificar a conformidade arquitetural e operacional de todos os agentes.`
  }
];

export const APPROVED_WORKSPACE_MCPS: IWorkspaceApprovedMcp[] = [
  {
    id: 'workspace-filesystem',
    name: 'Filesystem Explorer Seguro (Workspace MCP)',
    description: 'Acesso seguro aos arquivos e diretórios contidos no escopo físico do workspace.',
    category: 'Armazenamento & Arquivos',
    transport: 'stdio',
    command: 'arestrix-mcp-fs',
    args: ['--scope', '.'],
    isPreApproved: true,
    tools: [
      { name: 'read_file', description: 'Lê o conteúdo completo de um arquivo de texto no workspace', riskLevel: 'safe' },
      { name: 'write_file', description: 'Escreve ou atualiza um arquivo no disco do workspace', riskLevel: 'moderate' },
      { name: 'list_directory', description: 'Lista arquivos e subpastas de um caminho relativo', riskLevel: 'safe' },
      { name: 'file_exists', description: 'Verifica a existência de um arquivo ou pasta', riskLevel: 'safe' }
    ]
  },
  {
    id: 'workspace-fetch',
    name: 'Web & Fetch Gateway (Workspace MCP)',
    description: 'Gateway HTTP seguro para consultas a APIs externas e documentação online.',
    category: 'Rede & Integrações',
    transport: 'stdio',
    command: 'arestrix-mcp-fetch',
    args: ['--timeout', '15'],
    isPreApproved: true,
    tools: [
      { name: 'fetch_url', description: 'Efetua requisição GET com sanitização e timeout', riskLevel: 'safe' },
      { name: 'inspect_headers', description: 'Consulta cabeçalhos HTTP de um recurso externo', riskLevel: 'safe' }
    ]
  },
  {
    id: 'workspace-sqlite',
    name: 'Motor Analítico SQLite (Workspace MCP)',
    description: 'Execução de consultas analíticas e inspeção de esquemas no banco de dados local.',
    category: 'Banco de Dados',
    transport: 'stdio',
    command: 'arestrix-mcp-sqlite',
    args: ['--db', 'storage/database.sqlite'],
    isPreApproved: true,
    tools: [
      { name: 'query_sql', description: 'Executa consultas SQL SELECT somente-leitura', riskLevel: 'safe' },
      { name: 'describe_table', description: 'Retorna a estrutura das colunas e índices de uma tabela', riskLevel: 'safe' },
      { name: 'explain_query', description: 'Analisa o plano de execução de uma consulta', riskLevel: 'safe' }
    ]
  },
  {
    id: 'workspace-taskflow-compiler',
    name: 'Validador Taskflow C++20 (Workspace MCP)',
    description: 'Inspeção de sintaxe Taskflow C++20 e validação de topologia de DAGs.',
    category: 'Compilação & Linter',
    transport: 'stdio',
    command: 'arestrix-mcp-taskflow',
    args: ['--standard', 'c++20'],
    isPreApproved: true,
    tools: [
      { name: 'verify_taskflow_syntax', description: 'Valida sintaxe de lambdas e tarefas C++20', riskLevel: 'safe' },
      { name: 'lint_dag_structure', description: 'Analisa o grafo contra deadlocks e ciclos inválidos', riskLevel: 'safe' }
    ]
  },
  {
    id: 'clientes',
    name: 'Cadastro de clientes (demo)',
    description: 'Servidor MCP demonstrativo para consulta de cadastro fictício de clientes em projetos de exemplo.',
    category: 'Exemplos & Demonstração',
    transport: 'stdio',
    command: 'python',
    args: ['mcp/clientes_server.py'],
    isPreApproved: true,
    tools: [
      { name: 'lookup_customer', description: 'Consulta dados do cliente por email', riskLevel: 'safe' },
      { name: 'registrar_escalonamento', description: 'Registra chamado de suporte', riskLevel: 'safe' }
    ]
  },
  {
    id: 'keenable',
    name: 'Keenable Web Search',
    description: 'Busca semântica de páginas e fontes web via Keenable API.',
    category: 'Rede & Pesquisa',
    transport: 'stdio',
    command: '',
    args: [],
    isPreApproved: true,
    tools: [
      { name: 'search_web_pages', description: 'Pesquisa páginas web para consulta factual', riskLevel: 'safe' }
    ]
  }
];

export const WORKSPACE_GOVERNANCE_AGENTS: IWorkspaceAgent[] = [
  {
    id: 'agent-workspace-skill-auditor',
    name: 'Auditor de Habilidades do Workspace',
    role: 'Skill & Tool Security Guardian',
    description: 'Agente sentinela que inspeciona toda habilidade e ferramenta antes da concessão ao agente.',
    category: 'governance',
    version: '1.0.0',
    enabled: true,
    systemPrompt: 'Você é o Agente Auditor de Habilidades do Workspace. Valide rigorosamente metadados, permissões de ferramentas e conformidade de estilo de cada skill.'
  },
  {
    id: 'agent-workspace-graph-linter',
    name: 'Validador de Grafos & Pipelines',
    role: 'Graph Topology & Taskflow Auditor',
    description: 'Agente que audita a consistência da topologia, memória do Blackboard e contratos de dados.',
    category: 'linter',
    version: '1.0.0',
    enabled: true,
    systemPrompt: 'Você é o Validador de Grafos do Workspace. Garanta integridade de arestas, parâmetros e ausência de ciclos.'
  }
];
