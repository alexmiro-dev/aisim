# infrastructure:: composition root + main(). The OUTERMOST layer: it wires
# everything, so it is the one place allowed to depend on all the others.
add_library(aisim_app)
target_sources(aisim_app
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            composition_root.cppm)
target_link_libraries(aisim_app
    PUBLIC aisim_adapter aisim_application aisim_domain)
target_compile_options(aisim_app PRIVATE -Wall -Wextra -Wpedantic)

# The aisim executable: a thin main() over the composition root.
add_executable(aisim main.cpp)
target_link_libraries(aisim PRIVATE aisim_app)
target_compile_options(aisim PRIVATE -Wall -Wextra -Wpedantic)
