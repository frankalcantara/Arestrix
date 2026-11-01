cmake_minimum_required(VERSION 3.20)

# version.txt e a fonte de verdade da versao do produto (SemVer).
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt" ARESTRIX_VERSION LIMIT_COUNT 1)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/version.txt")
string(REGEX REPLACE "^([0-9]+\\.[0-9]+\\.[0-9]+).*$" "\\1" ARESTRIX_VERSION_NUMERIC "${ARESTRIX_VERSION}")
project(Arestrix VERSION ${ARESTRIX_VERSION_NUMERIC} LANGUAGES C CXX)

message(STATUS "Arestrix ${ARESTRIX_VERSION}")

# ---------------------------------------------------------------------------
# Metadados de versao dos executaveis Windows (ProductName/ProductVersion/
# FileVersion). A SignPath Foundation exige esses atributos nos binarios
# assinados; o Inno Setup tambem usa o mesmo esquema.
# ---------------------------------------------------------------------------
if(WIN32)
  set(ARESTRIX_VERSION_RC_COMMA
    "${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},0")
  function(arestrix_add_version_info target description original_name internal_name)
    set(ARESTRIX_FILE_DESCRIPTION "${description}")
    set(ARESTRIX_ORIGINAL_FILENAME "${original_name}")
    set(ARESTRIX_INTERNAL_NAME "${internal_name}")
    set(version_rc "${CMAKE_CURRENT_BINARY_DIR}/generated/${target}-version.rc")
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/resources/version.rc.in"
      "${version_rc}"
      @ONLY
    )
    target_sources(${target} PRIVATE "${version_rc}")
  endfunction()
endif()


# ---------------------------------------------------------------------------
# Runtime local: catalogo de modelos e manifesto de binarios embutidos no binario.
# Os JSONs ficam em CppCore/runtime e sao a fonte de verdade; o header gerado evita
# depender do diretorio de trabalho no aplicativo empacotado.
# ---------------------------------------------------------------------------
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/runtime/models.catalog.json" ARESTRIX_MODEL_CATALOG_JSON)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/runtime/llama.manifest.json" ARESTRIX_LLAMA_MANIFEST_JSON)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/runtime/models.catalog.json"
  "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/runtime/llama.manifest.json")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/CppCore/runtime/LocalRuntimeData.hpp.in"
  "${CMAKE_CURRENT_BINARY_DIR}/generated/LocalRuntimeData.hpp"
  @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

option(ARESTRIX_BUILD_STUDIO "Compila o Studio (executavel com interface WebUI)" ON)
option(ARESTRIX_BUILD_TESTS "Compila os testes do runtime" ON)
option(ARESTRIX_WITH_ONNX "Compressao de prosa via ONNX Runtime (exige ONNXRUNTIME_ROOT)" OFF)

# ---------------------------------------------------------------------------
# Seguranca Corporativa, Hardening e Analise Estatica / Dinamica em C++20
# ---------------------------------------------------------------------------
option(ARESTRIX_ENABLE_HARDENING "Ativa flags de seguranca de memoria e mitigacao de exploracao" ON)
option(ARESTRIX_ENABLE_ASAN "Ativa AddressSanitizer (deteccao de use-after-free e buffer overflows)" OFF)
option(ARESTRIX_ENABLE_UBSAN "Ativa UndefinedBehaviorSanitizer (deteccao de comportamento indefinido)" OFF)
option(ARESTRIX_ENABLE_TSAN "Ativa ThreadSanitizer (deteccao de data races em multithreading)" OFF)
option(ARESTRIX_ENABLE_CLANG_TIDY "Ativa analise estatica Clang-Tidy integrada ao build" OFF)
option(ARESTRIX_ENABLE_CPPCHECK "Ativa analise estatica Cppcheck integrada ao build" OFF)
option(ARESTRIX_WARNINGS_AS_ERRORS "Trata avisos do compilador como erros fatais (/WX ou -Werror)" OFF)

