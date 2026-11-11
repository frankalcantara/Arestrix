# Privacy Policy

[Português (Brasil)](PRIVACY.pt-BR.md)

**Last updated: November 10, 2026**

## 1. Introduction

Arestrix is an open-source desktop studio and native compiler for AI-agent workflows. This policy explains what information may be handled when you visit the project website or use the Arestrix software, how it is stored, and when it may leave your environment.

> **Privacy by design. Arestrix is local-first. Your projects, workflows, memory and logs stay on your machine by default. Only AI-provider calls, MCP connections and services you explicitly configure may send data outside your environment.**

Arestrix does not operate a cloud service that stores your project content. Use of external services is initiated by your configuration or action, as described below.

## 2. Who we are and contact

Arestrix is an open-source project for visual orchestration and native compilation of AI-agent pipelines.

- Website: [arestrix.com](https://arestrix.com)
- General contact: [contact@arestrix.com](mailto:contact@arestrix.com)
- Privacy questions or requests: [privacy@arestrix.com](mailto:privacy@arestrix.com)

## 3. Information handled by the software

Depending on how you use Arestrix, the application may create or process:

- project files, workflow definitions, agent settings and prompt templates;
- generated source code and execution artifacts;
- execution logs and, when you enable it, local memory databases;
- configuration references needed to connect to providers and tools.

These items are stored on your device in the locations shown by the application. They are not sent to Arestrix maintainers by default. You control their retention and can delete them from your device. If you export, commit, back up or synchronize them, the destination's terms and privacy policy apply.

### Credentials

API keys are read from operating-system environment variables. The Studio can save a key as a user environment variable only when you explicitly request it. Arestrix does not write API-key values into project files, localStorage, cookies or the application database. Git credentials are handled by the operating system or Git credential manager.

Do not put secrets in prompts, project files, logs or source control. A connected provider or tool may receive any information you send to it.

## 4. Network connections and third-party services

Arestrix does not transmit project information to its maintainers by default. The software contacts external systems when you configure or request an operation, including:

- **AI providers and gateways:** prompts, context and other request data required to produce a response are sent to the provider or gateway selected by you. This may include OpenAI, Anthropic, Google, Mistral, DeepSeek, a local OpenAI-compatible server or an enterprise gateway. The selected service's privacy policy and terms govern its processing.
- **MCP servers and tools:** a configured connector may send information to its local or remote server. MCP server commands run with your user account's permissions; connect only to servers you trust.
- **Git hosting and remotes:** commits and other repository data you choose to synchronize are sent to the remote you configure, such as GitHub, GitLab, Azure DevOps or Bitbucket.
- **Model and runtime downloads:** catalogs, models and runtime packages may be downloaded from their configured sources when you request or enable the download. Their providers' terms apply.
- **Configured interaction channels:** forms, notifications or other outputs are delivered only through channels you configure.

Arestrix does not control how third parties retain or use information they receive. Review their privacy terms before connecting them.

## 5. Telemetry and diagnostics

The Arestrix desktop application does not include analytics, telemetry or advertising SDKs. It does not automatically upload crash reports, diagnostic information, prompts, project files, memories or logs. Runtime telemetry shown inside the Studio describes execution locally; it is not an automatic upload to Arestrix.

## 6. Website and cookies

Visiting the project website or repository may result in the hosting or security provider processing technical request information, such as IP address, browser details, requested pages and access times, under that provider's own policies. External links and embedded third-party services may also collect information under their own terms.

The desktop application does not use cookies. This policy does not claim that a website host or third-party service never uses cookies or similar technologies; consult the relevant provider's privacy and cookie notices and your browser settings for details.

## 7. Storage and retention

Project files, downloaded runtimes, memory databases and logs are stored locally. Logs are kept on your device and rotated by the application. You can remove local data by deleting the corresponding files or using the application's available controls.

Arestrix maintainers do not receive this local data by default. Information handled by a hosting provider or a third-party service is retained according to that provider's policies and your settings with them.

## 8. Security

Arestrix uses local storage and operating-system facilities for credentials, but no software, storage system or transmission method can be guaranteed completely secure. Protect your device, use trusted providers and MCP servers, keep credentials out of project files, and review data before sending it to an external service.

## 9. International processing

When you use a third-party provider, your information may be processed in countries other than your own. The provider's privacy terms and agreements govern those transfers.

## 10. Children

Arestrix is a general-purpose developer tool and is not designed for children under 13. The project does not knowingly collect children's personal information through the desktop application.

## 11. Your choices and requests

You can choose whether to configure external providers, Git remotes, MCP servers, downloads and interaction channels. You can also remove locally stored project data from your device.

Depending on your location and applicable law, you may have rights concerning personal information processed by a website host or third party. Contact the relevant provider for requests about information it controls. For questions about this policy or Arestrix, contact [privacy@arestrix.com](mailto:privacy@arestrix.com) or [contact@arestrix.com](mailto:contact@arestrix.com).

## 12. Changes to this policy

This policy may be updated as the project changes. Updates will be published on this page with a revised date. The date above identifies the latest revision.
