import { defineConfig, type Plugin } from 'vite';
import { viteSingleFile } from 'vite-plugin-singlefile';
import path from 'path';

/**
 * `webui.js` é um arquivo virtual servido pelo backend C++ (FastWebUI) em runtime.
 * Ele não existe no disco e não deve ser resolvido/empacotado pelo Vite.
 *
 * Injetamos a tag clássica somente depois que o Vite processa o HTML, evitando o
 * aviso "can't be bundled without type=module" e preservando a tag no bundle final.
 */
const WEBUI_RUNTIME_SCRIPT =
  '    <!-- Script virtual servido pelo backend C++ (FastWebUI) -->\n' +
  '    <script src="webui.js"></script>\n';

function webuiRuntimeScript(): Plugin {
  return {
    name: 'arestrix:webui-runtime-script',
    transformIndexHtml: {
      order: 'post',
      handler(html) {
        return html.replace(/<\/body>/i, `${WEBUI_RUNTIME_SCRIPT}  </body>`);
      }
    }
  };
}

export default defineConfig({
  plugins: [webuiRuntimeScript(), viteSingleFile()],
  resolve: {
    alias: {
      'i18next': path.resolve(import.meta.dirname, 'third_party/i18next/src/index.js')
    }
  },
  server: {
    port: 3000,
    open: false
  },
  build: {
    target: 'esnext'
  },
  test: {
    setupFiles: ['./tests/setup.ts'],
    include: ['tests/**/*.spec.ts', 'tests/**/*.test.ts']
  }
} as any);
