import * as THREE from 'three';
import type { Text } from 'troika-three-text';
import { INodeData, IPort } from '../core/types.ts';
import { THEME } from './Theme.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { createNodeLabel, updateNodeLabel, disposeNodeLabel, fitTextToLines } from './text/NodeText.ts';
import { createIconMesh, disposeIconMesh, LucideIconNode } from './icons/VectorIcons.ts';
import { NODE_ACTIONS, NODE_TYPE_ICONS, NodeActionType } from './NodeActions.ts';
import { Brain, Wrench, CircleCheck, CircleX, TriangleAlert, Timer } from 'lucide';

/**
 * Arestrix - Visualização de Nó no Canvas WebGL (Fase 15.2: Legibilidade Vetorial)
 *
 * - Toda tipografia é SDF vetorial (troika-three-text): nítida em qualquer zoom, sem distorção.
 * - Ícones são geometria vetorial (Lucide -> SVGLoader), não emojis rasterizados.
 * - Header limpo: rótulo de tipo em branco direto sobre a cor do tipo (sem pílula de fundo).
 * - Título sem caixa cinza; truncamento em até 2 linhas com texto completo no tooltip.
 * - Borda e anel de seleção em geometria real (LineLoop do WebGL é sempre 1px).
 * - Barra de ações com 5 botões identificáveis e metadados de tooltip.
 * - Nível de detalhe (LOD) por zoom.
 *
 * REGRA GLOBAL: zero sombras/glows em textos. Destaque por cor, peso ou opacidade.
 */

interface ITelemetryVisual {
  group: THREE.Group;
  fill: THREE.Mesh;
  ring: THREE.Mesh;
  icon: THREE.Mesh | null;
  label: Text;
}

const TELEMETRY_ICONS: Record<string, { name: string; icon: LucideIconNode }> = {
  thinking: { name: 'brain', icon: Brain as unknown as LucideIconNode },
  tool_start: { name: 'wrench', icon: Wrench as unknown as LucideIconNode },
  tool_ok: { name: 'circle-check', icon: CircleCheck as unknown as LucideIconNode },
  tool_fail: { name: 'circle-x', icon: CircleX as unknown as LucideIconNode },
  rate_limit: { name: 'triangle-alert', icon: TriangleAlert as unknown as LucideIconNode },
  status: { name: 'timer', icon: Timer as unknown as LucideIconNode }
};

export class NodeView {
  public group: THREE.Group;
  public nodeData: INodeData;
  public isSelected: boolean = false;

  private width: number;
  private height: number;
  private executionState: 'idle' | 'running' | 'completed' = 'idle';
  private detailVisible: boolean = true;

  private bodyMesh!: THREE.Mesh;
  private headerMesh!: THREE.Mesh;
  private borderMesh!: THREE.Mesh;
  private selectionMesh!: THREE.Mesh;
  private typeIcon: THREE.Mesh | null = null;
  private typeLabel!: Text;
  private titleLabel!: Text;
  private titleHitbox!: THREE.Mesh;
  private portLabels: Text[] = [];

  private actionBar: THREE.Group | null = null;
  private actionBarMeshes: THREE.Mesh[] = [];
  private actionIcons: Map<NodeActionType, THREE.Mesh> = new Map();
  private actionHoverBgs: Map<NodeActionType, THREE.Mesh> = new Map();
  private actionHitboxes: THREE.Mesh[] = [];
  private hoveredAction: NodeActionType | null = null;

  private telemetry: ITelemetryVisual | null = null;
  private telemetryHandler?: (ev: any) => void;

  private portMeshes: Map<string, THREE.Mesh> = new Map();
  private portHitboxes: Map<string, THREE.Mesh> = new Map();

  /** Altura do cartão em função do número de portas (fonte única para layout e enquadramento). */
  static computeHeight(nodeData: Pick<INodeData, 'inputs' | 'outputs'>): number {
    const b = THEME.nodeBody;
    const t = THEME.typography;
    const ports = Math.max(nodeData.inputs.length, nodeData.outputs.length, 1);
    const titleBlock = t.titleSize * 1.2 * b.titleMaxLines;
    const content = b.headerHeight + b.titleGap + titleBlock + 8 + ports * b.portSpacing + b.portZoneBottomPadding;
    return Math.max(b.height, Math.ceil(content));
  }

