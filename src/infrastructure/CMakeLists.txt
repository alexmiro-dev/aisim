# infrastructure:: composition root + main(). The OUTERMOST layer: it wires
# everything, so it is the one place allowed to depend on all the others.
add_library(aisim_infrastructure)
target_sources(aisim_infrastructure
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            concurrency.cppm
            logger.cppm
            composition_root.cppm
    PRIVATE
        # Module implementation unit (the .cpp analog): definitions only, not
        # part of the interface, so it is an ordinary source — never in the
        # CXX_MODULES file-set.
        logger.cpp)
target_link_libraries(aisim_infrastructure
    PUBLIC aisim_adapter aisim_application aisim_domain aisim_kernel)
target_compile_options(aisim_infrastructure PRIVATE -Wall -Wextra -Wpedantic)

# The aisim executable: a thin main() over the composition root.
add_executable(aisim main.cpp)
target_link_libraries(aisim PRIVATE aisim_infrastructure)
target_compile_options(aisim PRIVATE -Wall -Wextra -Wpedantic)
