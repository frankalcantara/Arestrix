import { Graph } from './Graph';
import { INodeData } from './types';

export class GraphvizGenerator {
  public static generateDot(graph: Graph): string {
    const meta = graph.getMetadata();
    const nodes = graph.getAllNodes();
    const edges = graph.getAllEdges();

    let dot = `digraph "${meta.graphName}" {\n`;
    dot += `  fontname="Helvetica,Arial,sans-serif"\n`;
    dot += `  node [fontname="Helvetica,Arial,sans-serif", style="filled,rounded", shape="box"]\n`;
    dot += `  edge [fontname="Helvetica,Arial,sans-serif"]\n`;
    dot += `  label="${meta.description}\\n\\nGerado por Arestrix"\n`;
    dot += `  labelloc="t"\n\n`;

    // Nós
    nodes.forEach(node => {
      dot += this.generateNodeDot(node);
    });

    dot += `\n`;

    // Arestas
    edges.forEach(edge => {
      const srcNode = graph.getNode(edge.sourceNodeId);
      const tgtNode = graph.getNode(edge.targetNodeId);
      if (!srcNode || !tgtNode) return;

      const srcPort = srcNode.outputs.find(p => p.id === edge.sourcePortId);
      
      let edgeStyle = 'solid';
      let edgeColor = '#5b7290';
      let edgeLabel = '';

      if (edge.isFeedbackLoop) {
        edgeStyle = 'dashed';
        edgeColor = '#f59e0b'; // Dourado
      }

      if (srcNode.type === 'condition' && srcPort && srcPort.conditionBranchIndex !== undefined) {
        edgeLabel = ` [branch ${srcPort.conditionBranchIndex}]`;
        if (srcPort.conditionBranchIndex === 0) edgeColor = '#10b981'; // Verde para true/0
        if (srcPort.conditionBranchIndex === 1) edgeColor = '#ef4444'; // Vermelho para false/1
      }

      dot += `  "${srcNode.id}" -> "${tgtNode.id}" [color="${edgeColor}", style="${edgeStyle}", label="${edgeLabel}", penwidth=2.0];\n`;
    });

    dot += `}\n`;
    return dot;
  }

  private static generateNodeDot(node: INodeData): string {
    let color = '#3b82f6';
    let fontColor = '#ffffff';
    let label = node.label.replace(/"/g, '\\"');

    switch (node.type) {
      case 'static': color = '#3b82f6'; break;
      case 'condition': color = '#ff7828'; break;
      case 'subflow': color = '#8b5cf6'; break;
      case 'module': color = '#10b981'; break;
      case 'critical': color = '#06b6d4'; break;
      case 'cuda': color = '#14b8a6'; break;
      case 'agent': color = '#6366f1'; break;
    }

    // Para subflows, podemos indicar no ródulo
    let extra = '';
    if (node.type === 'subflow' || node.type === 'module') {
      extra = '\\n(Subgraph)';
    } else if (node.type === 'cuda') {
      extra = '\\n(GPU Kernel)';
    } else if (node.type === 'agent') {
      extra = '\\n(Code Mode Agent)';
    }

    return `  "${node.id}" [label="${label}${extra}", fillcolor="${color}", fontcolor="${fontColor}", color="${color}"];\n`;
  }
}
