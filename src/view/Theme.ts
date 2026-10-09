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
    }
  } as Record<TaskflowNodeType, { color: number; badgeBg: number; textColor: string; label: string }>,

  // Corpo dos Nós (Cartão Branco Técnico Visio com bordas limpas de 6px)
  nodeBody: {
    width: 260,
    height: 130,
    headerHeight: 32,
    borderRadius: 6,          // Cantos limpos e precisos do padrão Visio
    fillColor: 0xffffff,      // Branco puro
    borderColor: 0xcbd5e1,    // Borda nítida 1px cinza neutro
    borderSelected: 0x0f6cbd, // Azul Office selecionado
    borderWidth: 2
  },

  // Portas / Conectores Visio
  port: {
    radius: 7,
    inputColor: 0x0f6cbd,     // Azul Office
    outputColor: 0xc43e1c,    // Laranja Office
    hoverColor: 0x107c41,     // Verde hover
    hitboxRadius: 18
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
  typography: {
    fontFamily: 'Segoe UI, Inter, -apple-system, BlinkMacSystemFont, sans-serif',
    titleSize: 18,
    titleWeight: '700',
    badgeSize: 12,
    badgeWeight: '700',
    portSize: 11,
    portWeight: '600',
    focusedOpacity: 1.0,
    unfocusedOpacity: 0.5
  }
};
