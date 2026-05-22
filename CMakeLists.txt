cmake_minimum_required(VERSION 3.25)
project(agentcpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_CXX_FLAGS_DEBUG   "-g -O0 -DDEBUG")

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
  GIT_TAG        v5.0.0
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json CLI11 ftxui)

find_package(CURL REQUIRED)

# Optional: SQLite enables the SqlQuery tool.
option(AGENTCPP_WITH_SQLITE "Enable embedded SQLite (SqlQuery tool)" ON)
if(AGENTCPP_WITH_SQLITE)
  find_package(SQLite3 QUIET)
  if(SQLite3_FOUND)
    add_compile_definitions(AGENTCPP_WITH_SQLITE)
    message(STATUS "agentcpp: SQLite found - SqlQuery tool enabled")
  else()
    message(WARNING "agentcpp: SQLite not found - SqlQuery tool will report unavailable at runtime")
    set(AGENTCPP_WITH_SQLITE OFF CACHE BOOL "" FORCE)
  endif()
endif()

set(SOURCES
  src/main.cpp
  src/api/Types.cpp
  src/api/Capabilities.cpp
  src/api/ClaudeClient.cpp
  src/agent/QueryEngine.cpp
  src/agent/PlanGraph.cpp
  src/agent/PlannerEngine.cpp
  src/agent/Reflector.cpp
  src/agent/Persona.cpp
  src/agent/SelfBelief.cpp
  src/agent/MetaController.cpp
  src/agent/CoTMonitor.cpp
  src/agent/SelfModelStore.cpp
  src/agent/SchemaReviser.cpp
  src/agent/SelfModelMemoryAdapter.cpp
  src/agent/MetacognitionEngine.cpp
  src/tools/Tool.cpp
  src/tools/BashTool.cpp
  src/tools/FileReadTool.cpp
  src/tools/FileWriteTool.cpp
  src/tools/FileEditTool.cpp
  src/tools/GlobTool.cpp
  src/tools/GrepTool.cpp
  src/tools/SkillTool.cpp
  src/tools/MemoryTool.cpp
  src/tools/WebTool.cpp
  src/tools/DataTool.cpp
  src/tools/DocTool.cpp
  src/skills/SkillRegistry.cpp
  src/memory/MemoryStore.cpp
  src/memory/MemoryTypes.cpp
  src/memory/MemoryStorage.cpp
  src/memory/FactExtractor.cpp
  src/memory/EntityResolver.cpp
  src/memory/BM25Index.cpp
  src/memory/GraphRetrieval.cpp
  src/memory/Fusion.cpp
  src/memory/Reranker.cpp
  src/memory/MemoryEngine.cpp
  src/memory/MemoryProviders.cpp
  src/memory/providers/IEmbedder.cpp
  src/memory/providers/TemplateReflectComposer.cpp
  src/memory/providers/ClaudeFactExtractor.cpp
  src/memory/providers/ClaudeReflectComposer.cpp
  src/memory/providers/HttpEmbedder.cpp
  src/utils/Config.cpp
  src/utils/Logger.cpp
  src/utils/StringUtils.cpp
  src/mcp/McpClient.cpp
  src/mcp/McpManager.cpp
  src/tools/McpTool.cpp
  src/tools/TaskTool.cpp
  src/tools/ChannelTool.cpp
  src/tools/ComputerTool.cpp
  src/channels/ChannelBus.cpp
  src/tui/App.cpp
)

add_executable(agentcpp ${SOURCES})

target_include_directories(agentcpp PRIVATE
  ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(agentcpp PRIVATE
  nlohmann_json::nlohmann_json
  CLI11::CLI11
  ftxui::screen
  ftxui::dom
  ftxui::component
  CURL::libcurl
)

if(AGENTCPP_WITH_SQLITE AND SQLite3_FOUND)
  target_link_libraries(agentcpp PRIVATE SQLite::SQLite3)
endif()

if(APPLE)
  target_link_libraries(agentcpp PRIVATE "-framework CoreFoundation")
endif()

install(TARGETS agentcpp DESTINATION bin)
install(DIRECTORY personas/ DESTINATION share/agentcpp/personas
        FILES_MATCHING PATTERN "*.yaml")