  constructor(nodeData: INodeData) {
    this.nodeData = nodeData;
    this.width = THEME.nodeBody.width;
    this.height = NodeView.computeHeight(nodeData);

    this.group = new THREE.Group();
    this.group.position.set(nodeData.position.x, nodeData.position.y, 0);
    this.group.userData = { nodeId: nodeData.id, isNode: true };

    this.buildNode();

    // Telemetria de Agente em Tempo Real (Fase 10)
    this.telemetryHandler = (ev: any) => this.handleTelemetry(ev);
    globalEventBus.on(`agent:telemetry:${nodeData.id}`, this.telemetryHandler);
  }

  private buildNode(): void {
    const width = this.width;
    const height = this.height;
    const b = THEME.nodeBody;
    const typeTheme = THEME.nodeTypes[this.nodeData.type] || THEME.nodeTypes.static;

    // 1. Corpo branco com cantos arredondados
    const bodyGeo = new THREE.ShapeGeometry(roundedRectShape(width, height, b.borderRadius), 12);
    this.bodyMesh = new THREE.Mesh(bodyGeo, new THREE.MeshBasicMaterial({ color: b.fillColor, side: THREE.DoubleSide }));
    this.bodyMesh.userData = { nodeId: this.nodeData.id, isBody: true };
    this.group.add(this.bodyMesh);

    // 2. Header na cor do tipo
    const headerGeo = new THREE.ShapeGeometry(this.createHeaderShape(width, height, b.headerHeight, b.borderRadius), 12);
    this.headerMesh = new THREE.Mesh(headerGeo, new THREE.MeshBasicMaterial({ color: typeTheme.color, side: THREE.DoubleSide }));
    this.headerMesh.position.z = 2;
    this.headerMesh.userData = { nodeId: this.nodeData.id, isBody: true };
    this.group.add(this.headerMesh);

    // 3. Borda real (anel geométrico) e anel externo de seleção/execução
    this.borderMesh = new THREE.Mesh(
      ringGeometry(width, height, b.borderRadius, b.borderWidth, 'inside'),
      new THREE.MeshBasicMaterial({ color: b.borderColor, side: THREE.DoubleSide })
    );
    this.borderMesh.position.z = 3;
    this.borderMesh.raycast = () => {};
    this.group.add(this.borderMesh);

    this.selectionMesh = new THREE.Mesh(
      ringGeometry(width, height, b.borderRadius, b.selectionWidth, 'outside'),
      new THREE.MeshBasicMaterial({ color: b.borderSelected, side: THREE.DoubleSide })
    );
    this.selectionMesh.position.z = 3;
    this.selectionMesh.visible = false;
    this.selectionMesh.raycast = () => {};
    this.group.add(this.selectionMesh);

    // 4. Tipografia vetorial (header + título)
    this.createTypography(typeTheme);

    // 5. Portas com rótulos legíveis
    this.createPorts();

    // 6. Barra de ações flutuante com ícones vetoriais
    this.createActionBar();
  }

  /**
   * Header limpo: ícone + rótulo de tipo em branco direto sobre a cor sólida (sem pílula).
   * Título em Slate 900, peso 700, alinhado à esquerda, até 2 linhas (sem caixa cinza).
   */
  private createTypography(typeTheme: { label: string }): void {
    const b = THEME.nodeBody;
    const t = THEME.typography;
    const left = -this.width / 2 + b.paddingX;
    const headerCenterY = this.height / 2 - b.headerHeight / 2;

    const typeIconDef = NODE_TYPE_ICONS[this.nodeData.type] ?? NODE_TYPE_ICONS.static;
    const iconSize = 16;
    this.typeIcon = createIconMesh(typeIconDef.name, typeIconDef.icon, iconSize, t.badgeColor);
    this.typeIcon.position.set(left + iconSize / 2, headerCenterY, 5);
    this.group.add(this.typeIcon);

    this.typeLabel = createNodeLabel({
      text: typeTheme.label,
      size: t.badgeSize,
      weight: t.badgeWeight as 700,
      color: t.badgeColor,
      letterSpacing: t.badgeLetterSpacing,
      anchorX: 'left',
      anchorY: 'middle',
      z: 5
    });
    this.typeLabel.position.set(left + iconSize + 8, headerCenterY, 5);
    this.group.add(this.typeLabel);

    const titleTop = this.height / 2 - b.headerHeight - b.titleGap;
    const titleMaxWidth = this.width - b.paddingX * 2;
    const fitted = fitTextToLines(this.nodeData.label, t.titleSize, titleMaxWidth, b.titleMaxLines);

    this.titleLabel = createNodeLabel({
      text: fitted.text,
      size: t.titleSize,
      weight: t.titleWeight as 700,
      color: t.titleColor,
      anchorX: 'left',
      anchorY: 'top',
      maxWidth: titleMaxWidth,
      lineHeight: 1.2,
      z: 5
    });
    this.titleLabel.position.set(left, titleTop, 5);
    this.group.add(this.titleLabel);

    // Hitbox invisível do título: mantém a seleção pelo corpo e expõe o texto completo se truncado
    const titleBlockH = t.titleSize * 1.2 * b.titleMaxLines;
    const titleHitGeo = new THREE.PlaneGeometry(titleMaxWidth, titleBlockH);
    this.titleHitbox = new THREE.Mesh(titleHitGeo, new THREE.MeshBasicMaterial({ visible: false }));
    this.titleHitbox.position.set(0, titleTop - titleBlockH / 2, 4);
    this.titleHitbox.userData = {
      nodeId: this.nodeData.id,
      isBody: true,
      tooltip: fitted.truncated ? this.nodeData.label : undefined
    };
    this.group.add(this.titleHitbox);
  }

