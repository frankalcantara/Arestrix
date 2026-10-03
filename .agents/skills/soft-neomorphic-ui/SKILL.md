---
name: soft-neomorphic-ui
description: >-
  Design system e guia de implementação para interfaces no estilo Soft Neomorphic / Modern Claymorphic
  (padrão HR/Analytics Dashboard). Use quando precisar estilizar ou construir dashboards, controles em pílula,
  cartões brancos elevados, mini-gauges, gráficos suaves e navegação flutuante com alto contraste e zero sombras em textos.
---

# Soft Neomorphic & Modern Claymorphic UI Design System

Este guia documenta o padrão visual de alta sofisticação baseado em **superfícies limpas, elevações suaves (soft neomorphism), cantos arredondados generosos, controles em pílula e acentos cromáticos vibrantes**, com estrita observância à proibição de sombras ou difusões em fontes tipográficas.

---

## 1. Princípios Fundamentais & Diretrizes Visuais

1. **Superfícies Flutuantes e Elevação**:
   - Cartões brancos puros (`#ffffff`) sobre fundos com gradientes translúcidos sutis.
   - Profundidade obtida exclusivamente via contêineres com `box-shadow` suave e multicamada:
     ```css
     box-shadow: 0 14px 34px -8px rgba(30, 41, 59, 0.06), 0 4px 12px -2px rgba(30, 41, 59, 0.02);
     ```
2. **Tipografia Nítida (Regra Estrita de Ouro)**:
   - **NUNCA** utilizar `text-shadow`, `filter: drop-shadow` ou filtros de desfoque (`feGaussianBlur`) em textos, rótulos, legendas ou números.
   - O destaque deve ser conquistado por:
     - **Cor de contraste direto**: `#1e2538` para títulos/números em fundo claro; `#ffffff` sobre botões coloridos.
     - **Peso tipográfico**: `font-weight: 700` ou `800` para títulos e métricas; `500` para legendas.
     - **Contraste de opacidade**: Elementos secundários com `color: #64748b` ou `opacity: 0.6`.
     - **Pílulas e Badges de fundo limpo**: Fundos sólidos ou translúcidos com cantos arredondados por trás do texto.

---

## 2. Paleta Cromática e Tokens de Design

| Token | Hex / Gradiente | Uso Principal |
| :--- | :--- | :--- |
| **Background Base** | `linear-gradient(135deg, #f5f7fc 0%, #fdf3f4 50%, #fcf7f2 100%)` | Canvas de fundo da aplicação |
| **Card Surface** | `#ffffff` | Cartões, sidebar e tooltips elevados |
| **Primary CTA (Orange)** | `linear-gradient(135deg, #ff7828 0%, #ff9548 100%)` | Botão primário (`+ Create Notice`, `▶ Executar`) |
| **Active Nav (Blue)** | `linear-gradient(135deg, #3b82f6 0%, #6366f1 100%)` | Item selecionado da sidebar |
| **Cyan Accent** | `linear-gradient(135deg, #06b6d4 0%, #38bdf8 100%)` | Mini gauge de eventos / barras de progresso |
| **Amber Accent** | `linear-gradient(135deg, #f59e0b 0%, #fbbf24 100%)` | Feriados / avisos |
| **Green Accent** | `linear-gradient(135deg, #10b981 0%, #34d399 100%)` | Folha de pagamento / sucesso |
| **Purple Accent** | `linear-gradient(135deg, #8b5cf6 0%, #a855f7 100%)` | Relatórios / módulos |
| **Slate Secondary** | `#5b7290` | Linhas de tendência secundárias e barras |

---

## 3. Guia de Implementação por Elemento de Interface

### 3.1 Sidebar Vertical Flutuante com Docking Curvo
A barra lateral é um contêiner branco vertical com cantos direitos arredondados (`border-radius: 0 32px 32px 0`):

```html
<aside class="neo-sidebar">
  <div class="neo-sidebar-logo">
    <svg ...></svg>
  </div>
  <nav class="neo-sidebar-nav">
    <button class="neo-nav-item active" title="Dashboard">
      <svg class="icon">...</svg>
    </button>
    <button class="neo-nav-item" title="Agentes">
      <svg class="icon">...</svg>
    </button>
  </nav>
</aside>
```
```css
.neo-sidebar {
  width: 84px;
  background-color: #ffffff;
  border-top-right-radius: 32px;
  border-bottom-right-radius: 32px;
  box-shadow: 4px 0 24px rgba(30, 41, 59, 0.04);
}
.neo-nav-item.active {
  background-color: #ffffff;
  color: #3b82f6;
  box-shadow: 0 10px 25px rgba(59, 130, 246, 0.16);
}
```

---

### 3.2 Header, Controles em Pílula (Pill Selects & Search) e Ações
Controles de busca, seleção de data/ano e botões de notificação adotam formato de pílula (`border-radius: 9999px`):

