# SQLite (amalgamation)

SQLite 3.54.0, amalgamation oficial (`sqlite3.c` e `sqlite3.h`), de domínio público: https://sqlite.org/copyright.html

- Origem: https://sqlite.org/2026/sqlite-amalgamation-3540000.zip
- SHA3-256 do zip: `7b670a62fdfbd672b75fef004cb703c8a3e87d3a5cc7d675b4a08337004a2d93` (confere com https://sqlite.org/download.html)
- Arquivos sem nenhuma alteração. As opções de compilação (sem carga de extensões, sem aspas duplas como texto etc.) ficam em `CMakeLists.txt`.
- Usado só pelas ferramentas nativas `arestrix.sqlite` (`CppCore/src/native/NativeSqlite.cpp`).
