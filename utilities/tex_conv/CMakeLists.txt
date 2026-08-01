nwb_declare_executable(nwb_tex_conv)
target_sources(nwb_tex_conv PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
)
target_link_libraries(nwb_tex_conv PRIVATE
    nwb_common
    nwb_alloc
    nwb::basis_universal
    nwb::cli11_headers
)
