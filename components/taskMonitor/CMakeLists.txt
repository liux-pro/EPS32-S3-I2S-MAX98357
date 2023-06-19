idf_component_register(SRCS taskMonitor.h table.c
                    INCLUDE_DIRS .)
target_compile_options(${COMPONENT_LIB} PRIVATE -w)