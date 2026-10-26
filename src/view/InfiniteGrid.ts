import * as THREE from 'three';
import { THEME } from './Theme.ts';

export class InfiniteGrid {
  private mesh: THREE.Mesh;

  constructor(scene: THREE.Scene) {
    const geometry = new THREE.PlaneGeometry(2, 2); // Tela cheia normalizada

    const material = new THREE.ShaderMaterial({
      vertexShader: `
        varying vec2 vUv;
        void main() {
          vUv = uv;
          gl_Position = vec4(position.xy, -0.99, 1.0);
        }
      `,
      fragmentShader: `
        uniform vec2 uResolution;
        uniform vec2 uCameraPos;
        uniform float uZoom;
        uniform vec3 uBgColor;
        uniform vec3 uGridMajor;
        uniform vec3 uGridMinor;

        varying vec2 vUv;

        void main() {
          // Converte UV da tela para espaco de mundo
          vec2 screenCoord = (vUv - 0.5) * uResolution;
          vec2 worldPos = screenCoord / uZoom + uCameraPos;

          // Grids de 40px (menor) e 200px (maior)
          vec2 grid40 = abs(fract(worldPos / 40.0 - 0.5) - 0.5) / fwidth(worldPos / 40.0);
          vec2 grid200 = abs(fract(worldPos / 200.0 - 0.5) - 0.5) / fwidth(worldPos / 200.0);

          float line40 = 1.0 - min(min(grid40.x, grid40.y), 1.0);
          float line200 = 1.0 - min(min(grid200.x, grid200.y), 1.0);

          vec3 color = uBgColor;
          // Atenua grid menor quando o zoom diminui
          float minorAlpha = smoothstep(0.3, 0.8, uZoom) * 0.45;
          color = mix(color, uGridMinor, line40 * minorAlpha);
          color = mix(color, uGridMajor, line200 * 0.7);

          gl_FragColor = vec4(color, 1.0);
        }
      `,
      uniforms: {
        uResolution: { value: new THREE.Vector2(window.innerWidth, window.innerHeight) },
        uCameraPos: { value: new THREE.Vector2(0, 0) },
        uZoom: { value: 1.0 },
        uBgColor: { value: new THREE.Color(THEME.canvasBackground) },
        uGridMajor: { value: new THREE.Color(THEME.gridMajor) },
        uGridMinor: { value: new THREE.Color(THEME.gridMinor) }
      },
      depthWrite: false,
      depthTest: false
    });

    this.mesh = new THREE.Mesh(geometry, material);
    this.mesh.frustumCulled = false;
    scene.add(this.mesh);
  }

  update(camera: THREE.OrthographicCamera, width: number, height: number): void {
    const mat = this.mesh.material as THREE.ShaderMaterial;
    mat.uniforms.uResolution.value.set(width, height);
    mat.uniforms.uCameraPos.value.set(camera.position.x, camera.position.y);
    mat.uniforms.uZoom.value = camera.zoom;
  }

  /** Atualiza as cores do fundo/grade sem recriar a geometria. */
  setTheme(background: number, gridMajor: number, gridMinor: number): void {
    const mat = this.mesh.material as THREE.ShaderMaterial;
    mat.uniforms.uBgColor.value.set(background);
    mat.uniforms.uGridMajor.value.set(gridMajor);
    mat.uniforms.uGridMinor.value.set(gridMinor);
  }

  toggleVisible(): boolean {
    this.mesh.visible = !this.mesh.visible;
    return this.mesh.visible;
  }

  setVisible(visible: boolean): void {
    this.mesh.visible = visible;
  }

  isVisible(): boolean {
    return this.mesh.visible;
  }
}
