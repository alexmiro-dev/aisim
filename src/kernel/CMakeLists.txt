# kernel:: — shared kernel (cross-cutting seams). Depends on NOTHING, like
# domain::. Inner layers link this to reach seams (e.g. ILogger) without ever
# naming technology.
add_library(aisim_kernel)
target_sources(aisim_kernel
    PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            kernel.cppm)
# Intentionally NO target_link_libraries: the kernel depends on nothing.
target_compile_options(aisim_kernel PRIVATE -Wall -Wextra -Wpedantic)
