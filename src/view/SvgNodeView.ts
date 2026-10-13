import { INodeData, IPort } from '../core/types.ts';
import { THEME, getContrastTextColor } from './Theme.ts';
import { globalEventBus, EventBus } from '../core/EventBus.ts';
import { NODE_ACTIONS, NODE_TYPE_ICONS, NodeActionType } from './NodeActions.ts';
import { createLucideSvgElement, LucideIconNode } from './icons/VectorIcons.ts';
import { fitTextToLines } from './text/NodeText.ts';
import { CanvasTooltip } from '../ui/CanvasTooltip.ts';
import { Brain, Wrench, CircleCheck, CircleX, TriangleAlert, Timer } from 'lucide';

/**
 * Arestrix - Visualização Vetorial SVG de Vértice (Nó)
 *
 * Transforma os vértices em elementos SVG puros e dinâmicos:
 * - 100% vetorial: texto nativo DOM com subpixel rendering nítido em qualquer nível de zoom.
 * - Sem pixelização ou borrão característico de raster/BMP.
 * - Contraste WCAG 2.1 garantido entre a cor de fundo da barra de título e a cor da fonte.
 * - Barra de ações rápida com 5 ícones Lucide vetoriais e tooltips.
 * - Borda e anel de seleção/execução em traços vetoriais precisos.
 *
 * REGRA GLOBAL: zero sombras/glows em textos (text-shadow: none !important).
 */

const SVG_NS = 'http://www.w3.org/2000/svg';

const TELEMETRY_ICONS: Record<string, { name: string; icon: LucideIconNode }> = {
  thinking: { name: 'brain', icon: Brain as unknown as LucideIconNode },
  tool_start: { name: 'wrench', icon: Wrench as unknown as LucideIconNode },
  tool_ok: { name: 'circle-check', icon: CircleCheck as unknown as LucideIconNode },
  tool_fail: { name: 'circle-x', icon: CircleX as unknown as LucideIconNode },
  rate_limit: { name: 'triangle-alert', icon: TriangleAlert as unknown as LucideIconNode },
  status: { name: 'timer', icon: Timer as unknown as LucideIconNode }
};

export class SvgNodeView {
  public nodeData: INodeData;
  public group: SVGGElement;
  public isSelected: boolean = false;

  private width: number;
  private height: number;
  private executionState: 'idle' | 'running' | 'completed' = 'idle';
  private detailVisible: boolean = true;

  private selectionRing!: SVGRectElement;
  private bodyRect!: SVGRectElement;
  private headerPath!: SVGPathElement;
  private headerIconGroup!: SVGGElement;
  private headerLabel!: SVGTextElement;
  private titleGroup!: SVGGElement;

  private portElements: Map<string, { group: SVGGElement; circle: SVGCircleElement; label: SVGTextElement; hitbox: SVGCircleElement }> = new Map();
  private actionBarGroup: SVGGElement | null = null;
  private actionHoverBgs: Map<NodeActionType, SVGCircleElement> = new Map();
  private telemetryGroup: SVGGElement | null = null;
  private telemetryHandler?: (ev: any) => void;

  private tooltip: CanvasTooltip;
  private bus: EventBus;

  constructor(nodeData: INodeData, parentSvgGroup: SVGGElement, tooltip: CanvasTooltip, bus: EventBus = globalEventBus) {
    this.nodeData = nodeData;
    this.tooltip = tooltip;
    this.bus = bus;

    this.width = THEME.nodeBody.width;
    this.height = SvgNodeView.computeHeight(nodeData);

    this.group = document.createElementNS(SVG_NS, 'g');
    this.group.setAttribute('class', 'arestrix-svg-vertex');
    this.group.setAttribute('data-node-id', nodeData.id);
    this.updatePosition(nodeData.position.x, nodeData.position.y);

    this.buildSvgNode();
    parentSvgGroup.appendChild(this.group);

    // Telemetria em tempo real
    this.telemetryHandler = (ev: any) => this.handleTelemetry(ev);
    this.bus.on(`agent:telemetry:${nodeData.id}`, this.telemetryHandler);
  }

