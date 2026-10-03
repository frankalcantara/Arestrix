import { TaskflowNodeType } from '../core/types.ts';

export const THEME = {
  // Paleta de Fundo e Canvas (Soft Neomorphic)
  canvasBackground: 0xf6f8fc, // Fundo claro pastel
  gridMajor: 0xe2e8f0,        // Grade suave Slate 200
  gridMinor: 0xf1f5f9,        // Grade fina Slate 100

  // Nós por tipo (Cores da paleta da imagem: Laranja, Ciano, Âmbar, Verde, Púrpura)
  nodeTypes: {
    static: {
      color: 0x3b82f6,        // Azul Real (Active item na imagem)
      badgeBg: 0x2563eb,
      textColor: '#ffffff',
      label: 'STATIC TASK'
    },
    condition: {
      color: 0xff7828,        // Laranja Vibrante da imagem
      badgeBg: 0xe06015,
      textColor: '#ffffff',
      label: 'CONDITION TASK'
    },
    subflow: {
      color: 0x8b5cf6,        // Púrpura da imagem (Reports)
      badgeBg: 0x7c3aed,
      textColor: '#ffffff',
      label: 'SUBFLOW TASK'
    },
    module: {
      color: 0x10b981,        // Verde da imagem (Payrolls)
      badgeBg: 0x059669,
      textColor: '#ffffff',
      label: 'MODULE TASK'
    },
    critical: {
      color: 0x06b6d4,        // Ciano / Turquesa da imagem (Events)
      badgeBg: 0x0891b2,
      textColor: '#ffffff',
      label: 'CRITICAL TASK'
    }
  } as Record<TaskflowNodeType, { color: number; badgeBg: number; textColor: string; label: string }>,

  // Corpo dos Nós (Cartão Branco com Elevação Suave)
  nodeBody: {
    width: 210,
    height: 105,
    borderRadius: 16,
    fillColor: 0xffffff,      // Branco puro
    borderColor: 0xe2e8f0,    // Borda muito suave
    borderSelected: 0xff7828, // Laranja selecionado
    borderWidth: 2
  },

  // Portas / Sockets
  port: {
    radius: 7,
    inputColor: 0x3b82f6,     // Azul
    outputColor: 0xff7828,    // Laranja
    hoverColor: 0x10b981,     // Verde hover
    hitboxRadius: 18
  },

  // Arestas / Linhas Curvas (Inspiradas nas linhas de tendência da imagem)
  edge: {
    defaultColor: 0x5b7290,   // Azul ardósia suave da imagem
    selectedColor: 0xff7828,  // Laranja em foco
    feedbackColor: 0xf59e0b,  // Dourado para feedback loops
    activeFlowColor: 0x10b981,// Verde
    lineWidth: 3.5,
    pulseSpeed: 1.5
  },

  // Regras Estritas de Tipografia Nítida (Zero sombras/glows em textos)
  typography: {
    fontFamily: 'Inter, system-ui, -apple-system, sans-serif',
    titleSize: 13,
    titleWeight: '700',
    badgeSize: 9,
    badgeWeight: '800',
    portSize: 10,
    portWeight: '600',
    focusedOpacity: 1.0,
    unfocusedOpacity: 0.45
  }
};