```html
<header class="neo-header">
  <div class="neo-header-titles">
    <h1>Hello Admin!</h1>
    <p>Measure How Fast You're Growing Monthly Recurring performance management.</p>
  </div>
  <div class="neo-header-actions">
    <!-- Seletor em Pílula -->
    <button class="neo-pill-btn">
      <span>Year</span>
      <span class="chevron">▾</span>
    </button>

    <!-- Campo de Busca em Pílula -->
    <div class="neo-pill-search">
      <span class="search-icon">🔍</span>
      <input type="text" placeholder="Search..." />
    </div>

    <!-- Botões Circulares de Notificação com Dot -->
    <button class="neo-icon-btn">
      🔔
      <span class="neo-badge-dot"></span>
    </button>

    <!-- Botão Primário Laranja Vibrante -->
    <button class="neo-btn-primary">
      <span>+ Create Notice</span>
    </button>
  </div>
</header>
```

---

### 3.3 KPI Summary Cards com Mini Gauge Circular
Cada indicador é encapsulado em um cartão branco com um anel de progresso cônico (`conic-gradient`) e seta direcional:

```html
<div class="neo-kpi-card">
  <!-- Mini Gauge Circular -->
  <div class="neo-mini-gauge neo-gauge-orange">
    <div class="neo-mini-gauge-inner">
      <span>↗</span>
    </div>
  </div>
  <!-- Informações de Métricas -->
  <div class="neo-kpi-info">
    <span class="neo-kpi-value">348</span>
    <span class="neo-kpi-label">Users</span>
  </div>
</div>
```
```css
.neo-mini-gauge {
  width: 52px;
  height: 52px;
  border-radius: 50%;
  padding: 4px;
}
.neo-gauge-orange {
  background: conic-gradient(#ff7828 0% 75%, #f1f5f9 75% 100%);
}
.neo-mini-gauge-inner {
  width: 100%;
  height: 100%;
  background: #ffffff;
  border-radius: 50%;
  display: flex;
  align-items: center;
  justify-content: center;
  font-weight: 800;
}
```

---

### 3.4 Card de Estatísticas de Tendência (Curvas Bézier Spline)
Para gráficos de linha suaves:
1. Usar duas curvas Bézier cúbicas contrastantes: uma laranja vibrante (`#ff7828`) e uma azul-ardósia neutra (`#5b7290`).
2. Ponto de dado ativo com círculo duplo concêntrico (halo translúcido).
3. Tooltip flutuante com sombra suave e dados em duas colunas.

---

### 3.5 Medidor Semicircular de Satisfação (Half-Gauge)
Para indicadores de porcentagem geral:

```html
<div class="neo-semicircle-gauge-container">
  <svg viewBox="0 0 200 110" class="neo-gauge-svg">
    <!-- Trilho de Fundo Cinza -->
    <path d="M 20 100 A 80 80 0 0 1 180 100" fill="none" stroke="#f1f5f9" stroke-width="20" stroke-linecap="round" />
    <!-- Preenchimento de Progresso Laranja -->
    <path d="M 20 100 A 80 80 0 0 1 145 35" fill="none" stroke="url(#orangeGrad)" stroke-width="20" stroke-linecap="round" />
  </svg>
  <!-- Emblema Central Elevado -->
  <div class="neo-gauge-center-badge">👍</div>
  <!-- Porcentagem em Destaque Alto -->
  <div class="neo-gauge-percentage">74%</div>
  <div class="neo-gauge-scale">
    <span>0%</span>
    <span>100%</span>
  </div>
</div>
```

---

### 3.6 Barras de Progresso Horizontais em Cápsula (Progress Pills)
Usadas em estatísticas de equipe/desempenho:

```html
<div class="neo-progress-item">
  <div class="neo-progress-header">
    <span>Developer Team</span>
    <span>65%</span>
  </div>
  <div class="neo-progress-track">
    <div class="neo-progress-fill fill-orange" style="width: 65%;"></div>
  </div>
</div>
```
```css
.neo-progress-track {
  width: 100%;
  height: 8px;
  background-color: #f1f5f9;
  border-radius: 9999px;
  overflow: hidden;
}
.neo-progress-fill {
  height: 100%;
  border-radius: 9999px;
}
```

---

### 3.7 Gráficos de Colunas em Cápsula (Pill Bar Chart)
1. Colunas com `border-radius: 9999px` (estilo pílula vertical).
2. Altura proporcional aos valores, com alternância rítmica entre tom neutro (`#64748b`) e tom de destaque (`#ff7828`).
3. Uma linha pontilhada (`stroke-dasharray: 4, 4`) conectando os topos dos dados médios.

---

### 3.8 Tooltips Flutuantes Elevados
Contêineres com fundo branco sólido, borda `1px solid rgba(255,255,255,0.95)` e cantos `16px`:

```html
<div class="neo-floating-tooltip">
  <div class="neo-tooltip-title">2014</div>
  <div class="neo-tooltip-grid">
    <span>Marketing: 2437</span>
    <span>Design: 7689</span>
    <span>Development: 7689</span>
    <span>Others: 7689</span>
  </div>
</div>
```

---

## 4. Checklist de Conformidade com Regras Globais

- [x] **Zero Sombras em Texto**: Nenhuma propriedade `text-shadow` utilizada.
- [x] **Zero Glows**: Nenhum filtro `feGaussianBlur` ou `filter: drop-shadow` sobre elementos textuais.
- [x] **Contraste Limpo**: Texto escuro `#1e2538` sobre fundo claro; texto `#ffffff` sobre pílulas coloridas.
- [x] **Profundidade via Contêineres**: O efeito suave e volumétrico é aplicado estritamente através do `box-shadow` dos cards e pílulas.
