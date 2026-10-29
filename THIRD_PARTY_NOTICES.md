# Third-party notices

Arestrix is released under the [MIT License](LICENSE). It includes or depends on the third-party
components listed below. The binding license text for each component is distributed with that
component, under `third_party/` or `node_modules/`, and is not replaced by this summary.

When distributing Arestrix binaries or packages, include this file together with the license texts
of the components you redistribute.

## Vendored and submodule components

| Component | Version / pin | License | Role |
| :--- | :--- | :--- | :--- |
| [Taskflow](https://github.com/taskflow/taskflow) | `v4.1.0` (`45366fe`) | MIT | C++20 parallel and heterogeneous task DAG engine. |
| [simdjson](https://github.com/simdjson/simdjson) | `v5.0.5` (`9c00353`) | Apache-2.0 | SIMD-accelerated JSON parsing. |
| [FastWebUI](https://github.com/frankalcantara/FastWebUI) (fork of [WebUI](https://github.com/webui-dev/webui)) | `2.5.0-beta.4` (`a8e95c1`) | MIT | Native window and C++/web RPC bridge. |
| [i18next](https://github.com/i18next/i18next) | `v26.4.4-2-g15d0b9d9` | MIT | Internationalization runtime. |
| [Monaco Editor](https://github.com/microsoft/monaco-editor) | npm `0.57.0`; submodule `dbf1945` | MIT | Code editor used by the Studio. |
| [SkillSpector](https://github.com/NVIDIA/SkillSpector) | `v2.12.0-1159-g2b9f4512` | Apache-2.0 | Skill security analysis and static rule reference. |
| [Cloudflare security-audit-skill](https://github.com/cloudflare/security-audit-skill) | `c1c8a8c` | MIT | Boundary-driven security audit methodology used by the Arestrix Skill Guard. |
| [SQLite](https://sqlite.org) | `3.54.0` amalgamation | Public domain | Embedded persistence. |
| [ONNX Runtime](https://github.com/microsoft/onnxruntime) | `1.24+` (validado com `1.24.4` DirectML e `1.31.1` CPU) | MIT | Inferência da compressão de prosa (`ARESTRIX_WITH_ONNX`). O instalador oficial inclui `onnxruntime.dll`; o build de desenvolvimento padrão não o embute. |
| [DirectML](https://github.com/microsoft/DirectML) | `1.15.4` | Licença Microsoft DirectML (redistribuível em apps) | Aceleração da prosa na GPU (`ARESTRIX_WITH_ONNX_DML`, Windows). O instalador oficial inclui `DirectML.dll` e `onnxruntime_providers_shared.dll`. |
| [cpp-httplib](https://github.com/yhirose/cpp-httplib) | `v0.62.0` | MIT | HTTP server of the on-prem `arestrix-gateway`. |
| [Inter](https://github.com/rsms/inter) | `third_party/fonts/inter` | SIL OFL-1.1 | Interface and canvas typography. |

## Components downloaded at runtime (not bundled)

| Component | Version / source | License | Role |
| :--- | :--- | :--- | :--- |
| [llama.cpp / ggml](https://github.com/ggml-org/llama.cpp) | `b11669`, pinned in `CppCore/runtime/llama.manifest.json` | MIT | Optional local inference runtime (`llama-server`). Downloaded on demand and verified by SHA-256. |
| GGUF model weights | Pinned revisions in `CppCore/runtime/models.catalog.json` | Per model (currently Apache-2.0) | Downloaded directly from the model publisher. Arestrix does not redistribute weights. |
| [Kompress-v2-base](https://huggingface.co/chopratejas/kompress-v2-base) | `kompress-fp32.onnx` / `kompress-int8-wo.onnx` | Apache-2.0 | Optional prose compressor (token classification). Downloaded on demand; Arestrix does not redistribute the weights. |
| [ModernBERT-base](https://huggingface.co/answerdotai/ModernBERT-base) | Base encoder of Kompress | Apache-2.0 | Encoder used by the compressor model. |

The `llama.cpp` source is not vendored or linked into Arestrix; it runs as a separate process behind an
OpenAI-compatible HTTP endpoint. Model licenses are displayed in the Local Models screen and must be
accepted by the user before download.

## Direct npm dependencies

| Package | Version | License | Role |
| :--- | :--- | :--- | :--- |
| [`@hpcc-js/wasm`](https://www.npmjs.com/package/@hpcc-js/wasm) | `2.35.3` | Apache-2.0 | Graphviz/WASM support. |
| [`lucide`](https://www.npmjs.com/package/lucide) | `0.453.0` | ISC | Icon set. |
| [`monaco-editor`](https://www.npmjs.com/package/monaco-editor) | `0.57.0` | MIT | Code editor component. |
| [`three`](https://www.npmjs.com/package/three) | `0.169.0` | MIT | 3D canvas rendering. |
| [`troika-three-text`](https://www.npmjs.com/package/troika-three-text) | `0.50.3` | MIT | Text rendering in the 3D canvas. |

## Build and test dependencies

Build and test tooling includes TypeScript, Vite, Vitest, Puppeteer Core and their transitive
dependencies. Their exact versions and license metadata are recorded in `package-lock.json`.
`scripts/check_third_party_licenses.py` verifies that production dependencies use licenses
compatible with the project's MIT distribution policy.

## Reporting a licensing issue

If you believe a component is missing attribution or is listed with the wrong license, open an issue
using the bug template and include the component name, version and the relevant license file. Do not
include confidential material.
