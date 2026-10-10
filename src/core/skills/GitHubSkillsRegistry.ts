import { INodeSkill, IGitHubSkillRepo, IGitHubSkillItem } from '../types.ts';

/**
 * Arestrix Studio - GitHub Skills Registry & Catalog
 * 
 * Gerenciador de Repositórios Open Source do GitHub para importação de Skills.
 * Fornece lista curada default de skills permissivas (MIT / Apache-2.0 / BSD)
 * e permite configurar repositórios personalizados.
 */
export class GitHubSkillsRegistry {
  private static instance: GitHubSkillsRegistry;

  private repositories: IGitHubSkillRepo[] = [
    {
      id: 'repo-anthropics',
      repo: 'anthropics/prompt-eng-interactive-tutorial',
      name: 'Anthropic Prompt Engineering Skills',
      description: 'Habilidades estruturadas com tags XML, personas e raciocínio multi-etapa.',
      license: 'MIT',
      branch: 'main',
      isDefault: true,
      isEnabled: true
    },
    {
      id: 'repo-taskflow-agents',
      repo: 'taskflow/agent-skills-community',
      name: 'Taskflow Community Agent Skills',
      description: 'Habilidades de alto desempenho C++20, geração vetorial SVG e rigor matemático.',
      license: 'Apache-2.0',
      branch: 'main',
      isDefault: true,
      isEnabled: true
    },
    {
      id: 'repo-openai-evals',
      repo: 'openai/evals-and-skills',
      name: 'OpenAI Autonomous Agent Skills',
      description: 'Habilidades de raciocínio em cadeia (CoT), auditoria de integridade e síntese.',
      license: 'MIT',
      branch: 'main',
      isDefault: true,
      isEnabled: true
    },
    {
      id: 'repo-autogen',
      repo: 'microsoft/autogen-skills',
      name: 'Microsoft AutoGen Core Skills',
      description: 'Habilidades de execução de ferramentas em sandbox, codemode e análise de dados.',
      license: 'MIT',
      branch: 'main',
      isDefault: true,
      isEnabled: true
    }
  ];

