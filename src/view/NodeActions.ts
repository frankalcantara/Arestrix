import {
  ShieldCheck,
  BrainCircuit,
  Wrench,
  Database,
  BookOpenText,
  Bot,
  Square,
  GitBranch,
  Workflow,
  Package,
  Siren,
  Cpu,
  Split,
  Merge
} from 'lucide';
import type { TaskflowNodeType } from '../core/types.ts';
import type { LucideIconNode } from './icons/VectorIcons.ts';

/**
 * Arestrix - Catálogo de Ações Rápidas dos Nós (Fase 15.2)
 *
 * Fonte única para ícone, rótulo, descrição (tooltip), cor de identidade e evento
 * disparado por cada botão da barra flutuante acima do nó.
 */

export type NodeActionType = 'sandbox' | 'model' | 'tools' | 'memory' | 'skills';

export interface INodeActionDef {
  type: NodeActionType;
  iconName: string;
  icon: LucideIconNode;
  label: string;
  description: string;
  event: string;
  color: string;
}

export const NODE_ACTIONS: readonly INodeActionDef[] = [
  {
    type: 'sandbox',
    iconName: 'shield-check',
    icon: ShieldCheck as unknown as LucideIconNode,
    label: 'Sandbox & Isolamento',
    description: 'Ambiente de execução, rede e limites do agente',
    event: 'node:open-sandbox-modal',
    color: '#0F766E'
  },
  {
    type: 'model',
    iconName: 'brain-circuit',
    icon: BrainCircuit as unknown as LucideIconNode,
    label: 'Modelo de IA, Skill & Prompt',
    description: 'Provedor, modelo, arquivo de skill e template de prompt',
    event: 'node:open-model-modal',
    color: '#4F46E5'
  },
  {
    type: 'tools',
    iconName: 'wrench',
    icon: Wrench as unknown as LucideIconNode,
    label: 'Ferramentas & MCP',
    description: 'Ferramentas permitidas e servidores MCP do agente',
    event: 'node:open-tools-modal',
    color: '#B45309'
  },
  {
    type: 'memory',
    iconName: 'database',
    icon: Database as unknown as LucideIconNode,
    label: 'Memória do Agente',
    description: 'Memória efêmera ou persistente e domínios de conhecimento',
    event: 'node:open-memory-modal',
    color: '#0369A1'
  },
  {
    type: 'skills',
    iconName: 'book-open-text',
    icon: BookOpenText as unknown as LucideIconNode,
    label: 'Skills do Agente',
    description: 'Habilidades vinculadas e editor de SKILL.md',
    event: 'node:open-skills-modal',
    color: '#7C3AED'
  }
];

export function getNodeAction(type: string): INodeActionDef | undefined {
  return NODE_ACTIONS.find(a => a.type === type);
}

/**
 * Ícone vetorial exibido no header de cada tipo de nó (ao lado do rótulo de tipo).
 */
export const NODE_TYPE_ICONS: Record<TaskflowNodeType, { name: string; icon: LucideIconNode }> = {
  static: { name: 'square', icon: Square as unknown as LucideIconNode },
  condition: { name: 'git-branch', icon: GitBranch as unknown as LucideIconNode },
  subflow: { name: 'workflow', icon: Workflow as unknown as LucideIconNode },
  module: { name: 'package', icon: Package as unknown as LucideIconNode },
  critical: { name: 'siren', icon: Siren as unknown as LucideIconNode },
  cuda: { name: 'cpu', icon: Cpu as unknown as LucideIconNode },
  agent: { name: 'bot', icon: Bot as unknown as LucideIconNode },
  fork: { name: 'split', icon: Split as unknown as LucideIconNode },
  join: { name: 'merge', icon: Merge as unknown as LucideIconNode }
} as Record<TaskflowNodeType, { name: string; icon: LucideIconNode }>;
