import * as THREE from 'three';
import { globalEventBus } from '../core/EventBus.ts';

export class CameraController {
  private camera: THREE.OrthographicCamera;
  private domElement: HTMLElement;
  private frustumSize: number = 850;
  private isPanning: boolean = false;
  private panStart: THREE.Vector2 = new THREE.Vector2();
  private lastClientPos: THREE.Vector2 = new THREE.Vector2();
  private cameraStart: THREE.Vector3 = new THREE.Vector3();

  // Limites de zoom
  private minZoom: number = 0.2;
  private maxZoom: number = 3.5;

  constructor(camera: THREE.OrthographicCamera, domElement: HTMLElement, frustumSize: number = 850) {
    this.camera = camera;
    this.domElement = domElement;
    this.frustumSize = frustumSize;

    this.initEvents();
  }

  public setFrustumSize(frustumSize: number): void {
    this.frustumSize = frustumSize;
  }

  private initEvents(): void {
    // Zoom centrado na posicao do cursor do mouse
    this.domElement.addEventListener('wheel', (e: WheelEvent) => {
      e.preventDefault();

      const rect = this.domElement.getBoundingClientRect();
      const mouseX = e.clientX - rect.left;
      const mouseY = e.clientY - rect.top;

      // Ponto no espaco de mundo antes do zoom
      const worldBefore = this.screenToWorld(mouseX, mouseY);

      // Fator de zoom exponencial suave
      const zoomFactor = e.deltaY < 0 ? 1.1 : 0.909;
      const newZoom = THREE.MathUtils.clamp(this.camera.zoom * zoomFactor, this.minZoom, this.maxZoom);
      this.camera.zoom = newZoom;
      this.camera.updateProjectionMatrix();

      // Ponto no espaco de mundo apos o zoom
      const worldAfter = this.screenToWorld(mouseX, mouseY);

      // Desloca a camera para manter o ponto sob o mouse estavel
      this.camera.position.x += (worldBefore.x - worldAfter.x);
      this.camera.position.y += (worldBefore.y - worldAfter.y);

      globalEventBus.emit('camera:zoom-changed', { zoom: this.camera.zoom });
    }, { passive: false });

    // Inicio de Pan com botao do meio (wheel) ou botao direito
    this.domElement.addEventListener('pointerdown', (e: PointerEvent) => {
      if (e.button === 1 || e.button === 2 || (e.button === 0 && e.altKey)) {
        e.preventDefault();
        this.startPan(e.clientX, e.clientY);
      }
    });

    if (typeof window !== 'undefined') {
      window.addEventListener('pointermove', (e: PointerEvent) => {
        if (this.isPanning) {
          const dx = e.clientX - this.lastClientPos.x;
          const dy = e.clientY - this.lastClientPos.y;
          this.lastClientPos.set(e.clientX, e.clientY);
          this.panBy(dx, dy);
        }
      });

      window.addEventListener('pointerup', (e: PointerEvent) => {
        if (this.isPanning && (e.button === 1 || e.button === 2 || e.button === 0)) {
          this.stopPan();
        }
      });

      window.addEventListener('pointercancel', () => {
        if (this.isPanning) {
          this.stopPan();
        }
      });
    }

    // Impede menu de contexto do botao direito para uso no pan
    this.domElement.addEventListener('contextmenu', (e) => e.preventDefault());
  }

  /**
   * Converte coordenadas em pixels do elemento canvas para coordenadas de mundo no plano Z=0.
   */
  screenToWorld(clientX: number, clientY: number): THREE.Vector3 {
    const rect = this.domElement.getBoundingClientRect();
    const ndcX = ((clientX - rect.left) / rect.width) * 2 - 1;
    const ndcY = -((clientY - rect.top) / rect.height) * 2 + 1;

    const vec = new THREE.Vector3(ndcX, ndcY, 0);
    vec.unproject(this.camera);
    vec.z = 0; // Travado no plano de nós Z=0
    return vec;
  }

