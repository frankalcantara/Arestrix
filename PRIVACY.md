# Privacy policy

Arestrix is a local desktop application. It does not include analytics, telemetry or advertising
SDKs, and it does not send project content to the Arestrix project maintainers by default.

## Network access

Arestrix contacts external systems only when the user configures or requests an operation:

- **AI providers:** prompts and context are sent to the provider selected for the agent (for example
  DeepSeek, OpenAI, Anthropic, Gemini, Mistral, a local OpenAI-compatible server or an AI Gateway).
  The provider's own privacy policy applies.
- **Local model/runtime downloads:** model catalogs and llama.cpp runtime packages are downloaded
  from their configured upstream sources after explicit user action.
- **Git synchronization:** project repositories are synchronized with the Git remote configured by
  the user. Credentials are handled by the operating system/Git credential manager.
- **MCP connectors:** tools configured by the user may contact their own local or remote servers.
- **Interaction channels:** forms and notifications are delivered only through channels configured
  by the user.

## Data storage

- API keys are read from operating-system environment variables. The Studio can persist a key as a
  user environment variable when the user explicitly asks; keys are not written to project files,
  localStorage, cookies or the application database.
- Projects, pipelines, memory databases, logs and downloaded runtimes stay on the user's machine in
  the paths shown by the Studio.
- Logs are local (`%LOCALAPPDATA%\Arestrix\logs` on Windows) and are rotated.
- Crash reports or telemetry are not uploaded automatically.

## Third-party components

Arestrix includes open-source components listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and the release SBOM. Their licenses and privacy practices apply to their respective code.

## Contact

Privacy questions can be opened as a GitHub issue or sent through the contact channels listed in
[SUPPORT.md](SUPPORT.md).
