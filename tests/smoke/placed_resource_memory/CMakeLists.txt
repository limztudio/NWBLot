# Standalone validation-backed coverage for placed host-visible virtual buffer memory.
nwb_declare_gtest_executable(nwb_placed_resource_memory_tests)
target_sources(nwb_placed_resource_memory_tests PRIVATE
    "${CMAKE_SOURCE_DIR}/tests/common/vulkan_test_sync.h"
    "${CMAKE_CURRENT_LIST_DIR}/buffer_gpu_readiness_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_ir_backend_alias_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_ir_backend_operand_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_list_provenance_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_list_submission_provenance_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/device_object_ownership_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/permanent_state_lifetime_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/placed_resource_memory_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/staging_texture_contract_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/staging_texture_provenance_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_accel_gpu_readiness_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_clear_staging_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_gpu_readiness_state_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_gpu_readiness_submission_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_native_identity_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_native_provenance_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_transfer_atomicity_tests.cpp"
)
target_link_libraries(nwb_placed_resource_memory_tests PRIVATE
    nwb_graphics
    nwb_perf
    nwb_alloc
    nwb_common
    nwb_assets_graphics
)