  static computeHeight(nodeData: Pick<INodeData, 'inputs' | 'outputs'>): number {
    const b = THEME.nodeBody;
    const t = THEME.typography;
    const ports = Math.max(nodeData.inputs.length, nodeData.outputs.length, 1);
    const titleBlock = t.titleSize * 1.25 * b.titleMaxLines;
    const content = b.headerHeight + b.titleGap + titleBlock + 12 + ports * b.portSpacing + b.portZoneBottomPadding;
    return Math.max(b.height, Math.ceil(content));
  }

  private buildSvgNode(): void {
    const w = this.width;
    const h = this.height;
    const b = THEME.nodeBody;
    const r = b.borderRadius;
    const typeTheme = THEME.nodeTypes[this.nodeData.type] || THEME.nodeTypes.static;

    // 1. Anel externo de seleção / execução
    this.selectionRing = document.createElementNS(SVG_NS, 'rect');
    this.selectionRing.setAttribute('class', 'vertex-selection-ring');
    this.selectionRing.setAttribute('x', String(-w / 2 - b.selectionWidth));
    this.selectionRing.setAttribute('y', String(-h / 2 - b.selectionWidth));
    this.selectionRing.setAttribute('width', String(w + b.selectionWidth * 2));
    this.selectionRing.setAttribute('height', String(h + b.selectionWidth * 2));
    this.selectionRing.setAttribute('rx', String(r + b.selectionWidth));
    this.selectionRing.setAttribute('ry', String(r + b.selectionWidth));
    this.selectionRing.setAttribute('fill', 'none');
    this.selectionRing.setAttribute('stroke', '#0f6cbd');
    this.selectionRing.setAttribute('stroke-width', String(b.selectionWidth));
    this.selectionRing.style.display = 'none';
    this.group.appendChild(this.selectionRing);

    // 2. Corpo do vértice (cartão branco com cantos arredondados)
    this.bodyRect = document.createElementNS(SVG_NS, 'rect');
    this.bodyRect.setAttribute('class', 'vertex-body');
    this.bodyRect.setAttribute('x', String(-w / 2));
    this.bodyRect.setAttribute('y', String(-h / 2));
    this.bodyRect.setAttribute('width', String(w));
    this.bodyRect.setAttribute('height', String(h));
    this.bodyRect.setAttribute('rx', String(r));
    this.bodyRect.setAttribute('ry', String(r));
    this.bodyRect.setAttribute('fill', '#FFFFFF');
    this.bodyRect.setAttribute('stroke', '#CBD5E1');
    this.bodyRect.setAttribute('stroke-width', String(b.borderWidth));
    this.group.appendChild(this.bodyRect);

    // 3. Barra de título (Header com cantos superiores arredondados na cor do tipo)
    this.headerPath = document.createElementNS(SVG_NS, 'path');
    this.headerPath.setAttribute('class', 'vertex-header');
    const headerD = this.createHeaderPathD(w, h, b.headerHeight, r);
    this.headerPath.setAttribute('d', headerD);
    const headerHex = typeof typeTheme.color === 'number' 
      ? '#' + typeTheme.color.toString(16).padStart(6, '0') 
      : typeTheme.color;
    this.headerPath.setAttribute('fill', headerHex);
    this.group.appendChild(this.headerPath);

    // Eventos de seleção e arraste pelo corpo ou cabeçalho do vértice
    const onNodePointerDown = (e: PointerEvent) => {
      if (e.button !== 0 || e.altKey) return;
      e.stopPropagation();
      this.bus.emit('node:selected', this.nodeData.id);
      this.bus.emit('node:start-drag', { nodeId: this.nodeData.id, clientX: e.clientX, clientY: e.clientY });
    };
    this.bodyRect.addEventListener('pointerdown', onNodePointerDown);
    this.headerPath.addEventListener('pointerdown', onNodePointerDown);

    // Menu de contexto (botão direito) do vértice
    this.group.addEventListener('contextmenu', (e: MouseEvent) => {
      e.preventDefault();
      e.stopPropagation();
      this.tooltip.hide();
      this.bus.emit('node:context-menu', { nodeId: this.nodeData.id, clientX: e.clientX, clientY: e.clientY });
    });

    // 4. Tipografia do Header: Contraste Garantido WCAG 2.1
    this.buildHeaderTypography(typeTheme, headerHex);

    // 5. Título do Vértice
    this.buildTitleTypography();

    // 6. Portas de Entrada e Saída
    this.buildPorts();

    // 7. Barra de Ações Rápidas Flutuante
    this.buildActionBar();
  }

