option(NWB_BUILD_UI_INPUT_BENCHMARK "Build opt-in public UI input CPU benchmark" OFF)

if(NWB_BUILD_UI_INPUT_BENCHMARK)
    nwb_declare_executable(nwb_ui_input_benchmark)
    target_sources(nwb_ui_input_benchmark PRIVATE "${CMAKE_CURRENT_LIST_DIR}/ui_input/namespace.h" "${CMAKE_CURRENT_LIST_DIR}/ui_input/benchmark.h" "${CMAKE_CURRENT_LIST_DIR}/ui_input/entry.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/ui_input/pointer.cpp" "${CMAKE_CURRENT_LIST_DIR}/ui_input/paint.cpp" "${CMAKE_CURRENT_LIST_DIR}/ui_input/editor.cpp")
    target_link_libraries(nwb_ui_input_benchmark PRIVATE nwb_ui_widgets nwb_ecs_ui nwb_ui nwb_common nwb_alloc)
endif()
