#pragma once

// Servidor MCP stdio da Biblioteca de Vertices e Agentes (`arestrix-mcp-biblioteca`).
//
// Protocolo: JSON-RPC 2.0, uma mensagem por linha, sobre stdin/stdout.
// Ferramentas: library_list_assets, library_get_asset, library_search_assets,
// library_install_asset, library_derive_asset, library_restore_asset, library_audit_asset.
//
// O catalogo e carregado sob demanda na primeira chamada de ferramenta e mantido
// em cache no processo; `refresh` so recarrega o estado do Workspace quando o
// arquivo muda. O loop e estritamente sequencial, como exige o transporte stdio.

#include <filesystem>
#include <iosfwd>

namespace taskforge {
namespace libraries {

// Executa o loop do servidor. Retorna 0 em EOF/fim normal.
int runLibraryMcpServer(std::istream& in, std::ostream& out, const std::filesystem::path& projectRoot);

} // namespace libraries
} // namespace taskforge