  private portsTopY(): number {
    const b = THEME.nodeBody;
    const titleBlock = THEME.typography.titleSize * 1.2 * b.titleMaxLines;
    return this.height / 2 - b.headerHeight - b.titleGap - titleBlock - 8;
  }

  private createPorts(): void {
    const halfW = this.width / 2;
    const top = this.portsTopY();
    const spacing = THEME.nodeBody.portSpacing;

    this.nodeData.inputs.forEach((port, idx) => {
      this.createSinglePort(port, -halfW, top - (idx + 0.5) * spacing, true);
    });
    this.nodeData.outputs.forEach((port, idx) => {
      this.createSinglePort(port, halfW, top - (idx + 0.5) * spacing, false);
    });
  }

  private createSinglePort(port: IPort, px: number, py: number, isInput: boolean): void {
    const isConditionBranch = port.conditionBranchIndex !== undefined;

    let portColor = isInput ? THEME.port.inputColor : THEME.port.outputColor;
    if (isConditionBranch) {
      portColor = port.conditionBranchIndex === 0 ? 0x10b981 : 0xf59e0b;
    }

    // 1. Círculo visual da porta
    const portMesh = new THREE.Mesh(
      new THREE.CircleGeometry(THEME.port.radius, 24),
      new THREE.MeshBasicMaterial({ color: portColor })
    );
    portMesh.position.set(px, py, 4);
    portMesh.userData = { isPort: true, portId: port.id, nodeId: this.nodeData.id, direction: port.direction };
    this.group.add(portMesh);
    this.portMeshes.set(port.id, portMesh);

    // 2. Hitbox invisível expandida + metadados de tooltip
    const directionLabel = isInput ? 'Entrada' : 'Saída';
    const hitMesh = new THREE.Mesh(
      new THREE.CircleGeometry(THEME.port.hitboxRadius, 12),
      new THREE.MeshBasicMaterial({ visible: false })
    );
    hitMesh.position.set(px, py, 4);
    hitMesh.userData = {
      isPortHitbox: true,
      portId: port.id,
      nodeId: this.nodeData.id,
      direction: port.direction,
      tooltip: `${port.name || (isInput ? 'in' : 'out')} · ${directionLabel}`,
      tooltipDescription: port.dataType ? `Tipo: ${port.dataType}` : undefined
    };
    this.group.add(hitMesh);
    this.portHitboxes.set(port.id, hitMesh);

    // 3. Rótulo vetorial da porta
    this.createPortLabel(port, px, py, isInput);
  }

  private createPortLabel(port: IPort, px: number, py: number, isInput: boolean): void {
    const t = THEME.typography;
    const isConditionBranch = port.conditionBranchIndex !== undefined;

    let text = (port.name || (isInput ? 'in' : 'out')).toUpperCase();
    let color: string = t.portColor;

    if (!isInput && isConditionBranch) {
      if (port.conditionBranchIndex === 0) {
        text = 'TRUE (0)';
        color = '#047857'; // Emerald 700
      } else {
        text = 'FALSE (1)';
        color = '#B45309'; // Amber 700
      }
    }

    const offset = THEME.port.labelOffset;
    const label = createNodeLabel({
      text,
      size: t.portSize,
      weight: t.portWeight as 600,
      color,
      anchorX: isInput ? 'left' : 'right',
      anchorY: 'middle',
      z: 5
    });
    label.position.set(isInput ? px + offset : px - offset, py, 5);
    label.visible = this.detailVisible;
    this.group.add(label);
    this.portLabels.push(label);
  }