  private createHeaderPathD(w: number, h: number, headerH: number, r: number): string {
    const hw = w / 2;
    const hh = h / 2;
    const top = -hh;
    const bottom = -hh + headerH;
    return `M ${-hw + r} ${top} ` +
           `L ${hw - r} ${top} ` +
           `A ${r} ${r} 0 0 1 ${hw} ${top + r} ` +
           `L ${hw} ${bottom} ` +
           `L ${-hw} ${bottom} ` +
           `L ${-hw} ${top + r} ` +
           `A ${r} ${r} 0 0 1 ${-hw + r} ${top} Z`;
  }

  private buildHeaderTypography(typeTheme: { label: string; color: number | string }, headerHex: string): void {
    const b = THEME.nodeBody;
    const t = THEME.typography;
    const left = -this.width / 2 + b.paddingX;
    const headerCenterY = -this.height / 2 + b.headerHeight / 2;

    // Garantia estrita de contraste WCAG: calcula se a fonte deve ser branca ou escura
    const contrastColor = getContrastTextColor(headerHex);

    // Ícone do tipo de tarefa
    const typeIconDef = NODE_TYPE_ICONS[this.nodeData.type] ?? NODE_TYPE_ICONS.static;
    const iconSize = 16;
    this.headerIconGroup = createLucideSvgElement(typeIconDef.icon, iconSize, contrastColor, 2.2);
    this.headerIconGroup.setAttribute(
      'transform',
      `translate(${left}, ${headerCenterY - iconSize / 2}) scale(${iconSize / 24})`
    );
    this.group.appendChild(this.headerIconGroup);

    // Rótulo do tipo no header
    this.headerLabel = document.createElementNS(SVG_NS, 'text');
    this.headerLabel.setAttribute('class', 'vertex-header-label');
    this.headerLabel.setAttribute('x', String(left + iconSize + 8));
    this.headerLabel.setAttribute('y', String(headerCenterY));
    this.headerLabel.setAttribute('fill', contrastColor);
    this.headerLabel.setAttribute('font-size', String(t.badgeSize));
    this.headerLabel.setAttribute('font-weight', String(t.badgeWeight));
    this.headerLabel.setAttribute('letter-spacing', '0.04em');
    this.headerLabel.setAttribute('dominant-baseline', 'central');
    this.headerLabel.style.textShadow = 'none';
    this.headerLabel.style.fontFamily = t.fontFamily;
    this.headerLabel.textContent = typeTheme.label;
    this.group.appendChild(this.headerLabel);
  }

