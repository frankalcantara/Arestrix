# Política de privacidade

[English](PRIVACY.md)

**Última atualização: 10 de novembro de 2026**

## 1. Introdução

O Arestrix é um estúdio desktop de código aberto e compilador nativo para fluxos de trabalho com agentes de IA. Esta política explica quais informações podem ser tratadas quando você visita o site do projeto ou usa o software Arestrix, como são armazenadas e em que situações podem sair do seu ambiente.

> **Privacidade desde o projeto. O Arestrix é local-first. Seus projetos, fluxos de trabalho, memória e logs permanecem na sua máquina por padrão. Somente chamadas a provedores de IA, conexões MCP e serviços que você configurar explicitamente podem enviar dados para fora do seu ambiente.**

O Arestrix não opera um serviço em nuvem que armazene o conteúdo dos seus projetos. O uso de serviços externos depende da sua configuração ou ação, conforme descrito abaixo.

## 2. Quem somos e contato

O Arestrix é um projeto de código aberto para orquestração visual e compilação nativa de pipelines de agentes de IA.

- Site: [arestrix.com](https://arestrix.com)
- Contato geral: [contact@arestrix.com](mailto:contact@arestrix.com)
- Dúvidas ou solicitações de privacidade: [privacy@arestrix.com](mailto:privacy@arestrix.com)

## 3. Informações tratadas pelo software

Dependendo de como você usa o Arestrix, o aplicativo pode criar ou processar:

- arquivos de projeto, definições de fluxos, configurações de agentes e modelos de prompt;
- código-fonte gerado e artefatos de execução;
- logs de execução e, quando ativados por você, bancos de dados locais de memória;
- referências de configuração necessárias para conectar provedores e ferramentas.

Esses itens são armazenados no seu dispositivo, nos locais indicados pelo aplicativo. Por padrão, não são enviados aos mantenedores do Arestrix. Você controla sua retenção e pode apagá-los do dispositivo. Se você os exportar, versionar, incluir em backup ou sincronizar, aplicam-se os termos e a política de privacidade do destino.

### Credenciais

As chaves de API são lidas de variáveis de ambiente do sistema operacional. O Studio pode salvar uma chave como variável de ambiente do usuário somente quando você solicitar explicitamente. O Arestrix não grava valores de chaves de API em arquivos de projeto, localStorage, cookies ou no banco de dados do aplicativo. As credenciais do Git são tratadas pelo sistema operacional ou pelo gerenciador de credenciais do Git.

Não inclua segredos em prompts, arquivos de projeto, logs ou no controle de versão. Um provedor ou ferramenta conectada poderá receber as informações que você enviar a ela.

## 4. Conexões de rede e serviços de terceiros

Por padrão, o Arestrix não transmite informações dos projetos aos mantenedores. O software se conecta a sistemas externos quando você configura ou solicita uma operação, incluindo:

- **Provedores e gateways de IA:** prompts, contexto e outros dados necessários para gerar uma resposta são enviados ao provedor ou gateway escolhido por você. Isso pode incluir OpenAI, Anthropic, Google, Mistral, DeepSeek, um servidor local compatível com OpenAI ou um gateway empresarial. O tratamento segue os termos e a política de privacidade do serviço escolhido.
- **Servidores e ferramentas MCP:** um conector configurado pode enviar informações ao servidor local ou remoto correspondente. Comandos de servidores MCP são executados com as permissões da sua conta; conecte-se apenas a servidores confiáveis.
- **Hospedagem e repositórios Git:** commits e outros dados de repositório que você escolher sincronizar são enviados ao destino configurado, como GitHub, GitLab, Azure DevOps ou Bitbucket.
- **Downloads de modelos e runtimes:** catálogos, modelos e pacotes de runtime podem ser baixados de suas fontes configuradas quando você solicitar ou habilitar o download. Aplicam-se os termos dos respectivos provedores.
- **Canais de interação configurados:** formulários, notificações ou outras saídas são entregues somente por canais que você configurar.

O Arestrix não controla como terceiros retêm ou usam as informações recebidas. Consulte os termos de privacidade desses serviços antes de conectá-los.

## 5. Telemetria e diagnósticos

O aplicativo desktop Arestrix não inclui SDKs de análise, telemetria ou publicidade. Ele não envia automaticamente relatórios de falha, informações de diagnóstico, prompts, arquivos de projeto, memórias ou logs. A telemetria de execução exibida no Studio descreve a execução localmente; não é um envio automático ao Arestrix.

## 6. Site e cookies

Ao visitar o site ou o repositório do projeto, o provedor de hospedagem ou segurança pode processar informações técnicas da solicitação, como endereço IP, dados do navegador, páginas solicitadas e horários de acesso, conforme a própria política desse provedor. Links externos e serviços de terceiros incorporados também podem coletar informações segundo seus próprios termos.

O aplicativo desktop não usa cookies. Esta política não afirma que o provedor de hospedagem do site ou serviços de terceiros nunca usem cookies ou tecnologias semelhantes; consulte os avisos de privacidade e cookies do respectivo provedor e as configurações do seu navegador para obter detalhes.

## 7. Armazenamento e retenção

Arquivos de projeto, runtimes baixados, bancos de dados de memória e logs são armazenados localmente. Os logs permanecem no dispositivo e são rotacionados pelo aplicativo. Você pode remover os dados locais apagando os arquivos correspondentes ou usando os controles disponíveis no aplicativo.

Por padrão, os mantenedores do Arestrix não recebem esses dados locais. As informações tratadas por um provedor de hospedagem ou serviço de terceiros são retidas conforme as políticas desse provedor e as configurações que você definiu com ele.

## 8. Segurança

O Arestrix usa armazenamento local e recursos do sistema operacional para credenciais, mas nenhum software, sistema de armazenamento ou método de transmissão pode ser considerado totalmente seguro. Proteja seu dispositivo, use provedores e servidores MCP confiáveis, mantenha credenciais fora dos arquivos do projeto e revise os dados antes de enviá-los a um serviço externo.

## 9. Tratamento internacional

Ao usar um provedor de terceiros, suas informações podem ser tratadas em países diferentes do seu. As transferências são regidas pelos termos e acordos de privacidade do provedor.

## 10. Crianças

O Arestrix é uma ferramenta de desenvolvimento de uso geral e não foi projetado para crianças menores de 13 anos. O projeto não coleta intencionalmente informações pessoais de crianças por meio do aplicativo desktop.

## 11. Suas escolhas e solicitações

Você pode escolher se deseja configurar provedores externos, remotos Git, servidores MCP, downloads e canais de interação. Também pode remover do seu dispositivo os dados armazenados localmente.

Dependendo da sua localização e da legislação aplicável, você pode ter direitos sobre informações pessoais tratadas por um provedor de hospedagem ou terceiro. Para solicitações sobre informações controladas por esses provedores, entre em contato diretamente com eles. Para dúvidas sobre esta política ou sobre o Arestrix, escreva para [privacy@arestrix.com](mailto:privacy@arestrix.com) ou [contact@arestrix.com](mailto:contact@arestrix.com).

## 12. Alterações nesta política

Esta política poderá ser atualizada conforme o projeto evoluir. As atualizações serão publicadas nesta página com a data revisada. A data acima indica a revisão mais recente.
