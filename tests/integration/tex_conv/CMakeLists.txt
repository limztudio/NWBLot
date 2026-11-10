if(Python3_Interpreter_FOUND AND TARGET nwb_assets_texture_cook)
    nwb_declare_executable(nwb_tex_conv_prepared_decode)
    nwb_target_adhd_sources(nwb_tex_conv_prepared_decode NAMESPACE "NWB::Tests::TextureDecodeCases" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
    target_compile_definitions(nwb_tex_conv_prepared_decode PRIVATE NWB_COOK=1)
    target_link_libraries(nwb_tex_conv_prepared_decode PRIVATE nwb_assets_texture_loader nwb_assets_texture_cook nwb_common nwb_alloc nwb_logclient nwb::basis_universal)
    add_test(NAME nwb_tex_conv_prepared_textures COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_tex_conv.py" --tex-conv "$<TARGET_FILE:nwb_tex_conv>" --prepared-decode "$<TARGET_FILE:nwb_tex_conv_prepared_decode>"
            --output-dir "${PROJECT_BINARY_DIR}/tests/integration/tex_conv/$<CONFIG>")
endif()
