import type { RibbonIconName } from '../ribbon/RibbonIcons.ts';

export type PropertiesScope = 'studio' | 'workspace' | 'project';

export interface IPropertyAction {
  id: string;
  label: string;
  kind?: 'primary' | 'secondary';
}

export interface IPropertySection {
  id: string;
  title: string;
  description: string;
  icon: RibbonIconName;
  badge?: string;
  content: string;
  actions?: IPropertyAction[];
  /** Quando definido, a seção pode ser editada e o rodapé ganha Salvar/Cancelar. */
  save?: (values: Record<string, string>) => Promise<boolean> | boolean;
}

export interface IPropertiesScopeConfig {
  scope: PropertiesScope;
  title: string;
  subtitle: string;
  scopeLabel: string;
  sections: IPropertySection[];
  readOnlyNote: string;
  inheritedFrom?: string;
}

export type PropertyActionHandler = (actionId: string) => void;
