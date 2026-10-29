#pragma once

#include <string>
#include <string_view>

namespace taskforge {
namespace rt {
namespace compress {

// Tipo de conteudo de uma observacao de ferramenta. Define qual compressor entra.
enum class ContentKind {
    Skip,    // nao comprimir (pequeno, ambíguo ou sensivel)
    Json,
    Log,
    Code,
    Prose
};

struct Classification {
    ContentKind kind = ContentKind::Skip;
    std::string reason;   // curto, para o trace; nunca inclui o conteudo
};

// Classifica o texto de uma observacao. Contrato conservador: na duvida, Skip/Prose,
// nunca Json por engano (comprimir codigo ou log como JSON corromperia a leitura).
Classification classifyContent(std::string_view text);

const char* contentKindName(ContentKind kind);

} // namespace compress
} // namespace rt
} // namespace taskforge
