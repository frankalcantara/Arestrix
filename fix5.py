import codecs

with codecs.open('src/ui/Toolbar.ts', 'r', 'utf-8') as f:
    text = f.read()

# I will replace the exportGraphvizSVG method completely.
start_idx = text.find('private exportGraphvizSVG(): void {')
end_idx = text.rfind('}')

method = """private exportGraphvizSVG(): void {
    const dotString = GraphvizGenerator.generateDot(this.graph);
    
    // Create an elegant viewer using d3-graphviz CDN
    const html = '<!DOCTYPE html>\\n' +
'<html>\\n' +
'<head>\\n' +
'  <meta charset="utf-8">\\n' +
'  <title>TaskForge Graphviz Export</title>\\n' +
'  <style>\\n' +
'    body { margin: 0; padding: 0; background: #f6f8fc; font-family: Inter, sans-serif; display: flex; flex-direction: column; height: 100vh; }\\n' +
'    header { background: #ffffff; padding: 16px 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); display: flex; justify-content: space-between; align-items: center; }\\n' +
'    h1 { margin: 0; font-size: 18px; color: #0f172a; font-weight: 800; }\\n' +
'    .btn { background: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 8px; font-weight: 700; cursor: pointer; }\\n' +
'    .btn:hover { background: #2563eb; }\\n' +
'    #graph-container { flex: 1; overflow: auto; padding: 24px; display: flex; justify-content: center; align-items: center; }\\n' +
'    svg { max-width: 100%; max-height: 100%; }\\n' +
'  </style>\\n' +
'</head>\\n' +
'<body>\\n' +
'  <header>\\n' +
'    <h1>Visualizacao Graphviz (DOT)</h1>\\n' +
'    <button class="btn" id="btn-download">Baixar SVG</button>\\n' +
'  </header>\\n' +
'  <div id="graph-container">Carregando renderizador Graphviz (WASM)...</div>\\n' +
'  \\n' +
'  <script src="https://d3js.org/d3.v7.min.js"></script>\\n' +
'  <script src="https://unpkg.com/@hpcc-js/wasm@2.13.0/dist/index.min.js"></script>\\n' +
'  <script src="https://unpkg.com/d3-graphviz@5.4.0/build/d3-graphviz.js"></script>\\n' +
'  \\n' +
'  <script>\\n' +
'    const dotSrc = `' + dotString.replace(/`/g, '\\\\`') + '`;\\n' +
'    \\n' +
'    d3.select("#graph-container")\\n' +
'      .graphviz()\\n' +
'      .renderDot(dotSrc)\\n' +
'      .on("end", function() {\\n' +
'        document.getElementById("btn-download").addEventListener("click", () => {\\n' +
'          const svgData = document.querySelector("#graph-container svg").outerHTML;\\n' +
'          const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });\\n' +
'          const url = URL.createObjectURL(blob);\\n' +
'          const a = document.createElement("a");\\n' +
'          a.href = url;\\n' +
'          a.download = "taskforge_architecture.svg";\\n' +
'          a.click();\\n' +
'          URL.revokeObjectURL(url);\\n' +
'        });\\n' +
'      });\\n' +
'  </script>\\n' +
'</body>\\n' +
'</html>';

    const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    window.open(url, '_blank');
  }
"""

text = text[:start_idx] + method

with codecs.open('src/ui/Toolbar.ts', 'w', 'utf-8') as f:
    f.write(text)
