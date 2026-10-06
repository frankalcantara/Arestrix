import codecs

with codecs.open('src/ui/Toolbar.ts', 'r', 'utf-8') as f:
    text = f.read()

# 1. Add Import
text = text.replace(
    "import { TemplateModal } from './TemplateModal.ts';",
    "import { TemplateModal } from './TemplateModal.ts';\nimport { GraphvizGenerator } from '../core/GraphvizGenerator.ts';"
)

# 2. Add Button HTML
target_html = '<button id="btn-export-code" class="tool-btn btn-secondary" title="Opção avançada de engenharia: inspecionar ou exportar arquivos de código fonte">⚙️ Exportar</button>'
# Because of encoding issues in previous attempts, we'll find it dynamically
lines = text.split('\n')
for i, line in enumerate(lines):
    if 'id="btn-export-code"' in line:
        lines.insert(i + 1, '          <button id="btn-export-svg" class="tool-btn btn-secondary" title="Exportar arquitetura visual do grafo e baixar SVG usando Graphviz">🖼️ Exportar SVG</button>')
        break

# 3. Add Event Listener
for i, line in enumerate(lines):
    if 'this.codeModal.show();' in line:
        lines.insert(i + 3, """
    // Exportar SVG Graphviz
    this.container.querySelector('#btn-export-svg')?.addEventListener('click', () => {
      this.exportGraphvizSVG();
    });""")
        break

text = '\n'.join(lines)

# 4. Add Method exportGraphvizSVG
export_method = """
  private exportGraphvizSVG(): void {
    const dotString = GraphvizGenerator.generateDot(this.graph);
    
    // Create an elegant viewer using d3-graphviz CDN
    const html = `<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8">
  <title>TaskForge Graphviz Export</title>
  <style>
    body { margin: 0; padding: 0; background: #f6f8fc; font-family: Inter, sans-serif; display: flex; flex-direction: column; height: 100vh; }
    header { background: #ffffff; padding: 16px 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); display: flex; justify-content: space-between; align-items: center; }
    h1 { margin: 0; font-size: 18px; color: #0f172a; font-weight: 800; }
    .btn { background: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 8px; font-weight: 700; cursor: pointer; }
    .btn:hover { background: #2563eb; }
    #graph-container { flex: 1; overflow: auto; padding: 24px; display: flex; justify-content: center; align-items: center; }
    svg { max-width: 100%; max-height: 100%; }
  </style>
</head>
<body>
  <header>
    <h1>Visualizacao Graphviz (DOT)</h1>
    <button class="btn" id="btn-download">Baixar SVG</button>
  </header>
  <div id="graph-container">Carregando renderizador Graphviz (WASM)...</div>
  
  <script src="https://d3js.org/d3.v7.min.js"></script>
  <script src="https://unpkg.com/@hpcc-js/wasm@2.13.0/dist/index.min.js"></script>
  <script src="https://unpkg.com/d3-graphviz@5.4.0/build/d3-graphviz.js"></script>
  
  <script>
    const dotSrc = \\\`${dotString.replace(/\\`/g, '\\\\`')}\\\`;
    
    d3.select("#graph-container")
      .graphviz()
      .renderDot(dotSrc)
      .on("end", function() {
        document.getElementById('btn-download').addEventListener('click', () => {
          const svgData = document.querySelector('#graph-container svg').outerHTML;
          const blob = new Blob([svgData], { type: 'image/svg+xml;charset=utf-8' });
          const url = URL.createObjectURL(blob);
          const a = document.createElement('a');
          a.href = url;
          a.download = 'taskforge_architecture.svg';
          a.click();
          URL.revokeObjectURL(url);
        });
      });
  </script>
</body>
</html>`;

    const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    window.open(url, '_blank');
  }
}
"""

last_brace = text.rfind("}")
text = text[:last_brace] + export_method

with codecs.open('src/ui/Toolbar.ts', 'w', 'utf-8') as f:
    f.write(text)
