import * as THREE from 'three';
import { SVGLoader } from 'three/examples/jsm/loaders/SVGLoader.js';
import { mergeGeometries } from 'three/examples/jsm/utils/BufferGeometryUtils.js';

/**
 * Arestrix - Ícones Vetoriais para o Canvas WebGL (Fase 15.2)
 *
 * Converte ícones do pacote `lucide` (traços SVG 24x24) em geometria three.js real
 * (malhas de traço geradas por SVGLoader.pointsToStroke). Resultado: ícones nítidos
 * em qualquer zoom, independentes de fontes de emoji do sistema operacional.
 *
 * As geometrias são cacheadas por (ícone, tamanho) e compartilhadas entre todos os nós.
 * NÃO descarte geometrias obtidas via `getIconGeometry` ao destruir um nó.
 */

/** Formato de nó de ícone do lucide: ['svg', attrs, [[tag, attrs], ...]] */
export type LucideIconNode = [string, Record<string, string | number>, Array<[string, Record<string, string | number>]>?];

const LUCIDE_STROKE_WIDTH = 2.25;
const geometryCache = new Map<string, THREE.BufferGeometry | null>();

function attrsToString(attrs: Record<string, string | number>): string {
  return Object.entries(attrs)
    .filter(([key]) => key !== 'key')
    .map(([key, value]) => `${key}="${String(value).replace(/"/g, '&quot;')}"`)
    .join(' ');
}

/**
 * Gera a marcação SVG completa de um ícone lucide (útil também para HTML/tooltips).
 */
export function lucideToSvgMarkup(icon: LucideIconNode, strokeWidth: number = LUCIDE_STROKE_WIDTH, color: string = '#000000'): string {
  const children = (icon[2] ?? []) as Array<[string, Record<string, string | number>]>;
  const inner = children.map(([tag, attrs]) => `<${tag} ${attrsToString(attrs)}/>`).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${strokeWidth}" stroke-linecap="round" stroke-linejoin="round">${inner}</svg>`;
}

/**
 * Retorna uma geometria vetorial (centralizada na origem, eixo Y para cima) do ícone,
 * escalada para caber num quadrado de `size` unidades de mundo.
 * Retorna null em ambientes sem DOMParser (ex.: testes em Node).
 */
export function getIconGeometry(name: string, icon: LucideIconNode, size: number): THREE.BufferGeometry | null {
  const key = `${name}@${size}`;
  if (geometryCache.has(key)) {
    return geometryCache.get(key) ?? null;
  }

  if (typeof DOMParser === 'undefined') {
    geometryCache.set(key, null);
    return null;
  }

  try {
    const data = new SVGLoader().parse(lucideToSvgMarkup(icon));
    const parts: THREE.BufferGeometry[] = [];

    data.paths.forEach(path => {
      const style = (path.userData as any)?.style;
      const strokeStyle = SVGLoader.getStrokeStyle(
        style?.strokeWidth ?? LUCIDE_STROKE_WIDTH,
        '#000000',
        'round',
        'round',
        4
      );
      path.subPaths.forEach(subPath => {
        const geo = SVGLoader.pointsToStroke(subPath.getPoints(), strokeStyle);
        if (geo) {
          // Remove atributos não usados para permitir o merge homogêneo
          if (geo.hasAttribute('uv')) geo.deleteAttribute('uv');
          if (geo.hasAttribute('normal')) geo.deleteAttribute('normal');
          parts.push(geo);
        }
      });
    });

    if (parts.length === 0) {
      geometryCache.set(key, null);
      return null;
    }

    const merged = mergeGeometries(parts, false);
    parts.forEach(p => p.dispose());
    if (!merged) {
      geometryCache.set(key, null);
      return null;
    }

    // SVG: origem no canto superior esquerdo e Y para baixo -> centraliza e inverte Y
    const scale = size / 24;
    merged.translate(-12, -12, 0);
    merged.scale(scale, -scale, 1);
    merged.computeBoundingSphere();

    geometryCache.set(key, merged);
    return merged;
  } catch (err) {
    console.warn(`[VectorIcons] Falha ao gerar o ícone "${name}":`, err);
    geometryCache.set(key, null);
    return null;
  }
}

/**
 * Cria uma malha de ícone vetorial com material próprio (cor independente por instância).
 * Em ambientes sem DOM, retorna uma malha vazia (geometria nula substituída por BufferGeometry vazia).
 */
export function createIconMesh(name: string, icon: LucideIconNode, size: number, color: string | number): THREE.Mesh {
  const geometry = getIconGeometry(name, icon, size);
  const material = new THREE.MeshBasicMaterial({ color, side: THREE.DoubleSide });
  const mesh = new THREE.Mesh(geometry ?? new THREE.BufferGeometry(), material);
  mesh.userData = { isVectorIcon: true, iconName: name, sharedGeometry: geometry !== null };
  mesh.raycast = () => {};
  return mesh;
}

/**
 * Libera uma malha de ícone: descarta apenas o material (e a geometria se não for compartilhada).
 */
export function disposeIconMesh(mesh: THREE.Mesh | undefined | null): void {
  if (!mesh) return;
  (mesh.material as THREE.Material).dispose();
  if (!mesh.userData?.sharedGeometry) {
    mesh.geometry.dispose();
  }
}