  /**
   * Barra de ações flutuante: pílula branca em geometria real com 5 ícones vetoriais
   * identificáveis, fundo de hover e hitboxes com metadados de tooltip.
   */
  private createActionBar(): void {
    const a = THEME.actionBar;
    const count = NODE_ACTIONS.length;
    const barW = count * a.buttonSize + (count - 1) * a.buttonGap + a.padding * 2;
    const barH = a.buttonSize + a.padding * 2;

    const bar = new THREE.Group();
    bar.position.set(this.width / 2 - barW / 2, this.height / 2 + a.offsetY + barH / 2, 6);
    bar.userData = { isActionBar: true, nodeId: this.nodeData.id };

    const fill = new THREE.Mesh(
      new THREE.ShapeGeometry(roundedRectShape(barW, barH, barH / 2), 16),
      new THREE.MeshBasicMaterial({ color: a.fill, side: THREE.DoubleSide })
    );
    fill.raycast = () => {};
    bar.add(fill);
    this.actionBarMeshes.push(fill);

    const ring = new THREE.Mesh(
      ringGeometry(barW, barH, barH / 2, 1.25, 'inside'),
      new THREE.MeshBasicMaterial({ color: a.border, side: THREE.DoubleSide })
    );
    ring.position.z = 0.5;
    ring.raycast = () => {};
    bar.add(ring);
    this.actionBarMeshes.push(ring);

    NODE_ACTIONS.forEach((action, i) => {
      const cx = -barW / 2 + a.padding + a.buttonSize / 2 + i * (a.buttonSize + a.buttonGap);

      const hoverBg = new THREE.Mesh(
        new THREE.CircleGeometry(a.buttonSize / 2, 24),
        new THREE.MeshBasicMaterial({ color: a.hoverFill })
      );
      hoverBg.position.set(cx, 0, 1);
      hoverBg.visible = false;
      hoverBg.raycast = () => {};
      bar.add(hoverBg);
      this.actionHoverBgs.set(action.type, hoverBg);

      const icon = createIconMesh(action.iconName, action.icon, a.iconSize, action.color);
      icon.position.set(cx, 0, 2);
      bar.add(icon);
      this.actionIcons.set(action.type, icon);

      const hit = new THREE.Mesh(
        new THREE.CircleGeometry(a.buttonSize / 2, 16),
        new THREE.MeshBasicMaterial({ visible: false })
      );
      hit.position.set(cx, 0, 3);
      hit.userData = {
        isActionHitbox: true,
        actionType: action.type,
        nodeId: this.nodeData.id,
        tooltip: action.label,
        tooltipDescription: action.description,
        ariaLabel: action.label
      };
      bar.add(hit);
      this.actionHitboxes.push(hit);
    });

    bar.visible = this.detailVisible;
    this.actionBar = bar;
    this.group.add(bar);
  }

  /**
   * Destaque do botão sob o cursor: fundo Indigo 50 limpo (sem glow).
   */
  setHoveredAction(type: NodeActionType | null): void {
    if (this.hoveredAction === type) return;
    if (this.hoveredAction) {
      const prev = this.actionHoverBgs.get(this.hoveredAction);
      if (prev) prev.visible = false;
    }
    this.hoveredAction = type;
    if (type) {
      const bg = this.actionHoverBgs.get(type);
      if (bg) bg.visible = true;
    }
  }

  /**
   * Nível de detalhe por zoom: abaixo do limiar oculta rótulos de portas e a barra de ações
   * (que também deixa de receber raycast via traverseVisible no InteractionManager).
   */
  setDetailLevel(zoom: number): void {
    const visible = zoom >= THEME.lod.detailZoomThreshold;
    if (visible === this.detailVisible) return;
    this.detailVisible = visible;
    this.portLabels.forEach(l => (l.visible = visible));
    if (this.actionBar) this.actionBar.visible = visible;
    if (!visible) this.setHoveredAction(null);
  }

  isDetailVisible(): boolean {
    return this.detailVisible;
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
    this.refreshOutline();
  }

  setExecutionState(state: 'idle' | 'running' | 'completed'): void {
    this.executionState = state;
    this.refreshOutline();
  }

