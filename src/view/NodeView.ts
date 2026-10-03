import * as THREE from 'three';
import { INodeData, IPort } from '../core/types.ts';
import { THEME } from './Theme.ts';

export class NodeView {
  public group: THREE.Group;
  public nodeData: INodeData;
  public isSelected: boolean = false;

  private bodyMesh!: THREE.Mesh;
  private headerMesh!: THREE.Mesh;
  private borderLine!: THREE.LineLoop;
  private titleSprite!: THREE.Sprite;
  private badgeSprite!: THREE.Sprite;

  private portMeshes: Map<string, THREE.Mesh> = new Map();
  private portHitboxes: Map<string, THREE.Mesh> = new Map();

  constructor(nodeData: INodeData) {
    this.nodeData = nodeData;
    this.group = new THREE.Group();
    this.group.position.set(nodeData.position.x, nodeData.position.y, 0);
    this.group.userData = { nodeId: nodeData.id, isNode: true };

    this.buildNode();
  }

  private buildNode(): void {
    const width = THEME.nodeBody.width;
    const height = Math.max(THEME.nodeBody.height, 40 + Math.max(this.nodeData.inputs.length, this.nodeData.outputs.length) * 24);
    const radius = THEME.nodeBody.borderRadius;
    const typeTheme = THEME.nodeTypes[this.nodeData.type] || THEME.nodeTypes.static;

    // 1. Geometria com cantos arredondados para o corpo
    const shape = this.createRoundedRectShape(width, height, radius);
    const geometry = new THREE.ShapeGeometry(shape);

    const bodyMaterial = new THREE.MeshBasicMaterial({
      color: THEME.nodeBody.fillColor,
      side: THREE.DoubleSide
    });

    this.bodyMesh = new THREE.Mesh(geometry, bodyMaterial);
    this.bodyMesh.userData = { nodeId: this.nodeData.id, isBody: true };
    this.group.add(this.bodyMesh);

    // 2. Borda do nó
    const edgePoints = shape.getPoints(24);
    const borderGeo = new THREE.BufferGeometry().setFromPoints(edgePoints);
    const borderMat = new THREE.LineBasicMaterial({
      color: THEME.nodeBody.borderColor,
      linewidth: 1
    });
    this.borderLine = new THREE.LineLoop(borderGeo, borderMat);
    this.borderLine.position.z = 1;
    this.group.add(this.borderLine);

    // 3. Header Colorido
    const headerHeight = 28;
    const headerShape = this.createHeaderShape(width, height, headerHeight, radius);
    const headerGeo = new THREE.ShapeGeometry(headerShape);
    const headerMat = new THREE.MeshBasicMaterial({
      color: typeTheme.color,
      side: THREE.DoubleSide
    });
    this.headerMesh = new THREE.Mesh(headerGeo, headerMat);
    this.headerMesh.position.z = 2;
    this.group.add(this.headerMesh);

    // 4. Badge e Tipografia Nítida (SEM SOMBRAS NEM GLOW - REGRA DO USUÁRIO)
    // Criamos texturas HiDPI com renderizacao vetorial de texto nítido
    this.createTypography(width, height, headerHeight, typeTheme);

    // 5. Portas de Entrada e Saída
    this.createPorts(width, height, headerHeight);
  }

  /**
   * Tipografia nítida atendendo à diretriz:
   * "NUNCA utilizar efeitos de sombra ou brilho em elementos de texto...
   * Alternativas limpas: cor de contraste, peso tipográfico 700/800, badge de fundo limpo."
   */
  private createTypography(width: number, height: number, headerHeight: number, typeTheme: any): void {
    // A. Badge de Tipo no Topo
    const badgeCanvas = document.createElement('canvas');
    badgeCanvas.width = 380;
    badgeCanvas.height = 64;
    const bCtx = badgeCanvas.getContext('2d')!;
    bCtx.imageSmoothingEnabled = false;

    // Fundo da Pílula / Badge limpo com cantos arredondados
    bCtx.fillStyle = `#${typeTheme.badgeBg.toString(16).padStart(6, '0')}`;
    this.roundRect(bCtx, 10, 10, 360, 44, 8);
    bCtx.fill();

    // Texto do Badge perfeitamente nítido e em alto peso
    bCtx.fillStyle = '#FFFFFF';
    bCtx.font = 'bold 22px Inter, system-ui, sans-serif';
    bCtx.textAlign = 'center';
    bCtx.textBaseline = 'middle';
    bCtx.fillText(typeTheme.label, 190, 32);

    const badgeTexture = new THREE.CanvasTexture(badgeCanvas);
    badgeTexture.minFilter = THREE.LinearFilter;
    const badgeMat = new THREE.SpriteMaterial({ map: badgeTexture, transparent: true });
    this.badgeSprite = new THREE.Sprite(badgeMat);
    this.badgeSprite.scale.set(110, 18, 1);
    this.badgeSprite.position.set(0, height / 2 - headerHeight / 2, 4);
    this.group.add(this.badgeSprite);

    // B. Título do Nó (Label)
    const titleCanvas = document.createElement('canvas');
    titleCanvas.width = 512;
    titleCanvas.height = 80;
    const tCtx = titleCanvas.getContext('2d')!;

    // Fundo de pílula suave e limpo (Slate 100)
    tCtx.fillStyle = '#F1F5F9';
    this.roundRect(tCtx, 8, 8, 496, 64, 12);
    tCtx.fill();

    // Texto com peso tipográfico 800 e alto contraste limpo (#1E2538) - ZERO SOMBRAS
    tCtx.fillStyle = '#1E2538';
    tCtx.font = '800 28px Inter, system-ui, sans-serif';
    tCtx.textAlign = 'center';
    tCtx.textBaseline = 'middle';
    tCtx.fillText(this.nodeData.label, 256, 40);

    const titleTexture = new THREE.CanvasTexture(titleCanvas);
    titleTexture.minFilter = THREE.LinearFilter;
    const titleMat = new THREE.SpriteMaterial({ map: titleTexture, transparent: true });
    this.titleSprite = new THREE.Sprite(titleMat);
    this.titleSprite.scale.set(width - 24, 26, 1);
    this.titleSprite.position.set(0, height / 2 - headerHeight - 20, 4);
    this.group.add(this.titleSprite);
  }

