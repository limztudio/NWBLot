# Opt-in profiling fixture excluded from default CTest runs.
option(NWB_BUILD_RENDERER_GATHER_BENCHMARK "Build the fixed renderer CPU gathering benchmark" OFF)
if(NWB_BUILD_RENDERER_GATHER_BENCHMARK)
    set(_nwb_gather_runtime_root "${CMAKE_BINARY_DIR}/Testing/gather_benchmark_runtime/$<CONFIG>")
    set(_nwb_gather_generated_root "${CMAKE_BINARY_DIR}/Testing/gather_benchmark_generated/$<CONFIG>")
    set(_nwb_gather_asset_stamp "${_nwb_gather_runtime_root}/res/.gather_benchmark_assets.stamp")
    add_custom_command(OUTPUT "${_nwb_gather_asset_stamp}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/generate_gather_benchmark_assets.py" --output-root "${_nwb_gather_generated_root}" --source-root "${CMAKE_CURRENT_LIST_DIR}/assets"
            --mesh-template "${CMAKE_CURRENT_LIST_DIR}/assets/meshes/octahedron.nwb" --surface-template "${CMAKE_CURRENT_LIST_DIR}/gather_benchmark_assets/shaders/gather_benchmark_glass.surface"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>" --repo-root "${PROJECT_SOURCE_DIR}" --asset-root "impl/assets"
            --asset-root "${_nwb_gather_generated_root}" --output-directory "${_nwb_gather_runtime_root}/res" --configuration "$<CONFIG>" COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_gather_asset_stamp}"
        DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer "${CMAKE_CURRENT_LIST_DIR}/generate_gather_benchmark_assets.py" "${CMAKE_CURRENT_LIST_DIR}/gather_benchmark_assets/shaders/gather_benchmark_glass.surface"
            "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_transparent_multi_asset_inputs} WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}" COMMENT "Generating and cooking optional renderer gathering benchmark assets..." VERBATIM USES_TERMINAL)
    add_custom_target(nwb_renderer_gather_benchmark_assets DEPENDS "${_nwb_gather_asset_stamp}")
    nwb_declare_executable(nwb_renderer_gather_benchmark)
    target_sources(nwb_renderer_gather_benchmark PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_project.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_scene.h"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_scene.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_probe.h"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_gather_benchmark_probe.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_compiler_statistics_probe.h"
        "${CMAKE_CURRENT_LIST_DIR}/renderer_compiler_statistics_probe.cpp"
    )
    nwb_configure_runtime_smoke_target(nwb_renderer_gather_benchmark "${_nwb_gather_runtime_root}" nwb_ecs_model nwb_ecs_mesh_skinning)
    add_dependencies(nwb_renderer_gather_benchmark nwb_renderer_gather_benchmark_assets)
endif()

