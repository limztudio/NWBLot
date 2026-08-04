nwb_declare_gtest_executable(nwb_rgd_decode_smoke)
target_sources(nwb_rgd_decode_smoke PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/main.cpp"
)
target_link_libraries(nwb_rgd_decode_smoke PRIVATE nwb::rgd_backend)