  private createPorts(width: number, height: number, headerHeight: number): void {
    const halfW = width / 2;
    const halfH = height / 2;
    const usableH = height - headerHeight - 20;

    // Portas de Entrada (Esquerda)
    const inCount = this.nodeData.inputs.length;
    this.nodeData.inputs.forEach((port, idx) => {
      const spacing = usableH / (inCount + 1);
      const py = halfH - headerHeight - (idx + 1) * spacing;
      this.createSinglePort(port, -halfW, py);
    });

    // Portas de Saída (Direita)
    const outCount = this.nodeData.outputs.length;
    this.nodeData.outputs.forEach((port, idx) => {
      const spacing = usableH / (outCount + 1);
      const py = halfH - headerHeight - (idx + 1) * spacing;
      this.createSinglePort(port, halfW, py);
    });
  }

  private createSinglePort(port: IPort, px: number, py: number): void {
    const isInput = port.direction === 'input';
    const isConditionBranch = port.conditionBranchIndex !== undefined;

    let portColor = isInput ? THEME.port.inputColor : THEME.port.outputColor;
    if (isConditionBranch) {
      portColor = port.conditionBranchIndex === 0 ? 0x10b981 : 0xf59e0b;
    }

    // Circulo visual da porta
    const portGeo = new THREE.CircleGeometry(THEME.port.radius, 16);
    const portMat = new THREE.MeshBasicMaterial({ color: portColor });
    const portMesh = new THREE.Mesh(portGeo, portMat);
    portMesh.position.set(px, py, 3);
    portMesh.userData = {
      isPort: true,
      portId: port.id,
      nodeId: this.nodeData.id,
      direction: port.direction
    };
    this.group.add(portMesh);
    this.portMeshes.set(port.id, portMesh);

    // Hitbox invisivel expandida para clique facil
    const hitGeo = new THREE.CircleGeometry(THEME.port.hitboxRadius, 12);
    const hitMat = new THREE.MeshBasicMaterial({ visible: false });
    const hitMesh = new THREE.Mesh(hitGeo, hitMat);
    hitMesh.position.set(px, py, 3);
    hitMesh.userData = {
      isPortHitbox: true,
      portId: port.id,
      nodeId: this.nodeData.id,
      direction: port.direction
    };
    this.group.add(hitMesh);
    this.portHitboxes.set(port.id, hitMesh);
  }

  getPortWorldPosition(portId: string): THREE.Vector3 {
    const mesh = this.portMeshes.get(portId);
    if (!mesh) {
      // Fallback para a borda lateral
      return this.group.position.clone();
    }
    const worldPos = new THREE.Vector3();
    mesh.getWorldPosition(worldPos);
    return worldPos;
  }

  setSelected(selected: boolean): void {
    this.isSelected = selected;
    const borderMat = this.borderLine.material as THREE.LineBasicMaterial;
    borderMat.color.setHex(selected ? THEME.nodeBody.borderSelected : THEME.nodeBody.borderColor);
    this.group.position.z = selected ? 10 : 0;
  }

  setExecutionState(state: 'idle' | 'running' | 'completed'): void {
    const borderMat = this.borderLine.material as THREE.LineBasicMaterial;
    if (state === 'running') {
      // Destaque limpo de execução: cor contrastante e elevação em Z
      borderMat.color.setHex(0x10b981);
      this.group.position.z = 15;
    } else if (state === 'completed') {
      borderMat.color.setHex(0x059669);
      this.group.position.z = 5;
    } else {
      borderMat.color.setHex(this.isSelected ? THEME.nodeBody.borderSelected : THEME.nodeBody.borderColor);
      this.group.position.z = this.isSelected ? 10 : 0;
    }
  }

