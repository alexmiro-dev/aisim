cmake_minimum_required(VERSION 3.28)

project(aisim
    VERSION 0.1.0
    DESCRIPTION "AI simulation harness driven by a local LLM"
    LANGUAGES CXX)

# --- C++26 (latest standard the toolchain advertises) ------------------------
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# --- LLM model dependency: qwen2.5-coder:7b (served by Ollama) ---------------
# The model is a runtime dependency provided by Ollama, not a C++ library.
# We discover the Ollama CLI and expose a target that pulls the model, so the
# dependency can be materialised with `cmake --build build --target pull-model`.
# It is intentionally opt-in (network + several GB) and not part of the default
# build.
set(AISIM_OLLAMA_MODEL "qwen2.5-coder:7b" CACHE STRING "Ollama model used by aisim")

find_program(OLLAMA_EXECUTABLE ollama)
if(OLLAMA_EXECUTABLE)
    add_custom_target(pull-model
        COMMAND ${OLLAMA_EXECUTABLE} pull ${AISIM_OLLAMA_MODEL}
        COMMENT "Pulling Ollama model ${AISIM_OLLAMA_MODEL}"
        USES_TERMINAL
        VERBATIM)
else()
    message(WARNING
        "ollama not found on PATH. Install it from https://ollama.com to use "
        "the '${AISIM_OLLAMA_MODEL}' model. The 'pull-model' target is disabled.")
endif()

# --- HTTP client used to talk to the Ollama REST API -------------------------
find_package(CURL REQUIRED)
find_package(Threads REQUIRED)

# --- Executable --------------------------------------------------------------
add_executable(aisim src/main.cpp)
target_link_libraries(aisim PRIVATE CURL::libcurl Threads::Threads)
target_compile_definitions(aisim PRIVATE AISIM_MODEL="${AISIM_OLLAMA_MODEL}")
target_compile_options(aisim PRIVATE -Wall -Wextra -Wpedantic)
