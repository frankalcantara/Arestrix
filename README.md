# NodeForge ⚡

**NodeForge** é um ambiente visual e editor de nós de alta performance desenvolvido em **Three.js** (WebGL) para modelagem, orquestração e geração de código de pipelines assíncronos e heterogêneos de agentes com **Taskflow** em **C++20**.

O sistema foi arquitetado com separação estrita em três camadas desacopladas:
1. **Camada Lógica (Grafo / Engine de Dados)**: Estrutura em memória, validação topológica acíclica, validação de tipos de portas e checagem de feedback loops de decisão.
2. **Camada Visual & Interação (Three.js WebGL)**: Câmera ortográfica 2.5D com pan e zoom focados no cursor, grid procedural infinito com shaders, nós modulares com cantos arredondados, sockets circulares com snap magnético e conexões Bézier cúbicas via `Line2`.
3. **Camada de Exportação (Compilador Taskflow C++20)**: Geração modular de código C++20, compatível nativamente com o compilador **MSVC** (`cl.exe`) no Windows, scripts PowerShell de build, `CMakeLists.txt` e `Makefile`.

---

## 🎨 Diretrizes de Design & Tipografia Nítida

Em conformidade estrita com as regras do projeto:
- **Zero efeitos de sombra/glow em texto**: Não são utilizados `filter: drop-shadow`, `text-shadow` nem filtros SVG de desfoque (`feGaussianBlur`).
- **Destaque por contraste limpo**:
  - Contraste de cor direto entre texto e fundo.
  - Peso tipográfico reforçado (`font-weight: 700` e `font-weight: 800`).
  - Pílulas/badges de fundo limpo com cantos arredondados por trás dos títulos.
  - Atenuação por contraste de opacidade (`0.45` para elementos inativos e `1.0` para elementos em foco).

---

## 🚀 Como Executar o Projeto Web

### 1. Pré-requisitos
- Node.js 18+ (testado no Node.js v24)
- npm 9+

### 2. Instalação e Inicialização

```bash
# Instalar dependências
npm install

# Iniciar servidor de teste automatizado (roda testes e sobe o servidor)
python server_teste.py
# ou:
npm run server:test

# Ou iniciar apenas o servidor Vite de desenvolvimento
npm run dev

# Compilar para produção (TypeScript + Vite)
npm run build
```

---

## 🛠️ Como Compilar e Rodar o Pipeline C++20 Gerado (Taskflow)

### Opção 1: Compilação Direta com MSVC no Windows (Padrão Recomendado)
Abra o **Developer PowerShell for VS** ou **Developer Command Prompt** e execute o script gerado:

```powershell
.\build_msvc.ps1
```
Ou manualmente com o `cl.exe`:
```cmd
cl.exe /std:c++20 /EHsc /O2 /W4 /I"include" main.cpp /Fe:"pipeline_app.exe"
.\pipeline_app.exe
```

### Opção 2: Compilação com CMake (MSVC ou Ninja)
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
.\Release\pipeline_app.exe
```

### Opção 3: Compilação com Make (MinGW / Git Bash / WSL)
```bash
make
./pipeline_app.exe
```

---

## 📦 Mapeamento de Tarefas Taskflow no NodeForge

| Tipo de Nó | Tipo no Taskflow | Semântica |
| :--- | :--- | :--- |
| **Static Task** (Azul) | `tf::Task (taskflow.emplace([](){}))` | Computação determinística de agentes. |
| **Condition Task** (Âmbar) | `tf::Task (retorna int para branch)` | Decisões estocásticas, limiares e feedback loops. |
| **Subflow Task** (Roxo) | `tf::Task (recebe tf::Subflow& sf)` | Decomposição dinâmica em grafo filho durante runtime. |
| **Module Task** (Verde) | `tf::Task (tf.composed_of(...))` | Módulo hierárquico encapsulado. |
| **Critical Section** (Vermelho) | `tf::CriticalSection (acquire / release)` | Exclusão mútua e sincronização atômica concorrente. |

---

## 🧪 Testes Automatizados

O projeto possui suíte de testes unitários para o validador de grafo (detecção de ciclos e loops de condição) e para o gerador de código C++20:

```bash
npm run test
```
