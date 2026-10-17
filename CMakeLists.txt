cmake_minimum_required(VERSION 3.20)
project(Arestrix LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Tipo de build" FORCE)
endif()

option(ARESTRIX_BUILD_STUDIO "Compila o Studio (executavel com interface WebUI)" ON)
option(ARESTRIX_BUILD_TESTS "Compila os testes do runtime" ON)

set(TP "${CMAKE_SOURCE_DIR}/third_party")
set(SRC "${CMAKE_SOURCE_DIR}/CppCore/src")

foreach(dep taskflow/taskflow/taskflow.hpp simdjson/singleheader/simdjson.cpp)
  if(NOT EXISTS "${TP}/${dep}")
    message(FATAL_ERROR "Submodulo ausente: ${dep}. Execute: git submodule update --init third_party/taskflow third_party/simdjson")
  endif()
endforeach()

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
  ${SRC}/memory/AgentBlackboard.cpp
  ${SRC}/memory/SemanticMemoryEngine.cpp
  ${SRC}/project/OpenableFiles.cpp
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
  ${SRC}/rt/Agent.cpp
  ${SRC}/rt/ModelClient.cpp
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
  ${SRC}
  ${TP}/sqlite
  ${TP}/simdjson/singleheader
  ${TP}/taskflow)

# SQLite (dominio publico), compilado dentro de arestrix_core: sem carga de extensoes, sem aspas duplas como texto,
# chaves estrangeiras ligadas por padrao. Os executaveis gerados linkam so arestrix_core, entao ele precisa vir junto.
set_source_files_properties(${TP}/sqlite/sqlite3.c PROPERTIES
  COMPILE_DEFINITIONS "SQLITE_OMIT_LOAD_EXTENSION;SQLITE_DQS=0;SQLITE_THREADSAFE=1;SQLITE_DEFAULT_FOREIGN_KEYS=1;SQLITE_OMIT_DEPRECATED;SQLITE_OMIT_SHARED_CACHE;SQLITE_USE_URI=0;SQLITE_DEFAULT_MEMSTATUS=0")
if(MSVC)
  set_source_files_properties(${TP}/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(${TP}/sqlite/sqlite3.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

find_package(Threads REQUIRED)
target_link_libraries(arestrix_core PUBLIC Threads::Threads)
if(WIN32)
  target_link_libraries(arestrix_core PUBLIC winhttp ws2_32 advapi32 ole32 shell32 user32)
endif()

if(MSVC)
  target_compile_options(arestrix_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W3 /utf-8 /permissive- /Zc:__cplusplus /EHsc>)
  target_compile_definitions(arestrix_core PUBLIC _CRT_SECURE_NO_WARNINGS)
else()
  target_compile_options(arestrix_core PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -Wno-unused-parameter>)
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
target_link_libraries(arestrix-cli PRIVATE arestrix_core)
if(MSVC)
  target_compile_options(arestrix-cli PRIVATE /W3 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
endif()
add_custom_command(TARGET arestrix-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:arestrix-cli>" "${CMAKE_SOURCE_DIR}/"
  VERBATIM)

# ---------------------------------------------------------------------------
# Studio
# ---------------------------------------------------------------------------
if(ARESTRIX_BUILD_STUDIO)
  # WebUI (MIT) + CivetWeb (MIT): compilados a partir do submodulo, sem binarios baixados.
  set(WEBUI_DIR "${TP}/webui")
  if(NOT EXISTS "${WEBUI_DIR}/src/webui.c")
    message(FATAL_ERROR "Submodulo ausente: third_party/webui. Execute: git submodule update --init third_party/webui")
  endif()
  add_library(webui STATIC ${WEBUI_DIR}/src/webui.c ${WEBUI_DIR}/src/civetweb/civetweb.c)
  target_include_directories(webui PUBLIC ${WEBUI_DIR}/include)
  target_include_directories(webui PRIVATE ${WEBUI_DIR}/src/civetweb)
  target_compile_definitions(webui PRIVATE NDEBUG NO_CACHING NO_CGI USE_WEBSOCKET NO_SSL)
  if(WIN32)
    target_link_libraries(webui PUBLIC user32 advapi32 shell32 ole32 ws2_32)
    target_compile_definitions(webui PRIVATE _CRT_SECURE_NO_WARNINGS)
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
    ${SRC}/pi/PiProjectScanner.cpp)
  target_link_libraries(arestrix PRIVATE arestrix_core webui)
  if(UNIX)
    target_link_libraries(arestrix PRIVATE ${CMAKE_DL_LIBS} m)
  endif()
  if(MSVC)
    target_compile_options(arestrix PRIVATE /W3 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
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
    ${SRC}/build/BuildService.cpp)
  target_link_libraries(arestrix_tests PRIVATE arestrix_core)
  if(MSVC)
    target_compile_options(arestrix_tests PRIVATE /W3 /utf-8 /permissive- /Zc:__cplusplus /EHsc)
  endif()
  add_test(NAME arestrix_tests COMMAND arestrix_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
