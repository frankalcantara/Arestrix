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
  private portLabelSprites: THREE.Sprite[] = [];

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
    const height = Math.max(THEME.nodeBody.height, 60 + Math.max(this.nodeData.inputs.length, this.nodeData.outputs.length) * 28);
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
    const headerHeight = THEME.nodeBody.headerHeight;
    const headerShape = this.createHeaderShape(width, height, headerHeight, radius);
    const headerGeo = new THREE.ShapeGeometry(headerShape);
    const headerMat = new THREE.MeshBasicMaterial({
      color: typeTheme.color,
      side: THREE.DoubleSide
    });
    this.headerMesh = new THREE.Mesh(headerGeo, headerMat);
    this.headerMesh.position.z = 2;
    this.headerMesh.userData = { nodeId: this.nodeData.id, isBody: true };
    this.group.add(this.headerMesh);

    // 4. Badge e Tipografia Nítida em Alto Contraste (SEM SOMBRAS NEM GLOW - REGRA DO USUÁRIO)
    this.createTypography(width, height, headerHeight, typeTheme);

    // 5. Portas de Entrada e Saída com Rótulos Visíveis
    this.createPorts(width, height, headerHeight);
  }

  /**
   * Tipografia nítida atendendo à diretriz:
   * "NUNCA utilizar efeitos de sombra ou brilho em elementos de texto...
   * Alternativas limpas: cor de contraste, peso tipográfico 700/800, badge de fundo limpo."
   */
  private createTypography(width: number, height: number, headerHeight: number, typeTheme: any): void {
    this.badgeSprite = this.createBadgeSprite(height, headerHeight, typeTheme);
    this.group.add(this.badgeSprite);

    this.titleSprite = this.createTitleSprite(width, height, headerHeight, this.nodeData.label);
    this.group.add(this.titleSprite);
  }

  private createBadgeSprite(height: number, headerHeight: number, typeTheme: any): THREE.Sprite {
    const canvas = document.createElement('canvas');
    canvas.width = 512;
    canvas.height = 80;
    const ctx = canvas.getContext('2d')!;
    ctx.imageSmoothingEnabled = true;

    // Fundo da Pílula / Badge limpo
    ctx.fillStyle = `#${typeTheme.badgeBg.toString(16).padStart(6, '0')}`;
    this.roundRect(ctx, 8, 8, 496, 64, 16);
    ctx.fill();

    // Texto do Badge perfeitamente nítido e em alto peso (SEM SOMBRAS)
    ctx.fillStyle = '#FFFFFF';
    ctx.font = '800 32px Inter, system-ui, -apple-system, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(typeTheme.label, 256, 40);

    const texture = new THREE.CanvasTexture(canvas);
    texture.generateMipmaps = true;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.magFilter = THREE.LinearFilter;

    const mat = new THREE.SpriteMaterial({ map: texture, transparent: true });
    const sprite = new THREE.Sprite(mat);
    sprite.scale.set(170, 26, 1);
    sprite.position.set(0, height / 2 - headerHeight / 2, 4);
    sprite.userData = { nodeId: this.nodeData.id, isBody: true };
    return sprite;
  }

  private createTitleSprite(width: number, height: number, headerHeight: number, label: string): THREE.Sprite {
    const canvas = document.createElement('canvas');
    canvas.width = 1024;
    canvas.height = 160;
    const ctx = canvas.getContext('2d')!;
    ctx.imageSmoothingEnabled = true;

    // Fundo de pílula suave e limpo (Slate 100 com borda suave Slate 200)
    ctx.fillStyle = '#F1F5F9';
    this.roundRect(ctx, 8, 8, 1008, 144, 20);
    ctx.fill();

    ctx.strokeStyle = '#CBD5E1';
    ctx.lineWidth = 3;
    ctx.stroke();

    // Medição e escala dinâmica para que títulos longos fiquem grandes e sem corte
    let fontSize = 46;
    ctx.font = `800 ${fontSize}px Inter, system-ui, -apple-system, sans-serif`;
    const textW = ctx.measureText(label).width;
    const maxTextW = 940;
    if (textW > maxTextW) {
      fontSize = Math.max(26, Math.floor(fontSize * (maxTextW / textW)));
      ctx.font = `800 ${fontSize}px Inter, system-ui, -apple-system, sans-serif`;
    }

    // Texto com peso tipográfico 800 e alto contraste (#0F172A) - ZERO SOMBRAS
    ctx.fillStyle = '#0F172A';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(label, 512, 80);

    const texture = new THREE.CanvasTexture(canvas);
    texture.generateMipmaps = true;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.magFilter = THREE.LinearFilter;

    const mat = new THREE.SpriteMaterial({ map: texture, transparent: true });
    const sprite = new THREE.Sprite(mat);
    sprite.scale.set(width - 24, 38, 1);
    sprite.position.set(0, height / 2 - headerHeight - 24, 4);
    sprite.userData = { nodeId: this.nodeData.id, isBody: true };
    return sprite;
  }

  private createPorts(width: number, height: number, headerHeight: number): void {
    const halfW = width / 2;
    const halfH = height / 2;

    // Zona de portas abaixo do título
    const titleBottom = (halfH - headerHeight - 24) - 20;
    const cardBottom = -halfH + 12;
    const portZoneHeight = titleBottom - cardBottom;

    // Portas de Entrada (Esquerda)
    const inCount = this.nodeData.inputs.length;
    this.nodeData.inputs.forEach((port, idx) => {
      const spacing = portZoneHeight / (inCount + 1);
      const py = titleBottom - (idx + 1) * spacing;
      this.createSinglePort(port, -halfW, py, true);
    });

    // Portas de Saída (Direita)
    const outCount = this.nodeData.outputs.length;
    this.nodeData.outputs.forEach((port, idx) => {
      const spacing = portZoneHeight / (outCount + 1);
      const py = titleBottom - (idx + 1) * spacing;
      this.createSinglePort(port, halfW, py, false);
    });
  }

  private createSinglePort(port: IPort, px: number, py: number, isInput: boolean): void {
    const isConditionBranch = port.conditionBranchIndex !== undefined;

    let portColor = isInput ? THEME.port.inputColor : THEME.port.outputColor;
    if (isConditionBranch) {
      portColor = port.conditionBranchIndex === 0 ? 0x10b981 : 0xf59e0b;
    }

    // 1. Círculo visual da porta
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

    // 2. Hitbox invisível expandida para clique/conexão fácil
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

    // 3. Rótulo legível da porta (IN / OUT / TRUE / FALSE) em alto contraste
    this.createPortLabel(port, px, py, isInput);
  }

  private createPortLabel(port: IPort, px: number, py: number, isInput: boolean): void {
    const isConditionBranch = port.conditionBranchIndex !== undefined;
    const canvas = document.createElement('canvas');
    canvas.width = 220;
    canvas.height = 60;
    const ctx = canvas.getContext('2d')!;
    ctx.imageSmoothingEnabled = true;

    let text = isInput ? (port.name ? port.name.toUpperCase() : 'IN') : (port.name ? port.name.toUpperCase() : 'OUT');
    let textColor = '#64748B'; // Slate 500
    let fontSize = 28;

    if (!isInput && isConditionBranch) {
      if (port.conditionBranchIndex === 0) {
        text = 'TRUE (0)';
        textColor = '#059669'; // Emerald 600
        fontSize = 26;
      } else {
        text = 'FALSE (1)';
        textColor = '#D97706'; // Amber 600
        fontSize = 26;
      }
    }

    ctx.font = `800 ${fontSize}px Inter, system-ui, -apple-system, sans-serif`;
    ctx.fillStyle = textColor;
    ctx.textBaseline = 'middle';

    const spriteWidth = isInput ? 44 : 58;
    const spriteHeight = 15;
    const spriteX = isInput ? px + 36 : px - 44;

    if (isInput) {
      ctx.textAlign = 'left';
      ctx.fillText(text, 6, 30);
    } else {
      ctx.textAlign = 'right';
      ctx.fillText(text, 214, 30);
    }

    const texture = new THREE.CanvasTexture(canvas);
    texture.generateMipmaps = true;
    texture.minFilter = THREE.LinearMipmapLinearFilter;
    texture.magFilter = THREE.LinearFilter;

    const mat = new THREE.SpriteMaterial({ map: texture, transparent: true });
    const labelSprite = new THREE.Sprite(mat);
    labelSprite.scale.set(spriteWidth, spriteHeight, 1);
    labelSprite.position.set(spriteX, py, 4);
    labelSprite.userData = { nodeId: this.nodeData.id, isBody: true };

    this.group.add(labelSprite);
    this.portLabelSprites.push(labelSprite);
  }

  getPortWorldPosition(portId: string): THREE.Vector3 {
    const mesh = this.portMeshes.get(portId);
    if (!mesh) {
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
    this.titleSprite.material.map?.dispose();
    this.titleSprite.material.dispose();

    const width = THEME.nodeBody.width;
    const height = Math.max(THEME.nodeBody.height, 60 + Math.max(this.nodeData.inputs.length, this.nodeData.outputs.length) * 28);
    const headerHeight = THEME.nodeBody.headerHeight;

    this.titleSprite = this.createTitleSprite(width, height, headerHeight, newLabel);
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
    
    if (this.titleSprite) {
      this.titleSprite.material.map?.dispose();
      this.titleSprite.material.dispose();
    }
    if (this.badgeSprite) {
      this.badgeSprite.material.map?.dispose();
      this.badgeSprite.material.dispose();
    }

    this.portLabelSprites.forEach(s => {
      s.material.map?.dispose();
      s.material.dispose();
    });
    this.portLabelSprites = [];

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
