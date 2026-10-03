import * as THREE from 'three';

export class CameraController {
  private camera: THREE.OrthographicCamera;
  private domElement: HTMLElement;
  private frustumSize: number = 1000;
  private isPanning: boolean = false;
  private panStart: THREE.Vector2 = new THREE.Vector2();
  private cameraStart: THREE.Vector3 = new THREE.Vector3();

  // Limites de zoom
  private minZoom: number = 0.2;
  private maxZoom: number = 3.5;

  constructor(camera: THREE.OrthographicCamera, domElement: HTMLElement, frustumSize: number = 1000) {
    this.camera = camera;
    this.domElement = domElement;
    this.frustumSize = frustumSize;

    this.initEvents();
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
    }, { passive: false });

    // Inicio de Pan com botao do meio (wheel) ou botao direito
    this.domElement.addEventListener('pointerdown', (e: PointerEvent) => {
      if (e.button === 1 || e.button === 2 || (e.button === 0 && e.altKey)) {
        e.preventDefault();
        this.isPanning = true;
        this.panStart.set(e.clientX, e.clientY);
        this.cameraStart.copy(this.camera.position);
        this.domElement.style.cursor = 'grab';
      }
    });

    window.addEventListener('pointermove', (e: PointerEvent) => {
      if (this.isPanning) {
        const dx = e.clientX - this.panStart.x;
        const dy = e.clientY - this.panStart.y;

        // Na camera ortografica, a escala de deslocamento do mundo e proporcional ao zoom
        const scale = (this.frustumSize / this.domElement.clientHeight) / this.camera.zoom;
        this.camera.position.x = this.cameraStart.x - dx * scale;
        this.camera.position.y = this.cameraStart.y + dy * scale;
      }
    });

    window.addEventListener('pointerup', (e: PointerEvent) => {
      if (this.isPanning && (e.button === 1 || e.button === 2 || e.button === 0)) {
        this.isPanning = false;
        this.domElement.style.cursor = 'default';
      }
    });

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
  }

  reset(): void {
    this.camera.position.set(0, 0, 1000);
    this.camera.zoom = 1;
    this.camera.updateProjectionMatrix();
  }

  getIsPanning(): boolean {
    return this.isPanning;
  }
}
