import {
  DEFAULT_NODE_MEMORY_CONFIG,
  DEFAULT_PIPELINE_DEFAULTS,
  type ICodeModeConfig,
  type IModelConfig,
  type INodeAgentSettings,
  type INodeMemoryConfig,
  type ISandboxPolicy
} from '../types.ts';
import { memoryModeOf, memoryModePatch } from '../memory/MemoryConfig.ts';
import type {
  IPipelineDefaultsPatch,
  IResolvedSetting,
  IResolvedSettings,
  ISettingsContext,
  SettingsOrigin
} from './types.ts';

interface ILayer<T> {
  scope: SettingsOrigin;
  source: string;
  value: Partial<T> | undefined;
}

function isDefined(value: unknown): boolean {
  return value !== undefined && value !== null;
}

function hasDefinedKeys(value: object | undefined): boolean {
  if (!value) return false;
  return Object.values(value).some(isDefined);
}

function mergePatch<T extends object>(base: T, patch: Partial<T> | undefined): T {
  if (!patch) return { ...base };
  const out = { ...base } as Record<string, unknown>;
  for (const [key, value] of Object.entries(patch)) {
    if (!isDefined(value)) continue;
    out[key] = value;
  }
  return out as T;
}

function resolveCategory<T extends object>(
  base: T,
  layers: Array<ILayer<T>>
): IResolvedSetting<T> {
  let value = mergePatch(base, undefined);
  let origin: SettingsOrigin = 'default';
  let source = 'DEFAULT_PIPELINE_DEFAULTS';
  let inheritedFrom: SettingsOrigin | undefined;

  for (const layer of layers) {
    if (!hasDefinedKeys(layer.value)) continue;
    inheritedFrom = origin;
    value = mergePatch(value, layer.value);
    origin = layer.scope;
    source = layer.source;
  }

  return {
    value,
    origin,
    source,
    inheritedFrom: origin === 'default' || inheritedFrom === 'default' ? undefined : inheritedFrom
  };
}

function sourceFor(scope: SettingsOrigin): string {
  switch (scope) {
    case 'studio':
      return 'studio.config.json';
    case 'workspace':
      return 'workspace.config.json';
    case 'project':
      return 'arestrix.project.json';
    case 'vertex':
      return 'graph node settings';
    default:
      return 'DEFAULT_PIPELINE_DEFAULTS';
  }
}

function layersFor(
  context: ISettingsContext,
  category: keyof IPipelineDefaultsPatch
): Array<ILayer<any>> {
  const studioDefaults = context.studio.defaults || {};
  const workspaceDefaults = context.workspace.defaults || {};
  const projectDefaults = context.project.defaults;
  const vertex = context.vertex;
  const layers: Array<ILayer<any>> = [
    { scope: 'studio', source: sourceFor('studio'), value: studioDefaults[category] },
    { scope: 'workspace', source: sourceFor('workspace'), value: workspaceDefaults[category] },
    { scope: 'project', source: sourceFor('project'), value: (projectDefaults as any)?.[category] }
  ];

  if (!vertex) return layers;

  const vertexValue = (vertex as any)?.[category];
  const inheritKey = category === 'model'
    ? 'inheritModel'
    : category === 'sandbox'
      ? 'inheritSandbox'
      : category === 'codeMode'
        ? 'inheritCodeMode'
        : 'inheritMemory';
  const inherited = (vertex as any)?.[inheritKey];

  // O vértice só entra na resolução quando possui override explícito.
  if (!inherited && hasDefinedKeys(vertexValue)) {
    layers.push({ scope: 'vertex', source: sourceFor('vertex'), value: vertexValue });
  }
  return layers;
}

/**
 * Resolve os valores efetivos de modelo, sandbox, ferramentas e memória.
 *
 * Precedência: default → studio → workspace → projeto → vértice.
 * O vértice só participa quando o override correspondente está desligado.
 */
export class SettingsResolver {
  public resolve(context: ISettingsContext): IResolvedSettings {
    const memory = resolveCategory<INodeMemoryConfig>(
      { ...DEFAULT_NODE_MEMORY_CONFIG, ...(DEFAULT_PIPELINE_DEFAULTS.memory || {}) },
      layersFor(context, 'memory')
    );
    memory.value = { ...memory.value, ...memoryModePatch(memoryModeOf(memory.value)) };
    return {
      model: resolveCategory<IModelConfig>(
        DEFAULT_PIPELINE_DEFAULTS.model,
        layersFor(context, 'model')
      ),
      sandbox: resolveCategory<ISandboxPolicy>(
        DEFAULT_PIPELINE_DEFAULTS.sandbox,
        layersFor(context, 'sandbox')
      ),
      codeMode: resolveCategory<ICodeModeConfig>(
        DEFAULT_PIPELINE_DEFAULTS.codeMode,
        layersFor(context, 'codeMode')
      ),
      memory
    };
  }

  public resolveForVertex(
    context: Omit<ISettingsContext, 'vertex'>,
    vertex?: INodeAgentSettings | null
  ): IResolvedSettings {
    return this.resolve({ ...context, vertex });
  }
}
