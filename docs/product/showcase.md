# Feature showcase

> **Applies to:** Arestrix Beta 2.2 (`2.2.0-beta.1`) · Last reviewed: 2026-10-27

## 1. Studio canvas and agent swarm

The Studio uses an Office-inspired ribbon and an infinite canvas. A running pipeline shows
completed and active vertices, live thinking telemetry, the C++20 Taskflow build terminal and the
agent inspector.

![Arestrix Studio main canvas with a running multi-agent pipeline](../assets/screenshots/studio-main.webp)

## 2. Native C++20 Taskflow generation

Generated C++20 source is available in the embedded Monaco editor. Parallel branches map to
concurrent Taskflow tasks, and the generated project can be compiled and inspected outside the
Studio.

![C++20 Taskflow code generator in Monaco editor](../assets/screenshots/cpp20-taskflow-generator.webp)

## 3. Standalone runner

The exported runner gives non-technical operators a focused execution screen with a progress
timeline, node indicators and iteration history.

![Standalone runner progress timeline](../assets/screenshots/standalone-runner.webp)

## 4. Telemetry and diagnostics

Execution diagnostics include node latency, token throughput, live model reasoning streams and
tool argument inspection.

![Standalone runner diagnostics and telemetry](../assets/screenshots/runner-diagnostics.webp)

## 5. Governance and enterprise export

The export flow configures credential vaults (Windows DPAPI or Azure Key Vault), generates the
integrity manifest and prepares MSI/MSIX packaging for managed Windows deployment.

![Standalone runner export and governance](../assets/screenshots/runner-export-governance.webp)

## 6. Office Properties hub

The Backstage **Properties** hub brings the three configuration scopes into one Office-style
surface. Each scope opens the same shared shell with left navigation, content cards and a fixed
Cancel/Save footer.

![Properties hub for Studio, Workspace and Project](../assets/screenshots/properties-hub.webp)

### Studio Properties

Language, theme, canvas, default model, credentials, AI Gateway and advanced reset live in the
Studio scope. Provider keys remain write-only status fields.

![Studio Properties](../assets/screenshots/studio-properties.webp)

## 7. Workspace and Project Properties

The Workspace owns the default sandbox and audit policy. The Project owns the manifest, model/tools
defaults, sanitized environment references and SQLite path.

![Workspace sandbox properties](../assets/screenshots/workspace-properties.webp)

![Project Properties](../assets/screenshots/project-properties.webp)

![Project model and tools defaults](../assets/screenshots/project-defaults.webp)

## 8. Light and dark themes

Choose **Light**, **Dark** or **Follow system** in Studio Properties. The theme applies to the
ribbon, task pane, console, Backstage and Office dialogs; the canvas background remains an
independent preference.

![Arestrix Studio in dark mode](../assets/screenshots/studio-dark.webp)

![Studio Properties in dark mode](../assets/screenshots/studio-properties-dark.webp)

## 9. Vertex dialogs in the shared Office shell

Sandbox, model, tools and memory dialogs for a vertex now use the same `BaseModal` shell as the
Properties screens, while keeping explicit inheritance and per-vertex overrides.

![Vertex sandbox dialog](../assets/screenshots/vertex-sandbox.webp)

![Vertex model dialog](../assets/screenshots/vertex-model.webp)

> Screenshots are captured from the development build. Interface details may change during the
> beta. Regenerate them with `npm run capture:screenshots`.
