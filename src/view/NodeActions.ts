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
  Merge,
  ListTree,
  Plug,
  FolderDown,
  MessageCircle,
  PhoneCall
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

import { t } from '../i18n/index.ts';

export const NODE_ACTIONS: readonly INodeActionDef[] = [
  {
    type: 'sandbox',
    iconName: 'shield-check',
    icon: ShieldCheck,
    get label() { return t('actions.sandbox.label'); },
    get description() { return t('actions.sandbox.description'); },
    event: 'node:open-sandbox-modal',
    color: '#0F766E'
  },
  {
    type: 'model',
    iconName: 'brain-circuit',
    icon: BrainCircuit,
    get label() { return t('actions.model.label'); },
    get description() { return t('actions.model.description'); },
    event: 'node:open-model-modal',
    color: '#4F46E5'
  },
  {
    type: 'tools',
    iconName: 'wrench',
    icon: Wrench,
    get label() { return t('actions.tools.label'); },
    get description() { return t('actions.tools.description'); },
    event: 'node:open-tools-modal',
    color: '#B45309'
  },
  {
    type: 'memory',
    iconName: 'database',
    icon: Database,
    get label() { return t('actions.memory.label'); },
    get description() { return t('actions.memory.description'); },
    event: 'node:open-memory-modal',
    color: '#0369A1'
  },
  {
    type: 'skills',
    iconName: 'book-open-text',
    icon: BookOpenText,
    get label() { return t('actions.skills.label'); },
    get description() { return t('actions.skills.description'); },
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
  static: { name: 'square', icon: Square },
  condition: { name: 'git-branch', icon: GitBranch },
  subflow: { name: 'workflow', icon: Workflow },
  module: { name: 'package', icon: Package },
  critical: { name: 'siren', icon: Siren },
  cuda: { name: 'cpu', icon: Cpu },
  agent: { name: 'bot', icon: Bot },
  fork: { name: 'split', icon: Split },
  join: { name: 'merge', icon: Merge },
  switch: { name: 'list-tree', icon: ListTree },
  mcp: { name: 'plug', icon: Plug },
  interaction: { name: 'message-circle', icon: MessageCircle },
  call: { name: 'phone-call', icon: PhoneCall },
  output: { name: 'folder-down', icon: FolderDown }
} as Record<TaskflowNodeType, { name: string; icon: LucideIconNode }>;