  private buildTitleTypography(): void {
    const b = THEME.nodeBody;
    const t = THEME.typography;
    const left = -this.width / 2 + b.paddingX;
    const titleTopY = -this.height / 2 + b.headerHeight + b.titleGap;
    const titleMaxWidth = this.width - b.paddingX * 2;

    this.titleGroup = document.createElementNS(SVG_NS, 'g');
    this.titleGroup.setAttribute('class', 'vertex-title-group');

    const fitted = fitTextToLines(this.nodeData.label, t.titleSize, titleMaxWidth, b.titleMaxLines);
    const lines = fitted.text.split('\n');

    lines.forEach((line, idx) => {
      const textEl = document.createElementNS(SVG_NS, 'text');
      textEl.setAttribute('class', 'vertex-title-line');
      textEl.setAttribute('x', String(left));
      textEl.setAttribute('y', String(titleTopY + idx * (t.titleSize * 1.25) + t.titleSize * 0.8));
      textEl.setAttribute('fill', t.titleColor);
      textEl.setAttribute('font-size', String(t.titleSize));
      textEl.setAttribute('font-weight', String(t.titleWeight));
      textEl.style.textShadow = 'none';
      textEl.style.fontFamily = t.fontFamily;
      textEl.textContent = line;
      this.titleGroup.appendChild(textEl);
    });

    this.group.appendChild(this.titleGroup);
  }

  private portsTopY(): number {
    const b = THEME.nodeBody;
    const titleBlock = THEME.typography.titleSize * 1.25 * b.titleMaxLines;
    return -this.height / 2 + b.headerHeight + b.titleGap + titleBlock + 12;
  }

  private buildPorts(): void {
    const halfW = this.width / 2;
    const top = this.portsTopY();
    const spacing = THEME.nodeBody.portSpacing;

    this.nodeData.inputs.forEach((port, idx) => {
      this.createSinglePort(port, -halfW, top + (idx + 0.5) * spacing, true);
    });
    this.nodeData.outputs.forEach((port, idx) => {
      this.createSinglePort(port, halfW, top + (idx + 0.5) * spacing, false);
    });
  }

