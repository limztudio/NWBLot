option(NWB_BUILD_UI_INPUT_BENCHMARK "Build opt-in public UI input CPU benchmark" OFF)

if(NWB_BUILD_UI_INPUT_BENCHMARK)
    nwb_declare_executable(nwb_ui_input_benchmark)
    target_sources(nwb_ui_input_benchmark PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.c"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.cc"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.cxx"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.h"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.hh"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.hxx"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.hpp"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.inl"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.ipp"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/*.ixx"
    )
    target_link_libraries(nwb_ui_input_benchmark PRIVATE nwb_ui_widgets nwb_ecs_ui nwb_ui nwb_common nwb_alloc)
endif()

option(NWB_BUILD_SIMD_BENCHMARKS "Build opt-in SIMD microbenchmarks" OFF)

if(NWB_BUILD_SIMD_BENCHMARKS)
    add_subdirectory(simd_optimization)
endif()
