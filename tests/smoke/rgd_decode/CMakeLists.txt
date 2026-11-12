nwb_declare_gtest_executable(nwb_rgd_decode_smoke)
nwb_target_adhd_sources(nwb_rgd_decode_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
target_link_libraries(nwb_rgd_decode_smoke PRIVATE nwb_logrgd)