  /**
   * Converte coordenadas de mundo para pixels na tela.
   */
  worldToScreen(worldPos: THREE.Vector3): { x: number; y: number } {
    const projected = worldPos.clone().project(this.camera);
    const rect = this.domElement.getBoundingClientRect();

    return {
      x: ((projected.x + 1) * 0.5) * rect.width + rect.left,
      y: ((-projected.y + 1) * 0.5) * rect.height + rect.top
    };
  }

  /**
   * Enquadra a visualizacao em uma caixa delimitadora de nós (Zoom to Fit).
   */
  fitToBounds(bounds: THREE.Box2, padding: number = 100): void {
    const center = new THREE.Vector2();
    bounds.getCenter(center);

    const size = new THREE.Vector2();
    bounds.getSize(size);

    const w = size.x + padding * 2;
    const h = size.y + padding * 2;

    const aspect = this.domElement.clientWidth / this.domElement.clientHeight;
    const zoomX = (this.frustumSize * aspect) / w;
    const zoomY = this.frustumSize / h;

    this.camera.zoom = THREE.MathUtils.clamp(Math.min(zoomX, zoomY), this.minZoom, 1.5);
    this.camera.position.set(center.x, center.y, this.camera.position.z);
    this.camera.updateProjectionMatrix();
    globalEventBus.emit('camera:zoom-changed', { zoom: this.camera.zoom });
  }

  /** Centraliza a câmera num ponto do mundo, mantendo ao menos um zoom legível. */
  centerOn(x: number, y: number, minZoom: number = 0.7): void {
    this.camera.position.set(x, y, this.camera.position.z);
    if (this.camera.zoom < minZoom) this.camera.zoom = minZoom;
    this.camera.updateProjectionMatrix();
    globalEventBus.emit('camera:zoom-changed', { zoom: this.camera.zoom });
  }

  reset(): void {
    this.camera.position.set(0, 0, 1000);
    this.camera.zoom = 1;
    this.camera.updateProjectionMatrix();
    globalEventBus.emit('camera:zoom-changed', { zoom: this.camera.zoom });
  }

  getZoom(): number {
    return this.camera.zoom;
  }

  setZoom(newZoom: number): void {
    this.camera.zoom = THREE.MathUtils.clamp(newZoom, this.minZoom, this.maxZoom);
    this.camera.updateProjectionMatrix();
    globalEventBus.emit('camera:zoom-changed', { zoom: this.camera.zoom });
  }

  zoomIn(factor: number = 1.15): void {
    this.setZoom(this.camera.zoom * factor);
  }

  zoomOut(factor: number = 0.85): void {
    this.setZoom(this.camera.zoom * factor);
  }

  panBy(screenDx: number, screenDy: number): void {
    const domH = this.domElement.clientHeight || this.domElement.parentElement?.clientHeight || (typeof window !== 'undefined' ? window.innerHeight : 800) || 800;
    const scale = (this.frustumSize / domH) / this.camera.zoom;
    this.camera.position.x -= screenDx * scale;
    this.camera.position.y += screenDy * scale;
    this.camera.updateMatrixWorld();
    globalEventBus.emit('camera:panned', { x: this.camera.position.x, y: this.camera.position.y });
  }

  startPan(clientX: number, clientY: number): void {
    this.isPanning = true;
    this.panStart.set(clientX, clientY);
    this.lastClientPos.set(clientX, clientY);
    this.cameraStart.copy(this.camera.position);
    this.domElement.style.cursor = 'grabbing';
    if (typeof document !== 'undefined' && document.body) {
      document.body.style.cursor = 'grabbing';
    }
  }

  stopPan(): void {
    if (this.isPanning) {
      this.isPanning = false;
      this.domElement.style.cursor = 'default';
      if (typeof document !== 'undefined' && document.body) {
        document.body.style.cursor = '';
      }
    }
  }

  getIsPanning(): boolean {
    return this.isPanning;
  }
}