  private createSinglePort(port: IPort, px: number, py: number, isInput: boolean): void {
    const isConditionBranch = port.conditionBranchIndex !== undefined;
    let portColor = isInput ? '#0F6CBD' : '#C43E1C';
    if (isConditionBranch) {
      portColor = port.conditionBranchIndex === 0 ? '#10B981' : '#F59E0B';
    }

    const portGroup = document.createElementNS(SVG_NS, 'g');
    portGroup.setAttribute('class', `vertex-port vertex-port-${isInput ? 'in' : 'out'}`);
    portGroup.setAttribute('data-port-id', port.id);

    // Círculo visual da porta
    const circle = document.createElementNS(SVG_NS, 'circle');
    circle.setAttribute('cx', String(px));
    circle.setAttribute('cy', String(py));
    circle.setAttribute('r', String(THEME.port.radius));
    circle.setAttribute('fill', portColor);
    circle.setAttribute('stroke', '#FFFFFF');
    circle.setAttribute('stroke-width', '2');
    portGroup.appendChild(circle);

    // Rótulo da porta
    const t = THEME.typography;
    let labelText = (port.name || (isInput ? 'in' : 'out')).toUpperCase();
    let labelColor = t.portColor;

    if (!isInput && isConditionBranch) {
      if (port.conditionBranchIndex === 0) {
        labelText = 'TRUE (0)';
        labelColor = '#047857';
      } else {
        labelText = 'FALSE (1)';
        labelColor = '#B45309';
      }
    }

    const offset = THEME.port.labelOffset;
    const labelX = isInput ? px + offset : px - offset;

    const label = document.createElementNS(SVG_NS, 'text');
    label.setAttribute('class', 'vertex-port-label');
    label.setAttribute('x', String(labelX));
    label.setAttribute('y', String(py));
    label.setAttribute('text-anchor', isInput ? 'start' : 'end');
    label.setAttribute('dominant-baseline', 'central');
    label.setAttribute('fill', labelColor);
    label.setAttribute('font-size', String(t.portSize));
    label.setAttribute('font-weight', String(t.portWeight));
    label.style.textShadow = 'none';
    label.style.fontFamily = t.fontFamily;
    label.textContent = labelText;
    portGroup.appendChild(label);

    // Hitbox invisível expandida para toque / clique
    const hitbox = document.createElementNS(SVG_NS, 'circle');
    hitbox.setAttribute('class', 'vertex-port-hitbox');
    hitbox.setAttribute('cx', String(px));
    hitbox.setAttribute('cy', String(py));
    hitbox.setAttribute('r', String(THEME.port.hitboxRadius));
    hitbox.setAttribute('fill', 'rgba(0, 0, 0, 0.001)');
    hitbox.setAttribute('pointer-events', 'all');
    hitbox.setAttribute('data-port-id', port.id);
    hitbox.setAttribute('data-direction', port.direction);
    hitbox.style.cursor = 'crosshair';

    // Tooltip informativo na porta
    const directionLabel = isInput ? 'Entrada' : 'Saída';
    const tooltipTitle = `${port.name || (isInput ? 'in' : 'out')} · ${directionLabel}`;
    const tooltipDesc = port.dataType ? `Tipo: ${port.dataType}` : undefined;

    // Tooltip SVG nativo acessível
    const portTitleEl = document.createElementNS(SVG_NS, 'title');
    portTitleEl.textContent = tooltipDesc ? `${tooltipTitle} (${tooltipDesc})` : tooltipTitle;
    portGroup.appendChild(portTitleEl);

    const showPortTooltip = (e: PointerEvent) => {
      let anchorX = e.clientX;
      let anchorY = e.clientY;
      if (typeof hitbox.getBoundingClientRect === 'function') {
        const rect = hitbox.getBoundingClientRect();
        if (rect.width > 0) {
          anchorX = rect.left + rect.width / 2;
          anchorY = rect.top;
        }
      }
      this.tooltip.show(`port_${this.nodeData.id}_${port.id}`, { title: tooltipTitle, description: tooltipDesc }, anchorX, anchorY);
    };

    hitbox.addEventListener('pointerenter', showPortTooltip);
    hitbox.addEventListener('pointermove', showPortTooltip);
    hitbox.addEventListener('pointerleave', () => this.tooltip.hide());

    if (isInput) {
      hitbox.addEventListener('pointerup', (e: PointerEvent) => {
        e.stopPropagation();
        this.bus.emit('node:complete-connecting', { nodeId: this.nodeData.id, portId: port.id });
      });
    } else {
      hitbox.addEventListener('pointerdown', (e: PointerEvent) => {
        if (e.button !== 0) return;
        e.stopPropagation();
        this.bus.emit('node:start-connecting', { nodeId: this.nodeData.id, portId: port.id, clientX: e.clientX, clientY: e.clientY });
      });
    }

    portGroup.appendChild(hitbox);
    this.group.appendChild(portGroup);

    this.portElements.set(port.id, { group: portGroup, circle, label, hitbox });
  }