  private refreshOutline(): void {
    const selMat = this.selectionMesh.material as THREE.MeshBasicMaterial;
    const borderMat = this.borderMesh.material as THREE.MeshBasicMaterial;

    if (this.executionState === 'running') {
      selMat.color.setHex(0x10b981);
      borderMat.color.setHex(0x10b981);
      this.selectionMesh.visible = true;
      this.group.position.z = 15;
    } else if (this.executionState === 'completed') {
      selMat.color.setHex(0x059669);
      borderMat.color.setHex(0x059669);
      this.selectionMesh.visible = true;
      this.group.position.z = 5;
    } else {
      selMat.color.setHex(THEME.nodeBody.borderSelected);
      borderMat.color.setHex(this.isSelected ? THEME.nodeBody.borderSelected : THEME.nodeBody.borderColor);
      this.selectionMesh.visible = this.isSelected;
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
    const b = THEME.nodeBody;
    const fitted = fitTextToLines(newLabel, THEME.typography.titleSize, this.width - b.paddingX * 2, b.titleMaxLines);
    updateNodeLabel(this.titleLabel, fitted.text);
    this.titleHitbox.userData.tooltip = fitted.truncated ? newLabel : undefined;
  }

  // --- Telemetria (badge vetorial abaixo do nó) ---

  private handleTelemetry(ev: any): void {
    const p = ev.payload || {};
    let text = '';
    let iconKey = 'status';
    let bg = '#F8FAFC';
    let border = '#94A3B8';
    let textCol = '#334155';

    if (ev.type === 'thinking') {
      const kb = p.thinkingChars ? (p.thinkingChars / 1024).toFixed(1) : '0';
      text = `Raciocinando (${kb} KB)`;
      iconKey = 'thinking';
      bg = '#EEF2FF';
      border = '#6366F1';
      textCol = '#4338CA';
    } else if (ev.type === 'tool_start') {
      text = `Executando ${p.toolName || 'tool'}`;
      iconKey = 'tool_start';
      bg = '#F0F9FF';
      border = '#0284C7';
      textCol = '#0369A1';
    } else if (ev.type === 'tool_end') {
      const ok = p.toolSuccess !== false;
      text = ok ? `${p.toolName || 'tool'} concluída` : `${p.toolName || 'tool'} falhou`;
      iconKey = ok ? 'tool_ok' : 'tool_fail';
      bg = ok ? '#ECFDF5' : '#FEF2F2';
      border = ok ? '#10B981' : '#EF4444';
      textCol = ok ? '#047857' : '#B91C1C';
    } else if (ev.type === 'rate_limit') {
      text = `Rate limit 429 (${p.retrySeconds || 15}s)`;
      iconKey = 'rate_limit';
      bg = '#FFFBEB';
      border = '#F59E0B';
      textCol = '#B45309';
    } else if (ev.type === 'status') {
      text = `${p.status || 'ativo'}`;
      iconKey = 'status';
    } else {
      return;
    }

    this.renderTelemetryBadge(text, iconKey, bg, border, textCol);
  }

  private renderTelemetryBadge(text: string, iconKey: string, bg: string, border: string, textCol: string): void {
    const t = THEME.typography;
    const badgeW = this.width - 20;
    const badgeH = 26;
    const iconSize = 14;
    const left = -badgeW / 2 + 12;

    if (!this.telemetry) {
      const group = new THREE.Group();
      group.position.set(0, -this.height / 2 - badgeH / 2 - 8, 5);
      group.userData = { nodeId: this.nodeData.id, isTelemetry: true };

      const fill = new THREE.Mesh(
        new THREE.ShapeGeometry(roundedRectShape(badgeW, badgeH, badgeH / 2), 16),
        new THREE.MeshBasicMaterial({ color: bg, side: THREE.DoubleSide })
      );
      fill.raycast = () => {};
      group.add(fill);

      const ring = new THREE.Mesh(
        ringGeometry(badgeW, badgeH, badgeH / 2, 1.25, 'inside'),
        new THREE.MeshBasicMaterial({ color: border, side: THREE.DoubleSide })
      );
      ring.position.z = 0.5;
      ring.raycast = () => {};
      group.add(ring);

      const label = createNodeLabel({
        text,
        size: t.telemetrySize,
        weight: t.telemetryWeight as 600,
        color: textCol,
        anchorX: 'left',
        anchorY: 'middle',
        z: 1
      });
      label.position.set(left + iconSize + 8, 0, 1);
      group.add(label);

      this.telemetry = { group, fill, ring, icon: null, label };
      this.group.add(group);
    } else {
      (this.telemetry.fill.material as THREE.MeshBasicMaterial).color.set(bg);
      (this.telemetry.ring.material as THREE.MeshBasicMaterial).color.set(border);
      updateNodeLabel(this.telemetry.label, text, textCol);
    }

    // Ícone vetorial do estado
    if (this.telemetry.icon) {
      this.telemetry.group.remove(this.telemetry.icon);
      disposeIconMesh(this.telemetry.icon);
    }
    const iconDef = TELEMETRY_ICONS[iconKey] ?? TELEMETRY_ICONS.status;
    const icon = createIconMesh(iconDef.name, iconDef.icon, iconSize, textCol);
    icon.position.set(left + iconSize / 2, 0, 1);
    this.telemetry.group.add(icon);
    this.telemetry.icon = icon;
  }

  // --- Geometria auxiliar ---

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

  dispose(): void {
    if (this.telemetryHandler) {
      globalEventBus.off(`agent:telemetry:${this.nodeData.id}`, this.telemetryHandler);
    }

    disposeNodeLabel(this.typeLabel);
    disposeNodeLabel(this.titleLabel);
    this.portLabels.forEach(l => disposeNodeLabel(l));
    this.portLabels = [];

    disposeIconMesh(this.typeIcon);
    this.actionIcons.forEach(icon => disposeIconMesh(icon));
    this.actionIcons.clear();

    if (this.telemetry) {
      disposeNodeLabel(this.telemetry.label);
      disposeIconMesh(this.telemetry.icon);
      [this.telemetry.fill, this.telemetry.ring].forEach(disposeMesh);
      this.telemetry = null;
    }

    [this.bodyMesh, this.headerMesh, this.borderMesh, this.selectionMesh, this.titleHitbox].forEach(disposeMesh);
    this.actionBarMeshes.forEach(disposeMesh);
    this.actionHoverBgs.forEach(disposeMesh);
    this.actionHitboxes.forEach(disposeMesh);
    this.portMeshes.forEach(disposeMesh);
    this.portHitboxes.forEach(disposeMesh);

    this.actionBarMeshes = [];
    this.actionHoverBgs.clear();
    this.actionHitboxes = [];
  }
}

// --- Funções de geometria compartilhadas ---

function disposeMesh(mesh: THREE.Mesh): void {
  mesh.geometry.dispose();
  (mesh.material as THREE.Material).dispose();
}

function traceRoundedRect(path: THREE.Path, w: number, h: number, r: number): void {
  const hw = w / 2;
  const hh = h / 2;
  const rr = Math.max(0, Math.min(r, hw, hh));
  path.moveTo(-hw + rr, -hh);
  path.lineTo(hw - rr, -hh);
  path.absarc(hw - rr, -hh + rr, rr, -Math.PI / 2, 0, false);
  path.lineTo(hw, hh - rr);
  path.absarc(hw - rr, hh - rr, rr, 0, Math.PI / 2, false);
  path.lineTo(-hw + rr, hh);
  path.absarc(-hw + rr, hh - rr, rr, Math.PI / 2, Math.PI, false);
  path.lineTo(-hw, -hh + rr);
  path.absarc(-hw + rr, -hh + rr, rr, Math.PI, Math.PI * 1.5, false);
}

export function roundedRectShape(w: number, h: number, r: number): THREE.Shape {
  const shape = new THREE.Shape();
  traceRoundedRect(shape, w, h, r);
  return shape;
}

/**
 * Anel de retângulo arredondado com espessura real em unidades de mundo.
 * 'inside': a borda ocupa a faixa interna do retângulo w×h.
 * 'outside': a borda envolve o retângulo w×h pelo lado de fora.
 */
export function ringGeometry(w: number, h: number, r: number, thickness: number, mode: 'inside' | 'outside'): THREE.ShapeGeometry {
  const outerW = mode === 'outside' ? w + thickness * 2 : w;
  const outerH = mode === 'outside' ? h + thickness * 2 : h;
  const outerR = mode === 'outside' ? r + thickness : r;
  const innerW = mode === 'outside' ? w : w - thickness * 2;
  const innerH = mode === 'outside' ? h : h - thickness * 2;
  const innerR = mode === 'outside' ? r : Math.max(0, r - thickness);

  const shape = roundedRectShape(outerW, outerH, outerR);
  const hole = new THREE.Path();
  traceRoundedRect(hole, innerW, innerH, innerR);
  shape.holes.push(hole);
  return new THREE.ShapeGeometry(shape, 16);
}
