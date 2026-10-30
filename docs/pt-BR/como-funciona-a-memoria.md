# Como funciona a memória do Arestrix

> **Aplica-se a:** Arestrix 2.4 — última revisão: 2026-10-30
>
> Guia de uma página e referência da memória de projeto. A memória é opcional, local e
> pertence ao projeto. Não há importação automática de formatos antigos.

## 1. Princípios

- **Desligada por padrão.** Nenhum vértice consulta ou grava conhecimento sem configuração explícita.
- **Três modos.** `off`, `consult` e `learn`. O modo é decidido por vértice.
- **Aprender implica salvar.** `learn` grava um episódio e pode propor conhecimento; `consult` só lê.
- **Nada entra sozinho.** Lições e playbooks nascem como proposta/candidato e só viram conhecimento
  ativo depois de uma aprovação.
- **Telemetria separada.** Episódios, hits e uso de API ficam em banco próprio; limpar sessão ou
  apagar telemetria nunca apaga o conhecimento.
- **Local-first.** SQLite no disco do projeto, sem dependência de nuvem.

O ciclo é:

```text
consultar → executar → observar → propor → aprovar → consolidar → consultar
```

## 2. Onde a memória fica

| Escopo | Caminho | Papel |
| :--- | :--- | :--- |
| Conhecimento do projeto | `<projeto>/.arestrix/memory/memory.db` | lições, fatos, evidências e estado de trabalho |
| Telemetria do projeto | `<projeto>/.arestrix/telemetry/telemetry.db` | episódios, hits, uso de tokens e validações |
| Workspace | `<projeto>/.workspace/memory/memory.db` | conhecimento promovido, compartilhado |
| Instituição | banco do serviço institucional | conhecimento autenticado de vários projetos |

`ARESTRIX_MEMORY_DB` troca o banco de conhecimento e `ARESTRIX_TELEMETRY_DB` troca o de
telemetria. A raiz do projeto vem de `ARESTRIX_PROJECT_ROOT`.

## 3. Configuração do vértice

| Campo | Efeito |
| :--- | :--- |
| `mode: off` | Não lê nem grava memória. |
| `mode: consult` | Injeta o brief autorizado no prompt. Não grava conhecimento. |
| `mode: learn` | Consulta e, ao terminar, grava episódio e propostas de lição. |
| `theme` | Tema principal de leitura e escrita; fallback para `domains[0]`. |
| `readThemes` | Temas extras apenas de leitura. |
| `taskType` | Identidade estável de continuidade; padrão é o id do vértice. |
| `contextBudgetTokens` | Orçamento do brief; `0` desativa a injeção. |
| `maxLessonsPerRun` | Limite de propostas por execução. |
| `maxLessonChars` | Limite de caracteres por lição/trigger. |

A configuração do projeto define o padrão herdado pelos vértices: modo, tema, retenção,
consolidação e embeddings. O vértice pode sobrescrever a memória quando `inheritMemory` é falso.

## 4. O que o brief contém

O brief é montado no início da execução, dentro do orçamento configurado:

1. **Lições ativas** do tema, ordenadas por confiança e recência.
2. **Fatos ativos** ligados ao tema e às consultas da tarefa.
3. **Evidências** das lições e fatos injetados.
4. **Estado de trabalho** apenas quando o formato legado/manual o habilita. O grafo gerado pelo
   Arestrix não usa esse bloco.

Playbooks compilados ficam como **candidatos** e não entram no brief até serem promovidos.
Conhecimento com baixa confiança, conflito aberto ou validade expirada não é injetado.

Cada item injetado recebe um `usedHitRef`; no fim da execução, o runtime registra se ele foi
realmente usado. Isso alimenta a validação e o reforço de confiança.

## 5. Como o aprendizado entra

Uma execução `learn` grava um **episódio** com entrada, resultado, validação e hashes. A partir
dele, o sistema pode criar:

- **proposta de lição** (`memory_propose_lesson`) quando o agente declara uma regra;
- **proposta automática de falha** quando o validador reprova a saída;
- **candidato de playbook** quando há sequência suficiente de passos bem-sucedidos.

Propostas ficam pendentes no painel **Memória do Projeto**. Aprovar promove o item para o
conhecimento ativo; rejeitar mantém o registro para auditoria. Não existe gravação direta de
lição por ferramenta.

## 6. Ferramentas do agente

| Ferramenta | Efeito |
| :--- | :--- |
| `memory_search` | Busca somente leitura no conhecimento do projeto e no workspace autorizado. |
| `memory_propose_lesson` | Registra uma proposta pendente de lição; não ativa conhecimento. |

`memory_record_lesson`, `memoryWriteKeys` e o servidor nativo `arestrix.memoria` foram removidos.
O conhecimento é acessado pelo runtime e pelo painel, não por um chave-valor paralelo.

## 7. Busca e embeddings

A busca combina:

- **lexical** (FTS) sempre disponível;
- **vetorial** opcional, quando um provedor de embeddings está configurado;
- **fusão RRF** para ordenar os dois resultados.

A busca **não indexa** durante a consulta. A indexação é explícita: o painel do projeto oferece
**Testar** e **Reindexar**. Perfis prontos:

| Perfil | Dimensão |
| :--- | :--- |
| EmbeddingGemma 2 | 768 |
| Qwen3-Embedding-4B | 2560 |

Sem provedor disponível, a busca continua funcionando no modo lexical. O status do índice mostra
modelo, dimensão, itens indexados, candidatos e pendentes.

## 8. Medição A/B

O painel oferece **Comparar com e sem memória (contexto)**. A ação:

1. cria uma cópia consistente do conhecimento com `VACUUM INTO`;
2. calcula o fingerprint SHA-256 da cópia;
3. mede, por vértice ativo, tokens e caracteres do brief, lições, fatos e playbooks;
4. compara com o baseline sem memória (zero).

Essa é a versão **A/B v1**: mede custo e conteúdo do contexto, não a taxa de aprovação do
validador. O protocolo completo, com tarefas fixas, repetições e telemetria por braço, é a
evolução seguinte.

## 9. Retenção, segurança e escopo

- **Escopo estrito:** o conhecimento do projeto não vaza para outro projeto; o workspace só é
  lido quando configurado e autorizado.
- **Sanitização:** segredos conhecidos são removidos antes de qualquer gravação, leitura legada
  ou exportação.
- **Retenção:** afeta telemetria (episódios, hits, uso). Conhecimento não é apagado por rotina de
  retenção.
- **Limpeza de sessão:** em memória efêmera, limpa apenas a sessão; nunca purga um banco de arquivo.
- **Auditoria:** aprovações, rejeições, promoções, conflitos e expurgos deixam rastro.

## 10. Referência rápida

```text
Projeto
  .arestrix/memory/memory.db       conhecimento
  .arestrix/telemetry/telemetry.db telemetria
  .workspace/memory/memory.db      workspace (opcional)

Vértice
  off      não usa memória
  consult  lê o brief e registra hits
  learn    lê, grava episódio e propõe lições

Ciclo
  executar -> episódio -> proposta -> aprovação -> conhecimento ativo -> brief
```

Para operar pela linha de comando, consulte [`arestrix-cli.md`](arestrix-cli.md). Para a visão de
produto e os limites atuais, consulte [`workspaces-memoria-skills-mcp.md`](workspaces-memoria-skills-mcp.md).

## 12. Instituição

O serviço `arestrix-memory` expõe o conhecimento institucional por HTTP autenticado e MCP (stdio e
`/mcp`), com tokens, papéis `viewer`/`editor`/`admin` e escopo invariante. Ele é opcional e separado
do banco privado do projeto.