  private buildActionBar(): void {
    const a = THEME.actionBar;
    const count = NODE_ACTIONS.length;
    const barW = count * a.buttonSize + (count - 1) * a.buttonGap + a.padding * 2;
    const barH = a.buttonSize + a.padding * 2;
    const barX = this.width / 2 - barW;
    const barY = -this.height / 2 - a.offsetY - barH;

    const bar = document.createElementNS(SVG_NS, 'g');
    bar.setAttribute('class', 'vertex-action-bar');
    bar.setAttribute('transform', `translate(${barX}, ${barY})`);

    // Pílula de fundo da barra de ações
    const fill = document.createElementNS(SVG_NS, 'rect');
    fill.setAttribute('width', String(barW));
    fill.setAttribute('height', String(barH));
    fill.setAttribute('rx', String(barH / 2));
    fill.setAttribute('ry', String(barH / 2));
    fill.setAttribute('fill', '#FFFFFF');
    fill.setAttribute('stroke', '#CBD5E1');
    fill.setAttribute('stroke-width', '1.25');
    bar.appendChild(fill);

    // Botões de ação com ícones Lucide
    NODE_ACTIONS.forEach((action, i) => {
      const cx = a.padding + a.buttonSize / 2 + i * (a.buttonSize + a.buttonGap);
      const cy = barH / 2;

      const btnGroup = document.createElementNS(SVG_NS, 'g');
      btnGroup.setAttribute('class', 'vertex-action-btn');
      btnGroup.setAttribute('data-action', action.type);
      btnGroup.setAttribute('pointer-events', 'all');
      btnGroup.style.cursor = 'pointer';

      // Tooltip nativo acessível
      const titleEl = document.createElementNS(SVG_NS, 'title');
      titleEl.textContent = `${action.label}: ${action.description}`;
      btnGroup.appendChild(titleEl);

      // Círculo de fundo de hover
      const hoverBg = document.createElementNS(SVG_NS, 'circle');
      hoverBg.setAttribute('cx', String(cx));
      hoverBg.setAttribute('cy', String(cy));
      hoverBg.setAttribute('r', String(a.buttonSize / 2));
      hoverBg.setAttribute('fill', 'transparent');
      hoverBg.setAttribute('class', 'action-btn-hover-bg');
      hoverBg.setAttribute('pointer-events', 'none');
      btnGroup.appendChild(hoverBg);
      this.actionHoverBgs.set(action.type, hoverBg);

      // Ícone Lucide vetorial
      const iconSize = a.iconSize;
      const iconEl = createLucideSvgElement(action.icon, iconSize, action.color, 2.2);
      iconEl.setAttribute(
        'transform',
        `translate(${cx - iconSize / 2}, ${cy - iconSize / 2}) scale(${iconSize / 24})`
      );
      iconEl.setAttribute('pointer-events', 'none');
      btnGroup.appendChild(iconEl);

      // Hitbox do botão com área de captura garantida
      const hit = document.createElementNS(SVG_NS, 'circle');
      hit.setAttribute('cx', String(cx));
      hit.setAttribute('cy', String(cy));
      hit.setAttribute('r', String(a.buttonSize / 2));
      hit.setAttribute('fill', 'rgba(0, 0, 0, 0.001)');
      hit.setAttribute('pointer-events', 'all');

      const showActionTooltip = (e: PointerEvent) => {
        hoverBg.setAttribute('fill', '#EEF2FF');
        let anchorX = e.clientX;
        let anchorY = e.clientY;
        if (typeof hit.getBoundingClientRect === 'function') {
          const rect = hit.getBoundingClientRect();
          if (rect.width > 0) {
            anchorX = rect.left + rect.width / 2;
            anchorY = rect.top;
          }
        }
        this.tooltip.show(
          `act_${this.nodeData.id}_${action.type}`,
          { title: action.label, description: action.description },
          anchorX,
          anchorY
        );
      };

      // Interações de hover e clique do botão
      hit.addEventListener('pointerenter', showActionTooltip);
      hit.addEventListener('pointermove', showActionTooltip);
      hit.addEventListener('pointerleave', () => {
        hoverBg.setAttribute('fill', 'transparent');
        this.tooltip.hide();
      });
      hit.addEventListener('click', (e: MouseEvent) => {
        e.stopPropagation();
        this.tooltip.hide();
        this.bus.emit('node:selected', this.nodeData.id);
        this.bus.emit(action.event, this.nodeData.id);
      });

      btnGroup.appendChild(hit);
      bar.appendChild(btnGroup);
    });

    this.actionBarGroup = bar;
    this.group.appendChild(bar);
  }

  // --- Atualização de Estados e Posição ---

  public updatePosition(x: number, y: number): void {
    this.group.setAttribute('transform', `translate(${x}, ${-y})`);
    this.nodeData.position.x = x;
    this.nodeData.position.y = y;
  }

  public updateLabel(newLabel: string): void {
    this.nodeData.label = newLabel;
    if (this.titleGroup) {
      this.group.removeChild(this.titleGroup);
    }
    this.buildTitleTypography();
  }

