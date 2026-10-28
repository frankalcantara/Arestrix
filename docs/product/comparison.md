# Comparison

> **Applies to:** Arestrix Beta 2.3 (`2.3.0-beta.1`) · Last reviewed: 2026-10-19
>
> **Scope:** this is a directional summary based on public documentation and the Arestrix design,
> not a benchmark. Capabilities and licensing of third-party products change; verify them with the
> upstream project before procurement. Product names and trademarks belong to their owners.

The table contrasts architectural choices that matter when an agent workflow has to run on a
managed endpoint.

| Dimension | Arestrix | Server-based workflow platforms (e.g. n8n) | Script-based agent wrappers | Hosted chat platforms |
| :--- | :--- | :--- | :--- | :--- |
| **Execution model** | Native C++20 compiled from the graph, with Taskflow parallel DAGs. | Usually an interpreted Node.js service or container. | Python or Node.js scripts with framework dependencies. | Remote inference; workflow logic runs in the vendor environment. |
| **End-user distribution** | Standalone runner; optional MSI/MSIX packaging for managed Windows deployment. | Typically a server, container or hosted subscription. | Requires an interpreter, package manager and environment setup. | Web or desktop client only; no portable workflow artifact. |
| **Secrets** | Runtime resolution through DPAPI or Azure Key Vault; no secret values in project files. | Depends on deployment: environment variables, database or external vault. | Commonly environment variables or `.env` files. | Managed by the vendor; data leaves the local perimeter. |
| **Package integrity** | SHA-256 HMAC manifest with optional fail-closed verification at runner startup. | Depends on the deployment and artifact pipeline. | Depends on the project; often no package-level verification. | Not applicable to a locally distributed artifact. |
| **Visual orchestration** | Interactive graph canvas with decision vertices, feedback loops and cost caps. | Visual workflow canvas, usually oriented to integrations and ETL. | Typically code-first or terminal-first. | Conversational interface. |
| **Enterprise packaging** | Packaging flow for MSI/MSIX and silent deployment; validate against your own Intune/SCCM baseline. | Requires container or web infrastructure governance. | Requires per-endpoint runtime management. | Not designed for offline endpoint deployment. |
| **MCP** | Native MCP client with a persistent session per server and a tool registry. | Varies by community nodes or custom HTTP steps. | Varies by framework and integration. | Usually limited to vendor-supported tools. |
| **Licensing** | MIT; model API usage is billed by the provider you choose. | Varies: fair-code or commercial editions. | Varies by framework. | Commercial subscription. |

## How to read this table

- "Depends" means the property is a deployment decision, not an inherent limitation.
- Arestrix is optimized for local, reviewable artifacts and managed Windows endpoints. It is not a
  hosted multi-tenant automation service.
- For a feature-by-feature evaluation, run the [examples](../../examples/README.md) against your
  own model provider and packaging baseline.