# Integracao com Clang-Tidy
if(ARESTRIX_ENABLE_CLANG_TIDY)
  find_program(CLANG_TIDY_EXE NAMES clang-tidy
    PATHS
      "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
      "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/Llvm/x64/bin"
      "C:/Program Files/LLVM/bin"
      "/usr/bin"
      "/usr/local/bin")
  if(CLANG_TIDY_EXE)
    message(STATUS "[Seguranca] Clang-Tidy habilitado: ${CLANG_TIDY_EXE}")
    set(CMAKE_CXX_CLANG_TIDY "${CLANG_TIDY_EXE};--config-file=${CMAKE_SOURCE_DIR}/.clang-tidy;--extra-arg=-fcxx-exceptions")
  else()
    message(WARNING "[Seguranca] Clang-Tidy solicitado mas executavel nao encontrado.")
  endif()
endif()

# Integracao com Cppcheck
if(ARESTRIX_ENABLE_CPPCHECK)
  find_program(CPPCHECK_EXE NAMES cppcheck)
  if(CPPCHECK_EXE)
    message(STATUS "[Seguranca] Cppcheck habilitado: ${CPPCHECK_EXE}")
    set(CMAKE_CXX_CPPCHECK "${CPPCHECK_EXE};--enable=warning,performance,portability;--inline-suppr;--suppress=*:*third_party/*;--inconclusive")
  else()
    message(WARNING "[Seguranca] Cppcheck solicitado mas executavel nao encontrado.")
  endif()
endif()

# ---------------------------------------------------------------------------
# Target de Interface com Flags de Seguranca & Hardening Compartilhadas
# ---------------------------------------------------------------------------
add_library(arestrix_security_flags INTERFACE)

if(MSVC)
  target_compile_options(arestrix_security_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:/utf-8 /permissive- /Zc:__cplusplus /EHsc>)
  
  if(ARESTRIX_ENABLE_HARDENING)
    target_compile_options(arestrix_security_flags INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:/GS /guard:cf /sdl /W4>)
    target_link_options(arestrix_security_flags INTERFACE
      /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT /guard:cf)
  else()
    target_compile_options(arestrix_security_flags INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:/W3>)
  endif()

  if(ARESTRIX_WARNINGS_AS_ERRORS)
    target_compile_options(arestrix_security_flags INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:/WX>)
  endif()

  if(ARESTRIX_ENABLE_ASAN)
    message(STATUS "[Seguranca] AddressSanitizer (MSVC) ativado.")
    target_compile_options(arestrix_security_flags INTERFACE /fsanitize=address)
  endif()

  target_compile_definitions(arestrix_security_flags INTERFACE _CRT_SECURE_NO_WARNINGS)

