# Instrumentação Tracy e laço de auto-otimização

> **Status:** F1 (build opcional) implementado e validado em 2026-11-11; F2–F5 pendentes.
> **Escopo:** código C++20 gerado pelo `TaskflowGenerator` e o próprio Arestrix
> (`CppCore`, `arestrix`, `arestrix-cli`, `arestrix-gateway` e `arestrix-memory`).
> **Referências:** `docs/en/pending.md` §4.5–4.6 e `docs/PENDENCIAS.md` §4.5–4.6.

## 1. Objetivo

Fechar o ciclo **observar → diagnosticar → otimizar → recompilar → re-medir** com
evidência de performance, usando o Tracy já fixado em `third_party/tracy`
(v0.14.1-303) e o modelo padrão configurado no Studio como assistente de
refatoração. O ciclo deve valer para os dois alvos:

1. o código C++20 que o Arestrix gera e compila;
2. o código-fonte do próprio Arestrix.

## 2. Invariantes

- **Desligado por padrão.** O build de release não inclui o cliente Tracy nem
  custo de instrumentação. `ARESTRIX_ENABLE_TRACY=OFF` é o padrão.
- **Sem mudança semântica.** Zonas, plots e frames não alteram artefatos,
  validação, ordem de execução, orçamento de memória ou resultados de teste.
- **Sem segredos.** Nomes de zonas e plots não incluem chaves, prompts, caminhos
  pessoais ou conteúdo de memória; ids de vértice e contadores bastam.
- **Evidência fora do repositório.** Traces ficam em `release/profiling/<runId>/`
  e não são versionados.
- **Iteração limitada.** O laço automático tem teto de iterações, diff revisável e
  rollback se qualquer gate funcional regredir.

## 3. Fases

### F1 — Build opcional do cliente

- Opção CMake `ARESTRIX_ENABLE_TRACY` (padrão `OFF`).
- Quando ligada, compilar o cliente de `third_party/tracy` como biblioteca
  estática, definir `TRACY_ENABLE`, `TRACY_ON_DEMAND` e expor
  `ARESTRIX_TRACY=1` ao código.
- Manter o pacote portátil e o instalador sem a DLL/servidor do Tracy; a captura
  é uma ferramenta de desenvolvimento.
- Validar: build padrão idêntico; build com Tracy compila e inicia; nenhum
  símbolo/arquivo novo no pacote de release.

**Implementado:** `arestrix_tracy_client` é uma biblioteca estática ligada
publicamente a `arestrix_core`; `CppCore/src/rt/Profiling.hpp` concentra os
macros (`ARX_ZONE_SCOPED`, `ARX_ZONE_NAMED`, `ARX_FRAME_MARK`, `ARX_PLOT`) e
`runAgent` já abre a zona `agent.run`. O `dev.py` ganhou `--tracy`.

**Validação (2026-11-11):** build OFF com `4223/4223` verificações e build ON com
`4223/4223`; a cache final foi deixada em `ARESTRIX_ENABLE_TRACY=OFF` para o
pacote de release.

### F2 — Código gerado

- O `TaskflowGenerator` emite, atrás de `#ifdef ARESTRIX_TRACY`:
  - `ZoneScopedN("vertex:<id>")` em cada vértice;
  - `FrameMark` ao fim de cada execução do pipeline;
  - `TracyPlot("memory:<id>", bytes)` e contadores de alocações por vértice;
  - zonas em torno de esperas do executor e de subflows.
- A opção fica em `compilerConfig.profiling` (`off` padrão; `tracy` opt-in) e não
  muda o código gerado por padrão.
- Validar: o mesmo pipeline gera byte a byte o mesmo código com `off`; com
  `tracy`, compila e produz trace com uma zona por vértice.

### F3 — Núcleo do Arestrix

Instrumentar, sempre atrás do define:

- execução de agente (`rt::Agent`) e chamadas de modelo;
- motor de memória (`MemoryStore`: busca lexical/híbrida, consolidação,
  travessia em feixe, ingestão, snapshots);
- AI Gateway (reserva/reconciliação de budget, roteamento, proxy);
- cliente/servidor MCP e ponte IPC/WebUI;
- operações pesadas do Studio expostas ao nativo.

Plots mínimos: profundidade de fila, latência por operação, bytes de memória e
contenção de threads.

### F4 — Captura e análise headless

- `tracy-capture -o <trace> -s <segundos>` em execuções de benchmark e nas
  baterias E2E.
- `tracy-csvexport` para exportar zonas e estatísticas.
- `scripts/profile_report.py` produz um resumo determinístico: top zonas por
  tempo total e p95, alocações, locks, tarefas Taskflow e regressões contra a
  linha de base anterior.
- O relatório é JSON + Markdown, com o commit, o build e o comando usados.

### F5 — Laço com o modelo padrão do Studio

1. Selecionar o hotspot e o trecho de código correspondente.
2. Enviar o relatório estruturado e o trecho ao modelo padrão do Studio.
3. Receber um patch mínimo e explicável.
4. Recompilar, rodar testes funcionais e re-medir.
5. Aceitar somente se o ganho for acima do ruído e nenhum gate regredir; caso
   contrário, reverter e registrar o resultado.

O laço começa manual/assistido e só vira automático depois de F1–F4 estáveis.

## 4. Critérios de aceite

- `ARESTRIX_ENABLE_TRACY=OFF` mantém o comportamento e o pacote atuais.
- Build com Tracy captura um pipeline real e mostra uma zona por vértice.
- O relatório de perfil é reproduzível com o mesmo commit e comando.
- Um ciclo de otimização aplicado reduz o hotspot medido sem regressão funcional.
- Nenhum trace, segredo ou caminho pessoal entra no Git ou no SBOM.

## 5. Riscos e mitigação

| Risco | Mitigação |
| :--- | :--- |
| Overhead alto no build instrumentado | `TRACY_ON_DEMAND`, captura curta e build separado do release |
| Aumento do binário/pacote | Cliente só em build de desenvolvimento; validar o manifesto |
| Trace com dados sensíveis | Nomes por id; nunca incluir prompts, chaves ou conteúdo de memória |
| Laço automático introduzir regressão | Diff revisável, teto de iterações, testes e rollback |
| Ruído de medição | Repetições, p50/p95, mesma máquina e comparação com baseline |

## 6. Ordem de execução

1. F1 (build opcional) e um smoke de captura.
2. F2 no código gerado, com o mesmo pipeline em `off` e `tracy`.
3. F4 (relatório headless) antes de instrumentar todo o núcleo.
4. F3 por subsistema, começando por memória e agente.
5. F5 assistido, com o modelo padrão do Studio.
