# application:: — use cases + driving ports. Depends on domain:: ONLY.
add_library(aisim_application)
target_sources(aisim_application
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            application.cppm)
target_link_libraries(aisim_application PUBLIC aisim_domain)
target_compile_options(aisim_application PRIVATE -Wall -Wextra -Wpedantic)
