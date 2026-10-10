import { TaskflowNodeType } from '../core/types.ts';

export const THEME = {
  // Paleta de Fundo e Canvas (Microsoft Visio Technical Diagramming)
  canvasBackground: 0xfafbfc, // Fundo límpido técnico de prancheta Visio
  gridMajor: 0xd9e2ec,        // Grade principal Visio Slate 300
  gridMinor: 0xedf2f7,        // Grade fina Visio Slate 100

  // Nós por tipo (Cores da paleta corporativa Microsoft Office Fluent)
  nodeTypes: {
    static: {
      color: 0x0f6cbd,        // Azul Clássico Office / Visio
      badgeBg: 0x115ea3,      // Azul 700 Office
      textColor: '#ffffff',
      label: 'STATIC TASK'
    },
    condition: {
      color: 0xc43e1c,        // Laranja PowerPoint / Rust
      badgeBg: 0x9e2f14,      // Rust 700
      textColor: '#ffffff',
      label: 'CONDITION TASK'
    },
    subflow: {
      color: 0x5c2d91,        // Púrpura Teams / OneNote
      badgeBg: 0x44206a,      // Púrpura 700
      textColor: '#ffffff',
      label: 'SUBFLOW TASK'
    },
    module: {
      color: 0x107c41,        // Verde Excel
      badgeBg: 0x0c592e,      // Verde 700
      textColor: '#ffffff',
      label: 'MODULE TASK'
    },
    critical: {
      color: 0x008272,        // Ciano SharePoint / Azure
      badgeBg: 0x005c51,      // Teal 700
      textColor: '#ffffff',
      label: 'CRITICAL TASK'
    },
    cuda: {
      color: 0x0078d4,        // Azul Cyan Fluent
      badgeBg: 0x005a9e,      // Azul Fluent 700
      textColor: '#ffffff',
      label: 'CUDA KERNEL'
    },
    agent: {
      color: 0x4f46e5,        // Indigo Fluent IA
      badgeBg: 0x3730a3,      // Indigo 700
      textColor: '#ffffff',
      label: 'AGENT CODE MODE'
    },
    fork: {
      color: 0x008272,        // Teal / Despachante Concorrente Fluent
      badgeBg: 0x005c51,      // Teal 700
      textColor: '#ffffff',
      label: 'FORK / DISPACHER'
    },
    join: {
      color: 0x5c2d91,        // Púrpura / Barreira de Sincronização Fluent
      badgeBg: 0x44206a,      // Púrpura 700
      textColor: '#ffffff',
      label: 'JOIN / BARRIER'
    }
  } as Record<TaskflowNodeType, { color: number; badgeBg: number; textColor: string; label: string }>,

  // Corpo dos Nós (Cartão Branco Técnico Visio com bordas limpas de 6px)
  // Fase 15.2: cartão mais largo, header mais alto e espaçamento de portas maior para legibilidade.
  nodeBody: {
    width: 300,
    height: 150,
    headerHeight: 38,
    borderRadius: 6,          // Cantos limpos e precisos do padrão Visio
    fillColor: 0xffffff,      // Branco puro
    borderColor: 0xcbd5e1,    // Borda nítida cinza neutro
    borderSelected: 0x0f6cbd, // Azul Office selecionado
    borderWidth: 1.5,         // Espessura real (anel geométrico, não LineLoop de 1px)
    selectionWidth: 3,        // Anel externo de seleção / execução
    paddingX: 16,
    titleGap: 12,             // Distância entre o header e o título
    titleMaxLines: 2,
    portSpacing: 32,
    portZoneBottomPadding: 14
  },

  // Portas / Conectores Visio
  port: {
    radius: 7,
    inputColor: 0x0f6cbd,     // Azul Office
    outputColor: 0xc43e1c,    // Laranja Office
    hoverColor: 0x107c41,     // Verde hover
    hitboxRadius: 18,
    labelOffset: 16
  },

  // Arestas / Linhas Conectoras do Visio
  edge: {
    defaultColor: 0x64748b,   // Linha de conector Visio
    selectedColor: 0x0f6cbd,  // Azul Office de seleção
    feedbackColor: 0xd97706,  // Âmbar para feedback loops
    activeFlowColor: 0x107c41,// Verde de execução ativa
    lineWidth: 3.0,
    pulseSpeed: 1.5
  },

  // Regras Estritas de Tipografia Nítida (Zero sombras/glows em textos)
  // Fase 15.2: tamanhos em unidades de mundo, renderizados como SDF vetorial (troika-three-text).
  typography: {
    fontFamily: 'Segoe UI, Inter, -apple-system, BlinkMacSystemFont, sans-serif',
    titleSize: 18,
    titleWeight: 700,
    titleColor: '#0F172A',    // Slate 900
    badgeSize: 13,            // Rótulo de tipo no header (ex.: AGENT CODE MODE)
    badgeWeight: 700,
    badgeColor: '#FFFFFF',
    badgeLetterSpacing: 0.04,
    portSize: 13,
    portWeight: 600,
    portColor: '#334155',     // Slate 700 (contraste >= 7:1 sobre branco)
    telemetrySize: 12,
    telemetryWeight: 600,
    focusedOpacity: 1.0,
    unfocusedOpacity: 0.5
  },

  // Barra de ações flutuante acima do nó (ícones vetoriais Lucide)
  actionBar: {
    buttonSize: 26,
    buttonGap: 4,
    iconSize: 15,
    padding: 4,
    offsetY: 6,
    fill: 0xffffff,
    border: 0xcbd5e1,
    hoverFill: 0xeef2ff
  },

  // Nível de detalhe: abaixo deste zoom, rótulos de portas e barra de ações são ocultados
  lod: {
    detailZoomThreshold: 0.45
  }
};