else()
  # GCC / Clang
  target_compile_options(arestrix_security_flags INTERFACE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wpedantic -Wno-unused-parameter>)

  if(ARESTRIX_ENABLE_HARDENING)
    target_compile_options(arestrix_security_flags INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:-fstack-protector-strong -D_FORTIFY_SOURCE=2 -Wformat=2 -Wformat-security>)
    target_link_options(arestrix_security_flags INTERFACE
      -Wl,-z,relro,-z,now -Wl,-z,noexecstack)
  endif()

  if(ARESTRIX_WARNINGS_AS_ERRORS)
    target_compile_options(arestrix_security_flags INTERFACE
      $<$<COMPILE_LANGUAGE:CXX>:-Werror>)
  endif()

  if(ARESTRIX_ENABLE_ASAN)
    message(STATUS "[Seguranca] AddressSanitizer (GCC/Clang) ativado.")
    target_compile_options(arestrix_security_flags INTERFACE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(arestrix_security_flags INTERFACE -fsanitize=address)
  endif()

  if(ARESTRIX_ENABLE_UBSAN)
    message(STATUS "[Seguranca] UndefinedBehaviorSanitizer ativado.")
    target_compile_options(arestrix_security_flags INTERFACE -fsanitize=undefined)
    target_link_options(arestrix_security_flags INTERFACE -fsanitize=undefined)
  endif()

  if(ARESTRIX_ENABLE_TSAN)
    message(STATUS "[Seguranca] ThreadSanitizer ativado.")
    target_compile_options(arestrix_security_flags INTERFACE -fsanitize=thread)
    target_link_options(arestrix_security_flags INTERFACE -fsanitize=thread)
  endif()
endif()

set(TP "${CMAKE_SOURCE_DIR}/third_party")
set(SRC "${CMAKE_SOURCE_DIR}/CppCore/src")

foreach(dep taskflow/taskflow/taskflow.hpp simdjson/singleheader/simdjson.cpp)
  if(NOT EXISTS "${TP}/${dep}")
    message(FATAL_ERROR "Submodulo ausente: ${dep}. Execute: git submodule update --init third_party/taskflow third_party/simdjson")
  endif()
endforeach()

# ---------------------------------------------------------------------------
# arestrix_gateway_core: persistencia e regras de budget do AI Gateway.
# ---------------------------------------------------------------------------
add_library(arestrix_gateway_core STATIC
  ${SRC}/gateway/GatewayTypes.cpp
  ${SRC}/gateway/GatewayStore.cpp)
target_include_directories(arestrix_gateway_core PUBLIC
  ${SRC}
  ${TP}/sqlite
  ${TP}/simdjson/singleheader)
target_link_libraries(arestrix_gateway_core PUBLIC arestrix_core arestrix_security_flags Threads::Threads)

# ---------------------------------------------------------------------------
# arestrix_core: parsing JSON (simdjson), processos, sandbox, memoria, MCP e runtime dos agentes.
# E a biblioteca que os executaveis gerados a partir de um grafo linkam.
# ---------------------------------------------------------------------------
add_library(arestrix_core STATIC
  ${SRC}/util/Json.cpp
  ${SRC}/platform/Process.cpp
  ${SRC}/sandbox/HostProcessDriver.cpp
  ${SRC}/sandbox/DockerSandboxDriver.cpp
  ${SRC}/sandbox/MicroVMSandboxDriver.cpp
  ${SRC}/sandbox/SandboxManager.cpp
  ${SRC}/sandbox/PipelineResumeEngine.cpp
  ${SRC}/memory/MemorySanitizer.cpp
  ${SRC}/memory/MemoryExtractor.cpp
  ${SRC}/memory/EmbeddingProvider.cpp
  ${SRC}/memory/MemoryVectorIndex.cpp
  ${SRC}/memory/MemoryIndexer.cpp
  ${SRC}/memory/MemoryRanker.cpp
  ${SRC}/memory/MemoryConsolidator.cpp
  ${SRC}/memory/MemoryPlaybookCompiler.cpp
  ${SRC}/memory/MemoryInstitutionService.cpp
  ${SRC}/memory/MemoryInstitutionApi.cpp
  ${SRC}/memory/MemoryStore.cpp
  ${SRC}/memory/MemoryService.cpp
  ${SRC}/memory/MemoryStateSchema.cpp
  ${SRC}/memory/MemoryPredicates.cpp
  ${SRC}/memory/MemoryAbProtocol.cpp
  ${SRC}/project/OpenableFiles.cpp
  ${SRC}/libraries/GitHubSource.cpp
  ${SRC}/project/GitSyncService.cpp
  ${SRC}/project/MemoryDatabaseGuard.cpp
  ${SRC}/project/RunnerService.cpp
  ${SRC}/project/CorporateVault.cpp
  ${SRC}/project/RunnerSecurity.cpp
  ${SRC}/project/RunnerPackaging.cpp
  ${SRC}/search/ProjectSearch.cpp
  ${SRC}/search/Synonyms.cpp
  ${SRC}/search/SynonymAgent.cpp
  ${SRC}/mcp/McpClient.cpp
  ${SRC}/mcp/McpErrors.cpp
  ${SRC}/mcp/McpRegistry.cpp
  ${SRC}/rt/Http.cpp
  ${SRC}/rt/NetGuard.cpp
  ${SRC}/rt/RuntimeLang.cpp
  ${SRC}/rt/Tools.cpp
  ${SRC}/rt/ArtifactValidation.cpp
  ${SRC}/rt/Agent.cpp
  ${SRC}/rt/CallGraph.cpp
  ${SRC}/rt/AsyncCalls.cpp
  ${SRC}/rt/Interaction.cpp
  ${SRC}/rt/Notification.cpp
  ${SRC}/rt/Trace.cpp
  ${SRC}/rt/ModelClient.cpp
  ${SRC}/rt/compress/ContentRouter.cpp
  ${SRC}/rt/compress/SmartCrusher.cpp
  ${SRC}/rt/compress/LogCompressor.cpp
  ${SRC}/rt/compress/CodeCompressor.cpp
  ${SRC}/rt/compress/CcrStore.cpp
  ${SRC}/rt/compress/ProseTokenizer.cpp
  ${SRC}/rt/compress/ProseModel.cpp
  ${SRC}/rt/compress/ProseOnnxModel.cpp
  ${SRC}/rt/compress/ProseCompressor.cpp
  ${SRC}/rt/compress/ProseRecommendation.cpp
  ${SRC}/rt/compress/ProseModelInstaller.cpp
  ${SRC}/rt/compress/ContextCompressor.cpp
  ${SRC}/local/LocalRuntime.cpp
  ${SRC}/local/Download.cpp
  ${SRC}/assistant/GraphDocument.cpp
  ${SRC}/assistant/AssistantTools.cpp
  ${SRC}/assistant/AssistantSession.cpp
  ${SRC}/rt/Decision.cpp
  ${SRC}/rt/McpTools.cpp
  ${SRC}/rt/NodeInput.cpp
  ${SRC}/rt/McpVertex.cpp
  ${SRC}/native/NativeTools.cpp
  ${SRC}/native/NativeFiles.cpp
  ${SRC}/native/NativeHttp.cpp
  ${SRC}/native/NativeData.cpp
  ${SRC}/native/NativeMisc.cpp
  ${SRC}/native/NativeSqlite.cpp
  ${TP}/sqlite/sqlite3.c
  ${TP}/simdjson/singleheader/simdjson.cpp)

target_include_directories(arestrix_core PUBLIC
  ${CMAKE_CURRENT_BINARY_DIR}/generated
  ${SRC}
  ${TP}/sqlite
  ${TP}/simdjson/singleheader
  ${TP}/taskflow)

# ---------------------------------------------------------------------------
# ONNX Runtime (opcional): compressao de prosa (Kompress-v2-base).
# Desligado por padrao; ligue com -DARESTRIX_WITH_ONNX=ON e
# -DONNXRUNTIME_ROOT=<pasta com include/ e lib/> (ou a variavel de ambiente).
# O runtime de execucao precisa do onnxruntime.dll ao lado do executavel.
# ---------------------------------------------------------------------------
if(ARESTRIX_WITH_ONNX)
  set(ONNXRUNTIME_ROOT "" CACHE PATH "Raiz do ONNX Runtime (include/ e lib/ ou pacote NuGet)")
  if(NOT ONNXRUNTIME_ROOT AND DEFINED ENV{ONNXRUNTIME_ROOT})
    set(ONNXRUNTIME_ROOT "$ENV{ONNXRUNTIME_ROOT}")
  endif()
  set(ARESTRIX_ONNX_INCLUDE "")
  foreach(ARESTRIX_ONNX_CANDIDATE
      "${ONNXRUNTIME_ROOT}/include"
      "${ONNXRUNTIME_ROOT}/build/native/include")
    if(EXISTS "${ARESTRIX_ONNX_CANDIDATE}/onnxruntime_cxx_api.h")
      set(ARESTRIX_ONNX_INCLUDE "${ARESTRIX_ONNX_CANDIDATE}")
    endif()
  endforeach()
  if(NOT ARESTRIX_ONNX_INCLUDE)
    message(FATAL_ERROR
      "ARESTRIX_WITH_ONNX=ON exige ONNXRUNTIME_ROOT (include/onnxruntime_cxx_api.h). "
      "Baixe o pacote do ONNX Runtime e aponte a variavel.")
  endif()
  find_library(ARESTRIX_ONNXRUNTIME_LIB NAMES onnxruntime
    PATHS
      "${ONNXRUNTIME_ROOT}/lib"
      "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native"
      "${ONNXRUNTIME_ROOT}/runtimes/linux-x64/native"
    NO_DEFAULT_PATH)
  if(NOT ARESTRIX_ONNXRUNTIME_LIB)
    message(FATAL_ERROR "onnxruntime.lib/.so nao encontrado em ${ONNXRUNTIME_ROOT}.")
  endif()
  target_include_directories(arestrix_core PUBLIC "${ARESTRIX_ONNX_INCLUDE}")
  target_compile_definitions(arestrix_core PUBLIC ARESTRIX_WITH_ONNX=1)
  target_link_libraries(arestrix_core PUBLIC "${ARESTRIX_ONNXRUNTIME_LIB}")
  message(STATUS "ONNX Runtime habilitado: ${ONNXRUNTIME_ROOT} (lembre-se de copiar onnxruntime.dll)")
endif()

# DirectML (opcional, Windows): usa a GPU dedicada quando ela for mais rapida que o CPU.
# Exige o pacote Microsoft.ML.OnnxRuntime.DirectML (1.24+) e DirectML.dll ao lado do executavel.
option(ARESTRIX_WITH_ONNX_DML "Usa DirectML (GPU) quando disponivel (Windows)" OFF)
if(ARESTRIX_WITH_ONNX_DML)
  if(NOT ARESTRIX_WITH_ONNX)
    message(FATAL_ERROR "ARESTRIX_WITH_ONNX_DML=ON exige ARESTRIX_WITH_ONNX=ON.")
  endif()
  if(NOT WIN32)
    message(FATAL_ERROR "ARESTRIX_WITH_ONNX_DML so existe no Windows.")
  endif()
  target_compile_definitions(arestrix_core PUBLIC ARESTRIX_WITH_ONNX_DML=1)
  message(STATUS "DirectML habilitado: copie DirectML.dll e onnxruntime_providers_shared.dll ao lado do executavel")
endif()

# SQLite (dominio publico), compilado dentro de arestrix_core: sem carga de extensoes, sem aspas duplas como texto,
# chaves estrangeiras ligadas por padrao. Os executaveis gerados linkam so arestrix_core, entao ele precisa vir junto.
set_source_files_properties(${TP}/sqlite/sqlite3.c PROPERTIES
  COMPILE_DEFINITIONS "SQLITE_OMIT_LOAD_EXTENSION;SQLITE_DQS=0;SQLITE_THREADSAFE=1;SQLITE_DEFAULT_FOREIGN_KEYS=1;SQLITE_OMIT_DEPRECATED;SQLITE_OMIT_SHARED_CACHE;SQLITE_USE_URI=0;SQLITE_DEFAULT_MEMSTATUS=0;SQLITE_ENABLE_FTS5")
if(MSVC)
  set_source_files_properties(${TP}/sqlite/sqlite3.c ${TP}/simdjson/singleheader/simdjson.cpp PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(${TP}/sqlite/sqlite3.c ${TP}/simdjson/singleheader/simdjson.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()

find_package(Threads REQUIRED)
target_compile_definitions(arestrix_core PRIVATE ARESTRIX_MEMORY_VERSION="${ARESTRIX_VERSION}")
target_link_libraries(arestrix_core PUBLIC arestrix_security_flags Threads::Threads)
if(WIN32)
  target_link_libraries(arestrix_core PUBLIC winhttp ws2_32 advapi32 ole32 shell32 user32 dxgi)
endif()

# Os executaveis gerados procuram a biblioteca em <raiz>/runtime/lib.
add_custom_command(TARGET arestrix_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/runtime/lib"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix_core>" "${CMAKE_SOURCE_DIR}/runtime/lib/"
  VERBATIM)

# ---------------------------------------------------------------------------
# arestrix-cli: gera, compila e executa grafos sem interface (CI, servidores)
# ---------------------------------------------------------------------------
add_executable(arestrix-cli
  ${SRC}/cli/cli_main.cpp
  ${SRC}/cli/Cli.cpp
  ${SRC}/core/Graph.cpp
  ${SRC}/compiler/TaskflowGenerator.cpp
  ${SRC}/build/BuildService.cpp)
target_link_libraries(arestrix-cli PRIVATE arestrix_core arestrix_security_flags)
target_compile_definitions(arestrix-cli PRIVATE ARESTRIX_CLI_VERSION="${ARESTRIX_VERSION}")
if(WIN32)
  arestrix_add_version_info(arestrix-cli "Arestrix CLI" "arestrix-cli.exe" "arestrix-cli")
endif()
add_custom_command(TARGET arestrix-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix-cli>" "${CMAKE_SOURCE_DIR}/"
  VERBATIM)

# AI Gateway on-prem: executavel separado, OpenAI/Anthropic-compatible, com budgets.
add_executable(arestrix-gateway
  ${SRC}/gateway/main.cpp
  ${SRC}/gateway/GatewayServer.cpp)
target_include_directories(arestrix-gateway PRIVATE
  ${SRC}
  ${TP}/httplib)
target_compile_definitions(arestrix-gateway PRIVATE ARESTRIX_GATEWAY_VERSION="${ARESTRIX_VERSION}")
if(WIN32)
  arestrix_add_version_info(arestrix-gateway "Arestrix AI Gateway" "arestrix-gateway.exe" "arestrix-gateway")
endif()
target_link_libraries(arestrix-gateway PRIVATE arestrix_gateway_core arestrix_core arestrix_security_flags)
add_custom_command(TARGET arestrix-gateway POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix-gateway>" "${CMAKE_SOURCE_DIR}/"
  VERBATIM)

# Servico institucional de memoria: API HTTP autenticada + MCP stdio.
add_executable(arestrix-memory
  ${SRC}/memory/institution_main.cpp
  ${SRC}/memory/MemoryInstitutionServer.cpp)
target_include_directories(arestrix-memory PRIVATE
  ${SRC}
  ${TP}/httplib)
target_compile_definitions(arestrix-memory PRIVATE ARESTRIX_MEMORY_VERSION="${ARESTRIX_VERSION}")
if(WIN32)
  arestrix_add_version_info(arestrix-memory "Arestrix Memory Service" "arestrix-memory.exe" "arestrix-memory")
endif()
target_link_libraries(arestrix-memory PRIVATE arestrix_core arestrix_security_flags)
add_custom_command(TARGET arestrix-memory POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix-memory>" "${CMAKE_SOURCE_DIR}/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Studio
# ---------------------------------------------------------------------------
if(ARESTRIX_BUILD_STUDIO)
  # FastWebUI (MIT, fork do WebUI com API identica) + CivetWeb (MIT): compilados a partir do submodulo, sem binarios baixados.
  set(WEBUI_DIR "${TP}/FastWebUI")
  if(NOT EXISTS "${WEBUI_DIR}/src/webui.c")
    message(FATAL_ERROR "Submodulo ausente: third_party/FastWebUI. Execute: git submodule update --init third_party/FastWebUI")
  endif()
  set(WEBUI_SOURCES ${WEBUI_DIR}/src/webui.c ${WEBUI_DIR}/src/civetweb/civetweb.c)
  if(WIN32)
    list(APPEND WEBUI_SOURCES ${WEBUI_DIR}/src/webview/win32_wv2.cpp)
  endif()
  add_library(webui STATIC ${WEBUI_SOURCES})
  target_include_directories(webui PUBLIC ${WEBUI_DIR}/include)
  target_include_directories(webui PRIVATE ${WEBUI_DIR}/src ${WEBUI_DIR}/src/civetweb)
  target_compile_definitions(webui PRIVATE NDEBUG NO_CACHING NO_CGI USE_WEBSOCKET NO_SSL)
  if(WIN32)
    target_link_libraries(webui PUBLIC user32 advapi32 shell32 ole32 ws2_32)
    if(NOT MSVC)
      target_link_libraries(webui PUBLIC uuid)
    endif()
    target_compile_definitions(webui PRIVATE _CRT_SECURE_NO_WARNINGS)
    if(MSVC)
      target_compile_options(webui PRIVATE /w)
    else()
      target_compile_options(webui PRIVATE -w)
    endif()
  else()
    target_compile_options(webui PRIVATE -w)
    target_link_libraries(webui PUBLIC Threads::Threads ${CMAKE_DL_LIBS} m)
  endif()

  add_executable(arestrix
    ${SRC}/main.cpp
    ${SRC}/core/Graph.cpp
    ${SRC}/compiler/TaskflowGenerator.cpp
    ${SRC}/build/BuildService.cpp
    ${SRC}/cli/Cli.cpp
    ${SRC}/project/ProjectManager.cpp
    ${SRC}/pi/PiProjectScanner.cpp
    ${SRC}/platform/StudioConsole.cpp)
  target_link_libraries(arestrix PRIVATE arestrix_core arestrix_security_flags webui)
  target_compile_definitions(arestrix PRIVATE ARESTRIX_STUDIO_VERSION="${ARESTRIX_VERSION}")
  if(WIN32)
    arestrix_add_version_info(arestrix "Arestrix Studio" "arestrix.exe" "arestrix")
  endif()
  # O Studio continua como aplicativo de console porque o FastWebUI/WebUI usa o console
  # do processo. Quando iniciado fora de um terminal, platform/StudioConsole.cpp oculta
  # a janela de console e espelha os logs em %LOCALAPPDATA%/Arestrix/logs.
  if(UNIX)
    target_link_libraries(arestrix PRIVATE ${CMAKE_DL_LIBS} m)
  endif()

  add_custom_command(TARGET arestrix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix>" "${CMAKE_SOURCE_DIR}/"
    VERBATIM)
endif()

# ---------------------------------------------------------------------------
# Testes (sem dependencia externa): rodam via ctest.
# ---------------------------------------------------------------------------
if(ARESTRIX_BUILD_TESTS)
  enable_testing()
  add_executable(arestrix_tests
    ${CMAKE_SOURCE_DIR}/CppCore/tests/main.cpp
    ${SRC}/core/Graph.cpp
    ${SRC}/compiler/TaskflowGenerator.cpp
    ${SRC}/build/BuildService.cpp
    ${SRC}/project/ProjectManager.cpp)
  target_link_libraries(arestrix_tests PRIVATE arestrix_gateway_core arestrix_core arestrix_security_flags)
  add_test(NAME arestrix_tests COMMAND arestrix_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# ---------------------------------------------------------------------------
# ONNX Runtime: os executaveis linkam onnxruntime.lib, entao a DLL precisa
# estar ao lado deles para rodar (inclusive os testes). O mesmo conjunto e
# empacotado pelo instalador. Rode scripts/fetch_onnxruntime.py para obter a
# raiz normalizada (include/, lib/, bin/).
# ---------------------------------------------------------------------------
if(ARESTRIX_WITH_ONNX AND WIN32)
  set(ARESTRIX_ONNX_BIN_DIR "${ONNXRUNTIME_ROOT}/bin")
  if(NOT EXISTS "${ARESTRIX_ONNX_BIN_DIR}/onnxruntime.dll")
    set(ARESTRIX_ONNX_BIN_DIR "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native")
  endif()
  if(NOT EXISTS "${ARESTRIX_ONNX_BIN_DIR}/onnxruntime.dll")
    message(FATAL_ERROR
      "onnxruntime.dll nao encontrado em ${ONNXRUNTIME_ROOT}/bin nem em "
      "${ONNXRUNTIME_ROOT}/runtimes/win-x64/native. Rode scripts/fetch_onnxruntime.py.")
  endif()

  set(ARESTRIX_ONNX_DLLS "${ARESTRIX_ONNX_BIN_DIR}/onnxruntime.dll")
  if(EXISTS "${ARESTRIX_ONNX_BIN_DIR}/onnxruntime_providers_shared.dll")
    list(APPEND ARESTRIX_ONNX_DLLS "${ARESTRIX_ONNX_BIN_DIR}/onnxruntime_providers_shared.dll")
  endif()
  if(ARESTRIX_WITH_ONNX_DML)
    set(ARESTRIX_DIRECTML_BIN_DIR "${ONNXRUNTIME_ROOT}/bin" CACHE PATH "Pasta com DirectML.dll")
    if(NOT EXISTS "${ARESTRIX_DIRECTML_BIN_DIR}/DirectML.dll")
      message(FATAL_ERROR
        "DirectML.dll nao encontrado em ${ARESTRIX_DIRECTML_BIN_DIR}. Rode scripts/fetch_onnxruntime.py.")
    endif()
    list(APPEND ARESTRIX_ONNX_DLLS "${ARESTRIX_DIRECTML_BIN_DIR}/DirectML.dll")
  endif()

  foreach(ARESTRIX_ONNX_TARGET arestrix arestrix-cli arestrix-gateway arestrix-memory arestrix_tests)
    if(TARGET ${ARESTRIX_ONNX_TARGET})
      add_custom_command(TARGET ${ARESTRIX_ONNX_TARGET} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ARESTRIX_ONNX_DLLS}
                "$<TARGET_FILE_DIR:${ARESTRIX_ONNX_TARGET}>"
        VERBATIM)
    endif()
  endforeach()
endif()
