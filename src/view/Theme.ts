import { TaskflowNodeType } from '../core/types.ts';

/**
 * Calcula a luminância relativa (WCAG 2.1) de uma cor hexadecimal ou número.
 */
export function getRelativeLuminance(color: string | number): number {
  let hex: string;
  if (typeof color === 'number') {
    hex = color.toString(16).padStart(6, '0');
  } else {
    hex = color.replace(/^#/, '');
  }
  if (hex.length === 3) {
    hex = hex.split('').map(c => c + c).join('');
  }
  const r = parseInt(hex.substring(0, 2), 16) / 255;
  const g = parseInt(hex.substring(2, 4), 16) / 255;
  const b = parseInt(hex.substring(4, 6), 16) / 255;

  const [rs, gs, bs] = [r, g, b].map(c => 
    c <= 0.03928 ? c / 12.92 : Math.pow((c + 0.055) / 1.055, 2.4)
  );
  return 0.2126 * rs + 0.7152 * gs + 0.0722 * bs;
}

/**
 * Calcula a razão de contraste entre duas luminâncias (WCAG 2.1).
 */
export function getContrastRatio(lum1: number, lum2: number): number {
  const lighter = Math.max(lum1, lum2);
  const darker = Math.min(lum1, lum2);
  return (lighter + 0.05) / (darker + 0.05);
}

/**
 * Retorna a cor de texto ideal ('#FFFFFF' ou '#0F172A') para maximizar
 * o contraste sobre a cor da barra de título do vértice (WCAG 2.1 AA/AAA).
 */
export function getContrastTextColor(bgColor: string | number): '#FFFFFF' | '#0F172A' {
  const bgLum = getRelativeLuminance(bgColor);
  const contrastWithWhite = getContrastRatio(1.0, bgLum);
  const contrastWithDark = getContrastRatio(bgLum, 0.013); // Slate 900

  return contrastWithWhite >= contrastWithDark ? '#FFFFFF' : '#0F172A';
}

export const THEME = {
  // Paleta de Fundo e Canvas (Technical Diagramming)
  canvasBackground: 0xfafbfc, // Fundo límpido técnico de prancheta
  gridMajor: 0xd9e2ec,        // Grade principal Slate 300
  gridMinor: 0xedf2f7,        // Grade fina Slate 100

  // Vértices por tipo (Cores da paleta corporativa Microsoft Office Fluent)
  nodeTypes: {
    static: {
      color: 0x0f6cbd,        // Azul Clássico Office
      badgeBg: 0x115ea3,      // Azul 700 Office
      textColor: getContrastTextColor(0x0f6cbd),
      label: 'STATIC TASK'
    },
    condition: {
      color: 0xc43e1c,        // Laranja PowerPoint / Rust
      badgeBg: 0x9e2f14,      // Rust 700
      textColor: getContrastTextColor(0xc43e1c),
      label: 'CONDITION TASK'
    },
    mcp: {
      color: 0x0e7490,        // Ciano profundo (integração externa)
      badgeBg: 0x0b5568,      // Ciano 700
      textColor: getContrastTextColor(0x0e7490),
      label: 'MCP TOOL'
    },
    switch: {
      color: 0x986f0b,        // Ouro Fluent (seleção multivia)
      badgeBg: 0x75540a,      // Ouro 700
      textColor: getContrastTextColor(0x986f0b),
      label: 'SWITCH'
    },
    subflow: {
      color: 0x5c2d91,        // Púrpura Teams / OneNote
      badgeBg: 0x44206a,      // Púrpura 700
      textColor: getContrastTextColor(0x5c2d91),
      label: 'SUBFLOW TASK'
    },
    module: {
      color: 0x107c41,        // Verde Excel
      badgeBg: 0x0c592e,      // Verde 700
      textColor: getContrastTextColor(0x107c41),
      label: 'MODULE TASK'
    },
    critical: {
      color: 0x008272,        // Ciano SharePoint / Azure
      badgeBg: 0x005c51,      // Teal 700
      textColor: getContrastTextColor(0x008272),
      label: 'CRITICAL TASK'
    },
    cuda: {
      color: 0x0078d4,        // Azul Cyan Fluent
      badgeBg: 0x005a9e,      // Azul Fluent 700
      textColor: getContrastTextColor(0x0078d4),
      label: 'CUDA KERNEL'
    },
    agent: {
      color: 0x4f46e5,        // Indigo Fluent IA
      badgeBg: 0x3730a3,      // Indigo 700
      textColor: getContrastTextColor(0x4f46e5),
      label: 'AGENT CODE MODE'
    },
    fork: {
      color: 0x008272,        // Teal / Despachante Concorrente Fluent
      badgeBg: 0x005c51,      // Teal 700
      textColor: getContrastTextColor(0x008272),
      label: 'FORK / DISPATCHER'
    },
    join: {
      color: 0x5c2d91,        // Púrpura / Barreira de Sincronização Fluent
      badgeBg: 0x44206a,      // Púrpura 700
      textColor: getContrastTextColor(0x5c2d91),
      label: 'JOIN / BARRIER'
    },
    output: {
      color: 0x047857,        // Esmeralda 700 Fluent / Entrega e Artefatos
      badgeBg: 0x065f46,      // Esmeralda 800
      textColor: getContrastTextColor(0x047857),
      label: 'OUTPUT / ARTIFACT'
    },
    interaction: {
      color: 0xdb2777,        // Rosa/Magenta Fluent / Interação humana
      badgeBg: 0x9d174d,      // Rosa 800
      textColor: getContrastTextColor(0xdb2777),
      label: 'HUMAN INTERACTION'
    },
    call: {
      color: 0x7c3aed,
      badgeBg: 0x5b21b6,
      textColor: getContrastTextColor(0x7c3aed),
      label: 'CALL VERTEX'
    }
  } as Record<TaskflowNodeType, { color: number; badgeBg: number; textColor: string; label: string }>,

  // Corpo dos Vértices (Cartão Técnico com proporções generosas e cantos limpos de 8px)
  // Legibilidade vetorizada: cartão de 340px, header de 44px e espaçamento claro.
  nodeBody: {
    width: 340,
    height: 160,
    headerHeight: 44,
    borderRadius: 6,          // Cantos sóbrios de 6px do Fluent/Visio
    fillColor: 0xffffff,      // Branco puro
    borderColor: 0xcbd5e1,    // Borda nítida cinza neutro
    borderSelected: 0x0f6cbd, // Azul Office selecionado
    borderWidth: 1.5,         // Espessura real da borda
    selectionWidth: 3,        // Anel externo de seleção / execução
    paddingX: 18,
    titleGap: 14,             // Distância entre o header e o título
    titleMaxLines: 2,
    portSpacing: 34,
    portZoneBottomPadding: 16
  },

  // Portas / Conectores
  port: {
    radius: 7,
    inputColor: 0x0f6cbd,     // Azul Office
    outputColor: 0xc43e1c,    // Laranja Office
    hoverColor: 0x107c41,     // Verde hover
    hitboxRadius: 18,
    labelOffset: 16
  },

  // Arestas / Linhas Conectoras do Diagrama
  edge: {
    defaultColor: 0x64748b,   // Linha de conector neutro
    selectedColor: 0x0f6cbd,  // Azul Office de seleção
    feedbackColor: 0xd97706,  // Âmbar para feedback loops
    callColor: 0x7c3aed,      // Violeta para chamadas/delegação
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
