# Documentação do Arestrix — Português

[English](../en/README.md) · [Toda a documentação](../README.md)

> **Aplica-se a:** Arestrix 2.5 (`2.5.0`) · Última revisão: 2026-10-31

## Trilha de aprendizado

1. [Primeiros passos](primeiros-passos.md) — requisitos, build, chaves de API e a primeira execução.
2. [Arquitetura](arquitetura.md) — Studio, engine, WebUI e o caminho do grafo ao C++.
3. [Grafo e vértices](grafo-e-vertices.md) — tipos de vértice, arestas, validação e ciclos de feedback.
4. [Agentes e execução](agentes-e-execucao.md) — laço de ferramentas, sandboxes, telemetria e execução em lote.
5. [Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md) - contexto persistente e integrações.
6. [Como funciona a memória](como-funciona-a-memoria.md) - ciclo completo, embeddings, estado de continuação, playbooks, workspace e instituição.
7. [Pendências](../PENDENCIAS.md) - trabalho operacional pendente e o programa separado da Fase D.
8. [Interface](interface.md) - ribbon, backstage, controles do canvas e atalhos.
9. [Trilhas e junção](trilhas-e-juncao.md) - caminhos independentes, conflito de escrita e junção avaliada pelo Assistente.

## Referência

- [`arestrix-cli`](arestrix-cli.md) — execução sem interface para CI, servidores e scripts.
- [Desenvolvimento](desenvolvimento.md) — mapa do repositório, hardening, testes e política de terceiros.
- [Gate de release e artefatos](release.md) — validação reproduzível, manifesto SHA-256 e SBOM.
- [Marca](marca.md) — uso do logotipo, paleta e tipografia.
- [Documentation in English](../en/README.md) — os mesmos guias em inglês.

## Operação

- [Roadmap e limitações conhecidas](roadmap.md) - escopo entregue, lacunas e próximos passos.
- [Plano de testes do Beta 2.3](../quality/beta2.3-test-plan.md) - escopo registrado, instalador/assinatura/CI pendentes e matriz obrigatória.
- [Registro de auditoria de testes](../quality/test-audit-log.md)
- [Revisão das referências de skills](../quality/skill-reference-review.md)
- [Modelo de segurança e governança](../security/governance.md)
- [Avisos de dependências](../security/dependency-advisories.md)
- [Política de segurança](../../SECURITY.md)
- [Contribuição](../../CONTRIBUTING.md)

## Tutoriais e produtos

- [Tutorial do agente SVG](tutorials/svg-agent.md) — passo a passo completo, do grafo à entrega.
- [Exemplos](../../examples/README.pt-BR.md) - treze projetos executáveis (o de pesquisa web usa Keenable).
- [Galeria de recursos](../product/showcase.md) — capturas do Studio, compilador, runner e governança.
- [Comparativo](../product/comparison.md) — diferenças em relação a outras abordagens de orquestração.
