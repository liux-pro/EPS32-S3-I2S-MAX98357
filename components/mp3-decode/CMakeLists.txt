idf_component_register(SRC_DIRS "." "helix" "helix/real"
        INCLUDE_DIRS "." "helix" "helix/pub" "helix/real")
target_compile_options(${COMPONENT_LIB} PRIVATE -w)