  public setSelected(selected: boolean): void {
    this.isSelected = selected;
    this.refreshOutline();
  }

  public setExecutionState(state: 'idle' | 'running' | 'completed'): void {
    this.executionState = state;
    this.refreshOutline();
  }

  private refreshOutline(): void {
    if (this.executionState === 'running') {
      this.selectionRing.setAttribute('stroke', '#10B981');
      this.bodyRect.setAttribute('stroke', '#10B981');
      this.selectionRing.style.display = 'block';
    } else if (this.executionState === 'completed') {
      this.selectionRing.setAttribute('stroke', '#059669');
      this.bodyRect.setAttribute('stroke', '#059669');
      this.selectionRing.style.display = 'block';
    } else {
      this.selectionRing.setAttribute('stroke', '#0F6CBD');
      this.bodyRect.setAttribute('stroke', this.isSelected ? '#0F6CBD' : '#CBD5E1');
      this.selectionRing.style.display = this.isSelected ? 'block' : 'none';
    }
  }

  public setDetailLevel(zoom: number): void {
    const visible = zoom >= THEME.lod.detailZoomThreshold;
    if (visible === this.detailVisible) return;
    this.detailVisible = visible;

    this.portElements.forEach(pe => {
      pe.label.style.display = visible ? 'block' : 'none';
    });
    if (this.actionBarGroup) {
      this.actionBarGroup.style.display = visible ? 'block' : 'none';
    }
  }

  // --- Telemetria em Tempo Real ---

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
    if (this.telemetryGroup) {
      this.group.removeChild(this.telemetryGroup);
      this.telemetryGroup = null;
    }

    const badgeW = this.width - 24;
    const badgeH = 26;
    const badgeY = this.height / 2 + 10;

    const group = document.createElementNS(SVG_NS, 'g');
    group.setAttribute('class', 'vertex-telemetry-badge');
    group.setAttribute('transform', `translate(0, ${badgeY})`);

    const rect = document.createElementNS(SVG_NS, 'rect');
    rect.setAttribute('x', String(-badgeW / 2));
    rect.setAttribute('y', String(-badgeH / 2));
    rect.setAttribute('width', String(badgeW));
    rect.setAttribute('height', String(badgeH));
    rect.setAttribute('rx', String(badgeH / 2));
    rect.setAttribute('ry', String(badgeH / 2));
    rect.setAttribute('fill', bg);
    rect.setAttribute('stroke', border);
    rect.setAttribute('stroke-width', '1.25');
    group.appendChild(rect);

    const iconDef = TELEMETRY_ICONS[iconKey] ?? TELEMETRY_ICONS.status;
    const iconSize = 14;
    const iconEl = createLucideSvgElement(iconDef.icon, iconSize, textCol, 2.2);
    iconEl.setAttribute(
      'transform',
      `translate(${-badgeW / 2 + 10}, ${-iconSize / 2}) scale(${iconSize / 24})`
    );
    group.appendChild(iconEl);

    const label = document.createElementNS(SVG_NS, 'text');
    label.setAttribute('x', String(-badgeW / 2 + 10 + iconSize + 6));
    label.setAttribute('y', '0');
    label.setAttribute('dominant-baseline', 'central');
    label.setAttribute('fill', textCol);
    label.setAttribute('font-size', '12');
    label.setAttribute('font-weight', '600');
    label.style.textShadow = 'none';
    label.style.fontFamily = THEME.typography.fontFamily;
    label.textContent = text;
    group.appendChild(label);

    this.telemetryGroup = group;
    this.group.appendChild(group);
  }

  public dispose(): void {
    if (this.telemetryHandler) {
      this.bus.off(`agent:telemetry:${this.nodeData.id}`, this.telemetryHandler);
    }
    if (this.group.parentNode) {
      this.group.parentNode.removeChild(this.group);
    }
    this.portElements.clear();
    this.actionHoverBgs.clear();
  }
}
