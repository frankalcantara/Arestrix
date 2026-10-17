---
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
1. **Zero Efeitos de Sombra em Texto**: Proibido `feGaussianBlur`, `feDropShadow` ou `filter: drop-shadow` sobre fontes.
2. **Escalabilidade**: Sempre utilize `viewBox="0 0 W H"` e `width="100%"`.
3. **Semântica**: Agrupe elementos lógicos em `<g id="...">` com títulos `<title>` acessíveis.