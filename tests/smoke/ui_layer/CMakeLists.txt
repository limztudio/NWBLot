if(NOT TARGET nwb_loader OR NOT TARGET nwb_ecs_ui OR NOT TARGET nwb_asset_builder OR NOT Python3_Interpreter_FOUND)
    return()
endif()

set(_nwb_ui_smoke_runtime_root "${CMAKE_BINARY_DIR}/Testing/ui_layer_runtime/$<CONFIG>")

nwb_declare_executable(nwb_ui_layer_smoke)
target_sources(nwb_ui_layer_smoke PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project.h"
    "${CMAKE_CURRENT_LIST_DIR}/world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/world.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_samples.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_samples.h"
    "${CMAKE_CURRENT_LIST_DIR}/../framebuffer_capture.cpp"
)
set_target_properties(nwb_ui_layer_smoke PROPERTIES
    WIN32_EXECUTABLE ON
    DEBUGGER_WORKING_DIRECTORY "${_nwb_ui_smoke_runtime_root}"
    VS_DEBUGGER_WORKING_DIRECTORY "${_nwb_ui_smoke_runtime_root}"
)
nwb_target_link_libraries_whole_archive(nwb_ui_layer_smoke
    nwb_assets_shader
    nwb_assets_texture
    nwb_assets_ui_skin
    nwb_assets_font
    nwb_assets_sampler
)
target_link_libraries(nwb_ui_layer_smoke PRIVATE nwb_common nwb_alloc nwb_loader nwb_ecs_ui nwb_ui_widgets)
if(COMMAND nwb_copy_aftermath_runtime)
    nwb_copy_aftermath_runtime(nwb_ui_layer_smoke)
endif()
if(TARGET nwb_logserver)
    add_dependencies(nwb_ui_layer_smoke nwb_logserver)
    set(_nwb_ui_smoke_logserver_args "--logserver-executable" "$<TARGET_FILE:nwb_logserver>")
else()
    set(_nwb_ui_smoke_logserver_args)
endif()

file(GLOB_RECURSE _nwb_ui_smoke_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false "${PROJECT_SOURCE_DIR}/impl/assets/*")
set(_nwb_ui_smoke_asset_stamp "${_nwb_ui_smoke_runtime_root}/res/.nwb_ui_layer_assets_$<CONFIG>.stamp")
add_custom_command(
    OUTPUT "${_nwb_ui_smoke_asset_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_ui_smoke_runtime_root}/res"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
        --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
        --repo-root "${PROJECT_SOURCE_DIR}"
        --asset-root "impl/assets"
        --output-directory "${_nwb_ui_smoke_runtime_root}/res"
        --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/c/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
        --configuration "$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_ui_smoke_asset_stamp}"
    DEPENDS nwb_dependeny_computer nwb_asset_builder nwb_asset_gatherer
        "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_ui_smoke_asset_inputs}
    WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
    COMMENT "Cooking standalone UI smoke engine assets for $<CONFIG>..."
    VERBATIM
    USES_TERMINAL
)
add_custom_target(nwb_ui_layer_smoke_assets DEPENDS "${_nwb_ui_smoke_asset_stamp}")
add_dependencies(nwb_ui_layer_smoke nwb_ui_layer_smoke_assets)

foreach(_nwb_ui_smoke_mode IN ITEMS framebuffer resize)
    set(_nwb_ui_smoke_test "nwb_ui_layer_${_nwb_ui_smoke_mode}_smoke")
    add_test(NAME "${_nwb_ui_smoke_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/capture_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_${_nwb_ui_smoke_mode}"
            --mode "${_nwb_ui_smoke_mode}"
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_smoke_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 150
        LABELS "ui;standalone_gpu"
    )
endforeach()

unset(_nwb_ui_smoke_runtime_root)
unset(_nwb_ui_smoke_logserver_args)
unset(_nwb_ui_smoke_asset_inputs)
unset(_nwb_ui_smoke_asset_stamp)
unset(_nwb_ui_smoke_test)
unset(_nwb_ui_smoke_mode)
