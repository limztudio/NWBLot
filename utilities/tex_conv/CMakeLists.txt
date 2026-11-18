nwb_declare_executable(nwb_tex_conv)
nwb_target_adhd_sources(nwb_tex_conv NAMESPACE "NWB::TexConv" SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/*.adhd"
)
target_sources(nwb_tex_conv PRIVATE "${CMAKE_CURRENT_LIST_DIR}/basis_encoder.cpp")
target_link_libraries(nwb_tex_conv PUBLIC nwb_utilities_global PRIVATE nwb_common nwb_alloc nwb_logclient nwb::basis_universal nwb_tool_cli)

nwb_target_console_entry(nwb_tex_conv "utilities/tex_conv/main.adhd.h" "NWB::TexConv::RunConsole" "tex_conv" "NWB::TexConv::s_TexConvExitFatal")
