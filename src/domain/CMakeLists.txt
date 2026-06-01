# domain:: — pure core. Depends on NOTHING (no other layer, no technology).
# The empty link interface below is the architecture's load-bearing invariant
# made physical: if a domain file ever imports an adapter, it won't link.
add_library(aisim_domain)
target_sources(aisim_domain
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            domain.cppm)
# Intentionally NO target_link_libraries: domain depends on nothing.
target_compile_options(aisim_domain PRIVATE -Wall -Wextra -Wpedantic)
