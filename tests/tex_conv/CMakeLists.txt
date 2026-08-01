add_test(
    NAME nwb_tex_conv_uastc_mips
    COMMAND
        "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/verify_tex_conv.py"
        --tex-conv "$<TARGET_FILE:nwb_tex_conv>"
        --output-dir "${PROJECT_BINARY_DIR}/tests/tex_conv/$<CONFIG>"
)
