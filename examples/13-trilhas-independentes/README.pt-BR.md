# Exemplo 13 - Trilhas independentes

Duas cadeias que não compartilham nenhum vértice. O Studio marca cada uma com selo **INÍCIO** e
**FIM**, conta duas trilhas e avisa no Console que elas rodam em paralelo.

## Fluxo

```text
Levantar fontes → Resumir fontes → Entregar resumo das fontes
Levantar anexos → Resumir anexos → Entregar resumo dos anexos
```

1. Cada trilha escreve seu próprio arquivo em `workspace/` e entrega em `exports/`.
2. Como não há aresta entre as trilhas, o gerador liga as duas entradas ao `Pipeline_Bootstrap`:
   elas começam juntas, rodam em paralelo e o pipeline termina quando a última concluir.
3. O Console imprime `[RUN] 2 trilhas independentes: Levantar fontes, Levantar anexos…` antes de
   compilar, e a linha `[TRACE]` do fim mostra `concurrency=2`.

## O que observar no Studio

- **Selos**: `INÍCIO` nos dois primeiros vértices e `FIM` nas duas entregas.
- **Barra de status**: `6 vértices, 4 conexões · 2 trilhas`.
- **Botão Trilhas**: colore cada trilha e tinge o selo `INÍCIO` correspondente.
- **Menu de um vértice FIM → Sugerir junção (análise local)**: as duas entregas estão na mesma pasta
  e no mesmo formato, então o Studio propõe uma junção e explica o critério. Um Ctrl+Z desfaz.
- **Unir trilhas com o Assistente…**: a mesma decisão, feita semanticamente por um modelo (precisa
  de IA configurada).

## Conflito de escrita

As duas entregas gravam arquivos diferentes, então não há aviso. Se você apontar as duas para o
mesmo `destinationPath`, **Validar DAG** passa a mostrar um aviso `resource_conflict` (ele nunca
bloqueia a execução): as trilhas podem gravar ao mesmo tempo e a última gravação vence.

## Executar

Não precisa de modelo: os vértices são tarefas C++ estáticas.

```bash
python scripts/copy_example.py 13-trilhas-independentes C:/meus-projetos/trilhas
```

No Studio: **Arquivo → Abrir** a pasta copiada e **F5**. Sem o Studio:
`arestrix-cli run pipelines/main.json --root <a pasta copiada>`.

Veja também [`docs/pt-BR/trilhas-e-juncao.md`](../../docs/pt-BR/trilhas-e-juncao.md).
