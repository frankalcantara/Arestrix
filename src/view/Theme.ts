import { TaskflowNodeType } from '../core/types.ts';

export const THEME = {
  // Paleta de Fundo e Canvas (Soft Neomorphic)
  canvasBackground: 0xf6f8fc, // Fundo claro pastel
  gridMajor: 0xe2e8f0,        // Grade suave Slate 200
  gridMinor: 0xf1f5f9,        // Grade fina Slate 100

  // Nós por tipo (Cores da paleta: Laranja, Ciano, Âmbar, Verde, Púrpura, Azul)
  nodeTypes: {
    static: {
      color: 0x3b82f6,        // Azul Real
      badgeBg: 0x1d4ed8,      // Azul 700 (mais contraste)
      textColor: '#ffffff',
      label: 'STATIC TASK'
    },
    condition: {
      color: 0xff7828,        // Laranja Vibrante
      badgeBg: 0xc2410c,      // Laranja 700 (mais contraste)
      textColor: '#ffffff',
      label: 'CONDITION TASK'
    },
    subflow: {
      color: 0x8b5cf6,        // Púrpura
      badgeBg: 0x6d28d9,      // Púrpura 700
      textColor: '#ffffff',
      label: 'SUBFLOW TASK'
    },
    module: {
      color: 0x10b981,        // Verde
      badgeBg: 0x047857,      // Verde 700
      textColor: '#ffffff',
      label: 'MODULE TASK'
    },
    critical: {
      color: 0x06b6d4,        // Ciano
      badgeBg: 0x0e7490,      // Ciano 700
      textColor: '#ffffff',
      label: 'CRITICAL TASK'
    },
    cuda: {
      color: 0x14b8a6,        // Teal (Turquesa escuro) 
      badgeBg: 0x0f766e,      // Teal 700
      textColor: '#ffffff',
      label: 'CUDA KERNEL'
    },
    agent: {
      color: 0x6366f1,        // Indigo 500 (Vibrante)
      badgeBg: 0x4338ca,      // Indigo 700 (Alto Contraste)
      textColor: '#ffffff',
      label: 'AGENT CODE MODE'
    }
  } as Record<TaskflowNodeType, { color: number; badgeBg: number; textColor: string; label: string }>,

  // Corpo dos Nós (Cartão Branco com Elevação Suave e Proporções Ampliadas)
  nodeBody: {
    width: 260,
    height: 130,
    headerHeight: 36,
    borderRadius: 16,
    fillColor: 0xffffff,      // Branco puro
    borderColor: 0xe2e8f0,    // Borda muito suave
    borderSelected: 0xff7828, // Laranja selecionado
    borderWidth: 2
  },

  // Portas / Sockets
  port: {
    radius: 8,
    inputColor: 0x3b82f6,     // Azul
    outputColor: 0xff7828,    // Laranja
    hoverColor: 0x10b981,     // Verde hover
    hitboxRadius: 20
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
    titleSize: 18,
    titleWeight: '800',
    badgeSize: 13,
    badgeWeight: '800',
    portSize: 11,
    portWeight: '700',
    focusedOpacity: 1.0,
    unfocusedOpacity: 0.5
  }
};
