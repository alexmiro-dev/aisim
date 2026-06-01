cmake_minimum_required(VERSION 3.28)

project(aisim_ollama_client
    VERSION 0.1.0
    DESCRIPTION "Standalone example: an Ollama client built with C++ modules"
    LANGUAGES CXX)

# --- C++26 (latest standard the toolchain advertises) ------------------------
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Named C++ modules require dependency scanning (CMake 3.28+, Ninja/MSVC gens).
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

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

# --- aisim as a C++ module library -------------------------------------------
# The module interface units live in a FILE_SET of type CXX_MODULES. Both the
# executable and the test suite consume the library via `import aisim;`.
add_library(aisim_lib)
target_sources(aisim_lib
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            src/aisim.cppm
            src/aisim-core.cppm
            src/aisim-ollama.cppm)
target_link_libraries(aisim_lib PUBLIC CURL::libcurl Threads::Threads)
target_compile_definitions(aisim_lib PUBLIC AISIM_MODEL="${AISIM_OLLAMA_MODEL}")
target_compile_options(aisim_lib PRIVATE -Wall -Wextra -Wpedantic)

# --- Executable --------------------------------------------------------------
add_executable(aisim src/main.cpp)
target_link_libraries(aisim PRIVATE aisim_lib)
target_compile_options(aisim PRIVATE -Wall -Wextra -Wpedantic)

# --- Tests (Catch2 v3 via FetchContent) --------------------------------------
include(CTest)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.7.1
        GIT_SHALLOW    ON)
    FetchContent_MakeAvailable(Catch2)

    add_executable(aisim_tests
        tests/json_helpers_test.cpp
        tests/simulation_test.cpp)
    target_link_libraries(aisim_tests PRIVATE aisim_lib Catch2::Catch2WithMain)
    target_compile_options(aisim_tests PRIVATE -Wall -Wextra -Wpedantic)

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(Catch)
    catch_discover_tests(aisim_tests)
endif()
