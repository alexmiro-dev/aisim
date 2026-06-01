# adapter:: — driving + driven adapters. Depends on application:: and domain::.
# Technology dependencies (CURL, SQLite, a WS lib, …) get linked PRIVATE here as
# concrete adapters are added, so they never leak past this layer.
add_library(aisim_adapter)
target_sources(aisim_adapter
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            adapter.cppm)
target_link_libraries(aisim_adapter PUBLIC aisim_application aisim_domain)
target_compile_options(aisim_adapter PRIVATE -Wall -Wextra -Wpedantic)