  private defaultSkills: IGitHubSkillItem[] = [
    {
      id: 'redacao-tecnica-profunda',
      name: 'Redação Técnica e Conceitual',
      description: 'Produção de conteúdo denso, estruturado, sem redundâncias e com clareza expositiva.',
      repoId: 'repo-anthropics',
      repoName: 'Anthropic Prompt Engineering Skills',
      category: 'Redação & Conteúdo',
      license: 'MIT',
      githubUrl: 'https://github.com/anthropics/prompt-eng-interactive-tutorial',
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
      description: 'Demonstrações formais, validação de teoremas e formatação precisa de fórmulas matemáticas.',
      repoId: 'repo-taskflow-agents',
      repoName: 'Taskflow Community Agent Skills',
      category: 'Matemática & Ciência',
      license: 'Apache-2.0',
      githubUrl: 'https://github.com/taskflow/agent-skills-community',
      tools: ['read', 'write'],
      systemPrompt: `# DIRETRIZES DE RIGOR MATEMÁTICO E FÓRMULAS
Você é um agente especializado em formalismo matemático e demonstrações analíticas.
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
      description: 'Geração de diagramas SVG puros, geométricos, acessíveis e com estrito respeito à proibição de sombras em texto.',
      repoId: 'repo-taskflow-agents',
      repoName: 'Taskflow Community Agent Skills',
      category: 'Design & Visual',
      license: 'Apache-2.0',
      githubUrl: 'https://github.com/taskflow/agent-skills-community',
      tools: ['write', 'edit', 'validate_svg'],
      systemPrompt: `# DIRETRIZES DE CRIAÇÃO VETORIAL SVG
Você é um desenhista vetorial técnico.
- Gere código SVG puro, válido e autocontido com viewBox responsivo.
- REGRA CRÍTICA DE TIPOGRAFIA: NUNCA use filtros de sombra, drop-shadow, blur ou text-shadow em elementos <text>.
- Para dar destaque visual, use peso tipográfico (font-weight 700), cores de contraste ou badges <rect> de fundo limpos com cantos arredondados.
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
      description: 'Inspeção de concorrência, ausência de deadlocks, RAII e conformidade com C++20 Taskflow.',
      repoId: 'repo-autogen',
      repoName: 'Microsoft AutoGen Core Skills',
      category: 'Engenharia de Software',
      license: 'MIT',
      githubUrl: 'https://github.com/microsoft/autogen-skills',
      tools: ['read', 'write', 'edit', 'bash'],
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
  - bash
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
      description: 'Normalização relacional, sanitização de dados tabulares e criação de esquemas SQLite WAL.',
      repoId: 'repo-openai-evals',
      repoName: 'OpenAI Autonomous Agent Skills',
      category: 'Dados & Armazenamento',
      license: 'MIT',
      githubUrl: 'https://github.com/openai/evals-and-skills',
      tools: ['read', 'write', 'bash'],
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
  - bash
---

# Analista de Dados & Ingestão SQL

Capacidade de transformar dados brutos e planilhas em bases relacionais íntegras com consultas analíticas rápidas.`
    },
    {
      id: 'engenharia-prompt-xml',
      name: 'Engenharia de Prompt XML Semântico',
      description: 'Criação de prompts industriais estruturados com delimitação XML (<context>, <rules>, <output_format>).',
      repoId: 'repo-anthropics',
      repoName: 'Anthropic Prompt Engineering Skills',
      category: 'Engenharia de Prompt',
      license: 'MIT',
      githubUrl: 'https://github.com/anthropics/prompt-eng-interactive-tutorial',
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
    }
  ];

  private constructor() {
    this.loadPersistedConfig();
  }

  public static getInstance(): GitHubSkillsRegistry {
    if (!GitHubSkillsRegistry.instance) {
      GitHubSkillsRegistry.instance = new GitHubSkillsRegistry();
    }
    return GitHubSkillsRegistry.instance;
  }

  private loadPersistedConfig(): void {
    if (typeof localStorage !== 'undefined') {
      try {
        const saved = localStorage.getItem('arestrix_github_skills_repos');
        if (saved) {
          const parsed = JSON.parse(saved);
          if (Array.isArray(parsed) && parsed.length > 0) {
            this.repositories = parsed;
          }
        }
      } catch {}
    }
  }

  private savePersistedConfig(): void {
    if (typeof localStorage !== 'undefined') {
      try {
        localStorage.setItem('arestrix_github_skills_repos', JSON.stringify(this.repositories));
      } catch {}
    }
  }

  public getRepositories(): IGitHubSkillRepo[] {
    return [...this.repositories];
  }

  public addRepository(repoPath: string, name?: string, description?: string, license: string = 'MIT'): boolean {
    const cleanRepo = repoPath.trim().replace(/^https?:\/\/github\.com\//i, '').replace(/\.git$/i, '');
    if (!cleanRepo.includes('/')) return false;

    const id = `repo-${cleanRepo.replace(/[^a-zA-Z0-9_-]/g, '-')}`;
    if (this.repositories.some(r => r.id === id || r.repo.toLowerCase() === cleanRepo.toLowerCase())) {
      return false;
    }

    const newRepo: IGitHubSkillRepo = {
      id,
      repo: cleanRepo,
      name: name?.trim() || cleanRepo,
      description: description?.trim() || `Repositório open source ${cleanRepo}`,
      license: license.trim() || 'MIT',
      branch: 'main',
      isDefault: false,
      isEnabled: true
    };

    this.repositories.push(newRepo);
    this.savePersistedConfig();
    return true;
  }

  public toggleRepository(repoId: string, enabled: boolean): void {
    const target = this.repositories.find(r => r.id === repoId);
    if (target) {
      target.isEnabled = enabled;
      this.savePersistedConfig();
    }
  }

  public removeRepository(repoId: string): boolean {
    const idx = this.repositories.findIndex(r => r.id === repoId);
    if (idx !== -1 && !this.repositories[idx].isDefault) {
      this.repositories.splice(idx, 1);
      this.savePersistedConfig();
      return true;
    }
    return false;
  }

  public getDefaultSkills(): IGitHubSkillItem[] {
    const enabledRepos = new Set(this.repositories.filter(r => r.isEnabled).map(r => r.id));
    return this.defaultSkills.filter(s => enabledRepos.has(s.repoId));
  }

  public searchSkills(query: string = '', category?: string): IGitHubSkillItem[] {
    const q = query.toLowerCase().trim();
    return this.getDefaultSkills().filter(skill => {
      const matchQuery = !q || 
        skill.name.toLowerCase().includes(q) || 
        skill.description.toLowerCase().includes(q) ||
        skill.tools.some(t => t.toLowerCase().includes(q));
      
      const matchCategory = !category || category === 'todas' || skill.category.toLowerCase() === category.toLowerCase();
      return matchQuery && matchCategory;
    });
  }

  public convertToNodeSkill(item: IGitHubSkillItem): INodeSkill {
    return {
      id: item.id,
      name: item.name,
      description: item.description,
      markdownContent: item.markdownContent,
      systemPrompt: item.systemPrompt,
      tools: [...item.tools],
      license: item.license,
      sourceUrl: item.githubUrl || `https://github.com/${item.repoId}`,
      updatedAt: new Date().toISOString()
    };
  }
}