  updatePosition(x: number, y: number): void {
    this.group.position.x = x;
    this.group.position.y = y;
    this.nodeData.position.x = x;
    this.nodeData.position.y = y;
  }

  updateLabel(newLabel: string): void {
    this.nodeData.label = newLabel;
    this.group.remove(this.titleSprite);
    this.titleSprite.material.dispose();

    const width = THEME.nodeBody.width;
    const height = Math.max(THEME.nodeBody.height, 40 + Math.max(this.nodeData.inputs.length, this.nodeData.outputs.length) * 24);
    const headerHeight = 28;

    const titleCanvas = document.createElement('canvas');
    titleCanvas.width = 512;
    titleCanvas.height = 80;
    const tCtx = titleCanvas.getContext('2d')!;

    tCtx.fillStyle = '#F1F5F9';
    this.roundRect(tCtx, 8, 8, 496, 64, 12);
    tCtx.fill();

    tCtx.fillStyle = '#1E2538';
    tCtx.font = '800 28px Inter, system-ui, sans-serif';
    tCtx.textAlign = 'center';
    tCtx.textBaseline = 'middle';
    tCtx.fillText(newLabel, 256, 40);

    const titleTexture = new THREE.CanvasTexture(titleCanvas);
    titleTexture.minFilter = THREE.LinearFilter;
    const titleMat = new THREE.SpriteMaterial({ map: titleTexture, transparent: true });
    this.titleSprite = new THREE.Sprite(titleMat);
    this.titleSprite.scale.set(width - 24, 26, 1);
    this.titleSprite.position.set(0, height / 2 - headerHeight - 20, 4);
    this.group.add(this.titleSprite);
  }

  // --- Funções Auxiliares de Geometria ---

  private createRoundedRectShape(w: number, h: number, r: number): THREE.Shape {
    const shape = new THREE.Shape();
    const hw = w / 2;
    const hh = h / 2;

    shape.moveTo(-hw + r, -hh);
    shape.lineTo(hw - r, -hh);
    shape.absarc(hw - r, -hh + r, r, -Math.PI / 2, 0, false);
    shape.lineTo(hw, hh - r);
    shape.absarc(hw - r, hh - r, r, 0, Math.PI / 2, false);
    shape.lineTo(-hw + r, hh);
    shape.absarc(-hw + r, hh - r, r, Math.PI / 2, Math.PI, false);
    shape.lineTo(-hw, -hh + r);
    shape.absarc(-hw + r, -hh + r, r, Math.PI, Math.PI * 1.5, false);

    return shape;
  }

  private createHeaderShape(w: number, h: number, headerH: number, r: number): THREE.Shape {
    const shape = new THREE.Shape();
    const hw = w / 2;
    const hh = h / 2;
    const headerBottom = hh - headerH;

    shape.moveTo(-hw, headerBottom);
    shape.lineTo(hw, headerBottom);
    shape.lineTo(hw, hh - r);
    shape.absarc(hw - r, hh - r, r, 0, Math.PI / 2, false);
    shape.lineTo(-hw + r, hh);
    shape.absarc(-hw + r, hh - r, r, Math.PI / 2, Math.PI, false);
    shape.lineTo(-hw, headerBottom);

    return shape;
  }

  private roundRect(ctx: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number): void {
    ctx.beginPath();
    ctx.moveTo(x + r, y);
    ctx.lineTo(x + w - r, y);
    ctx.arcTo(x + w, y, x + w, y + r, r);
    ctx.lineTo(x + w, y + h - r);
    ctx.arcTo(x + w, y + h, x + w - r, y + h, r);
    ctx.lineTo(x + r, y + h);
    ctx.arcTo(x, y + h, x, y + h - r, r);
    ctx.lineTo(x, y + r);
    ctx.arcTo(x, y, x + r, y, r);
    ctx.closePath();
  }

  dispose(): void {
    this.bodyMesh.geometry.dispose();
    (this.bodyMesh.material as THREE.Material).dispose();
    this.borderLine.geometry.dispose();
    (this.borderLine.material as THREE.Material).dispose();
    this.headerMesh.geometry.dispose();
    (this.headerMesh.material as THREE.Material).dispose();
    this.titleSprite.material.dispose();
    this.badgeSprite.material.dispose();

    this.portMeshes.forEach(m => {
      m.geometry.dispose();
      (m.material as THREE.Material).dispose();
    });
    this.portHitboxes.forEach(m => {
      m.geometry.dispose();
      (m.material as THREE.Material).dispose();
    });
  }
}
