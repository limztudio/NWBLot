if(NOT TARGET nwb_loader OR NOT TARGET nwb_ecs_ui OR NOT TARGET nwb_asset_builder OR NOT Python3_Interpreter_FOUND)
    return()
endif()

set(_nwb_ui_smoke_runtime_root "${CMAKE_BINARY_DIR}/Testing/ui_layer_runtime/$<CONFIG>")

nwb_declare_executable(nwb_ui_layer_smoke)
target_sources(nwb_ui_layer_smoke PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/edit_selection_probe.h"
    "${CMAKE_CURRENT_LIST_DIR}/edit_selection_probe.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_image_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/texture_image_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/texture_image_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/texture_image_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/image_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/image_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/image_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/image_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/progress_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/progress_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/progress_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/progress_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/slider_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/slider_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/slider_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/slider_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/radio_group_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_area_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_area_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_area_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_area_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project.h"
    "${CMAKE_CURRENT_LIST_DIR}/world.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/world.h"
    "${CMAKE_CURRENT_LIST_DIR}/paint_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/paint_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/text_samples.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/text_samples.h"
    "${CMAKE_CURRENT_LIST_DIR}/interactive_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/interactive_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/window_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/window_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/edit_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/edit_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/popup_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/popup_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/combo_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/combo_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/combo_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/combo_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/search_combo_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/search_combo_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/search_combo_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/search_combo_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/popup_tools_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/popup_tools_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/popup_tools_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/popup_tools_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/nested_popup_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/nested_popup_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/nested_popup_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/nested_popup_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/numeric_edit_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/numeric_edit_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/numeric_edit_snapshot.h"
    "${CMAKE_CURRENT_LIST_DIR}/numeric_edit_snapshot.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/list_scene.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/list_scene.h"
    "${CMAKE_CURRENT_LIST_DIR}/list_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/list_source.h"
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
    nwb_assets_font_atlas
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

file(GLOB_RECURSE _nwb_ui_smoke_asset_inputs CONFIGURE_DEPENDS LIST_DIRECTORIES false
    "${PROJECT_SOURCE_DIR}/impl/assets/*"
    "${CMAKE_CURRENT_LIST_DIR}/assets/*"
)
set(_nwb_ui_smoke_asset_stamp "${_nwb_ui_smoke_runtime_root}/res/.nwb_ui_layer_assets_$<CONFIG>.stamp")
add_custom_command(
    OUTPUT "${_nwb_ui_smoke_asset_stamp}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${_nwb_ui_smoke_runtime_root}/res"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
        --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
        --repo-root "${PROJECT_SOURCE_DIR}"
        --asset-root "impl/assets"
        --asset-root "tests/smoke/ui_layer/assets"
        --output-directory "${_nwb_ui_smoke_runtime_root}/res"
        --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/c/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
        --configuration "$<CONFIG>"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_ui_smoke_asset_stamp}"
    DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer
        "${PROJECT_SOURCE_DIR}/pipeline/launch.py" ${_nwb_ui_smoke_asset_inputs}
    WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
    COMMENT "Cooking standalone UI smoke engine and fixture assets for $<CONFIG>..."
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
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

add_test(NAME nwb_ui_layer_interaction_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/interaction_smoke.py"
        --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
        --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_interaction"
        ${_nwb_ui_smoke_logserver_args}
)
set_tests_properties(nwb_ui_layer_interaction_smoke PROPERTIES
    RESOURCE_LOCK nwb_display
    SKIP_RETURN_CODE 77
    TIMEOUT 150
    LABELS "ui;standalone_gpu;native_input"
    ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=1;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
)

add_test(NAME nwb_ui_layer_window_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/window_smoke.py"
        --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
        --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_window"
        ${_nwb_ui_smoke_logserver_args}
)
set_tests_properties(nwb_ui_layer_window_smoke PROPERTIES
    RESOURCE_LOCK nwb_display
    SKIP_RETURN_CODE 77
    TIMEOUT 150
    LABELS "ui;standalone_gpu;native_input;window"
    ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=1;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
)

add_test(NAME nwb_ui_layer_window_skin_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/window_smoke.py"
        --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
        --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_window_skin"
        --skin alternate
        ${_nwb_ui_smoke_logserver_args}
)
set_tests_properties(nwb_ui_layer_window_skin_smoke PROPERTIES
    RESOURCE_LOCK nwb_display
    SKIP_RETURN_CODE 77
    TIMEOUT 150
    LABELS "ui;standalone_gpu;native_input;window;skin"
    ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=1;NWB_UI_LAYER_WINDOW_SKIN=1;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
)

add_test(NAME nwb_ui_layer_edit_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/edit_smoke.py"
        --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
        --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_edit"
        ${_nwb_ui_smoke_logserver_args}
)
set_tests_properties(nwb_ui_layer_edit_smoke PROPERTIES
    RESOURCE_LOCK nwb_display
    SKIP_RETURN_CODE 77
    TIMEOUT 150
    LABELS "ui;standalone_gpu;native_input;edit"
    ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=1;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
)

include("${CMAKE_CURRENT_LIST_DIR}/input_bindings_smoke.cmake")

foreach(_nwb_ui_popup_skin IN ITEMS default alternate)
    if(_nwb_ui_popup_skin STREQUAL "alternate")
        set(_nwb_ui_popup_test "nwb_ui_layer_popup_skin_smoke")
        set(_nwb_ui_popup_skin_flag 1)
    else()
        set(_nwb_ui_popup_test "nwb_ui_layer_popup_smoke")
        set(_nwb_ui_popup_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_popup_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/popup_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_popup_${_nwb_ui_popup_skin}"
            --skin "${_nwb_ui_popup_skin}"
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_popup_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 150
        LABELS "ui;standalone_gpu;native_input;popup;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=1;NWB_UI_LAYER_POPUP_SKIN=${_nwb_ui_popup_skin_flag};NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_list_skin IN ITEMS default alternate)
    if(_nwb_ui_list_skin STREQUAL "alternate")
        set(_nwb_ui_list_test "nwb_ui_layer_list_skin_smoke")
        set(_nwb_ui_list_skin_flag 1)
    else()
        set(_nwb_ui_list_test "nwb_ui_layer_list_smoke")
        set(_nwb_ui_list_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_list_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/list_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_list_${_nwb_ui_list_skin}"
            --skin "${_nwb_ui_list_skin}"
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_list_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 150
        LABELS "ui;standalone_gpu;native_input;list;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=1;NWB_UI_LAYER_LIST_SKIN=${_nwb_ui_list_skin_flag};NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_combo_skin IN ITEMS default alternate)
    if(_nwb_ui_combo_skin STREQUAL "alternate")
        set(_nwb_ui_combo_test "nwb_ui_layer_combo_skin_smoke")
        set(_nwb_ui_combo_skin_flag 1)
    else()
        set(_nwb_ui_combo_test "nwb_ui_layer_combo_smoke")
        set(_nwb_ui_combo_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_combo_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/combo_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_combo_${_nwb_ui_combo_skin}"
            --skin "${_nwb_ui_combo_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_combo_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;combo;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=1;NWB_UI_LAYER_COMBO_SKIN=${_nwb_ui_combo_skin_flag};NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_search_combo_skin IN ITEMS default alternate)
    if(_nwb_ui_search_combo_skin STREQUAL "alternate")
        set(_nwb_ui_search_combo_test "nwb_ui_layer_search_combo_skin_smoke")
        set(_nwb_ui_search_combo_skin_flag 1)
    else()
        set(_nwb_ui_search_combo_test "nwb_ui_layer_search_combo_smoke")
        set(_nwb_ui_search_combo_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_search_combo_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/search_combo_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_search_combo_${_nwb_ui_search_combo_skin}"
            --skin "${_nwb_ui_search_combo_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_search_combo_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;search_combo;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=1;NWB_UI_LAYER_SEARCH_COMBO_SKIN=${_nwb_ui_search_combo_skin_flag};NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_popup_tools_skin IN ITEMS default alternate)
    if(_nwb_ui_popup_tools_skin STREQUAL "alternate")
        set(_nwb_ui_popup_tools_test "nwb_ui_layer_popup_tools_skin_smoke")
        set(_nwb_ui_popup_tools_skin_flag 1)
    else()
        set(_nwb_ui_popup_tools_test "nwb_ui_layer_popup_tools_smoke")
        set(_nwb_ui_popup_tools_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_popup_tools_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/popup_tools_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_popup_tools_${_nwb_ui_popup_tools_skin}"
            --skin "${_nwb_ui_popup_tools_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_popup_tools_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;popup_tools;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=1;NWB_UI_LAYER_POPUP_TOOLS_SKIN=${_nwb_ui_popup_tools_skin_flag};NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_nested_popup_skin IN ITEMS default alternate)
    if(_nwb_ui_nested_popup_skin STREQUAL "alternate")
        set(_nwb_ui_nested_popup_test "nwb_ui_layer_nested_popup_skin_smoke")
        set(_nwb_ui_nested_popup_skin_flag 1)
    else()
        set(_nwb_ui_nested_popup_test "nwb_ui_layer_nested_popup_smoke")
        set(_nwb_ui_nested_popup_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_nested_popup_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/nested_popup_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_nested_popup_${_nwb_ui_nested_popup_skin}"
            --skin "${_nwb_ui_nested_popup_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_nested_popup_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;nested_popup;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=1;NWB_UI_LAYER_NESTED_POPUP_SKIN=${_nwb_ui_nested_popup_skin_flag};NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_numeric_edit_skin IN ITEMS default alternate)
    if(_nwb_ui_numeric_edit_skin STREQUAL "alternate")
        set(_nwb_ui_numeric_edit_test "nwb_ui_layer_numeric_edit_skin_smoke")
        set(_nwb_ui_numeric_edit_skin_flag 1)
    else()
        set(_nwb_ui_numeric_edit_test "nwb_ui_layer_numeric_edit_smoke")
        set(_nwb_ui_numeric_edit_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_numeric_edit_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/numeric_edit_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_numeric_edit_${_nwb_ui_numeric_edit_skin}"
            --skin "${_nwb_ui_numeric_edit_skin}"
            --timeout 240
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_numeric_edit_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 300
        LABELS "ui;standalone_gpu;native_input;numeric_edit;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=1;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=${_nwb_ui_numeric_edit_skin_flag};NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_text_area_skin IN ITEMS default alternate)
    if(_nwb_ui_text_area_skin STREQUAL "alternate")
        set(_nwb_ui_text_area_test "nwb_ui_layer_text_area_skin_smoke")
        set(_nwb_ui_text_area_skin_flag 1)
    else()
        set(_nwb_ui_text_area_test "nwb_ui_layer_text_area_smoke")
        set(_nwb_ui_text_area_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_text_area_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/text_area_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_text_area_${_nwb_ui_text_area_skin}"
            --skin "${_nwb_ui_text_area_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_text_area_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;text_area;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_TEXT_AREA=1;NWB_UI_LAYER_TEXT_AREA_SKIN=${_nwb_ui_text_area_skin_flag};NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_radio_group_skin IN ITEMS default alternate)
    if(_nwb_ui_radio_group_skin STREQUAL "alternate")
        set(_nwb_ui_radio_group_test "nwb_ui_layer_radio_group_skin_smoke")
        set(_nwb_ui_radio_group_skin_flag 1)
    else()
        set(_nwb_ui_radio_group_test "nwb_ui_layer_radio_group_smoke")
        set(_nwb_ui_radio_group_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_radio_group_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/radio_group_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_radio_group_${_nwb_ui_radio_group_skin}"
            --skin "${_nwb_ui_radio_group_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_radio_group_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;radio_group;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=1;NWB_UI_LAYER_RADIO_GROUP_SKIN=${_nwb_ui_radio_group_skin_flag};NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_slider_skin IN ITEMS default alternate)
    if(_nwb_ui_slider_skin STREQUAL "alternate")
        set(_nwb_ui_slider_test "nwb_ui_layer_slider_skin_smoke")
        set(_nwb_ui_slider_skin_flag 1)
    else()
        set(_nwb_ui_slider_test "nwb_ui_layer_slider_smoke")
        set(_nwb_ui_slider_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_slider_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/slider_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_slider_${_nwb_ui_slider_skin}"
            --skin "${_nwb_ui_slider_skin}"
            --timeout 180
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_slider_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 240
        LABELS "ui;standalone_gpu;native_input;slider;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_SLIDER=1;NWB_UI_LAYER_SLIDER_SKIN=${_nwb_ui_slider_skin_flag};NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_progress_skin IN ITEMS default alternate)
    if(_nwb_ui_progress_skin STREQUAL "alternate")
        set(_nwb_ui_progress_test "nwb_ui_layer_progress_skin_smoke")
        set(_nwb_ui_progress_skin_flag 1)
    else()
        set(_nwb_ui_progress_test "nwb_ui_layer_progress_smoke")
        set(_nwb_ui_progress_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_progress_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/progress_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_progress_${_nwb_ui_progress_skin}"
            --skin "${_nwb_ui_progress_skin}"
            --timeout 120
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_progress_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        LABELS "ui;standalone_gpu;native_input;progress;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=1;NWB_UI_LAYER_PROGRESS_SKIN=${_nwb_ui_progress_skin_flag};NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_image_skin IN ITEMS default alternate)
    if(_nwb_ui_image_skin STREQUAL "alternate")
        set(_nwb_ui_image_test "nwb_ui_layer_image_skin_smoke")
        set(_nwb_ui_image_skin_flag 1)
    else()
        set(_nwb_ui_image_test "nwb_ui_layer_image_smoke")
        set(_nwb_ui_image_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_image_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/image_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_image_${_nwb_ui_image_skin}"
            --skin "${_nwb_ui_image_skin}"
            --timeout 120
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_image_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        LABELS "ui;standalone_gpu;native_input;image;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=1;NWB_UI_LAYER_IMAGE_SKIN=${_nwb_ui_image_skin_flag};NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=0;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=0"
    )
endforeach()

foreach(_nwb_ui_texture_image_skin IN ITEMS default alternate)
    if(_nwb_ui_texture_image_skin STREQUAL "alternate")
        set(_nwb_ui_texture_image_test "nwb_ui_layer_texture_image_skin_smoke")
        set(_nwb_ui_texture_image_skin_flag 1)
    else()
        set(_nwb_ui_texture_image_test "nwb_ui_layer_texture_image_smoke")
        set(_nwb_ui_texture_image_skin_flag 0)
    endif()
    add_test(NAME "${_nwb_ui_texture_image_test}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/texture_image_smoke.py"
            --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
            --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_texture_image_${_nwb_ui_texture_image_skin}"
            --skin "${_nwb_ui_texture_image_skin}"
            --timeout 120
            ${_nwb_ui_smoke_logserver_args}
    )
    set_tests_properties("${_nwb_ui_texture_image_test}" PROPERTIES
        RESOURCE_LOCK nwb_display
        SKIP_RETURN_CODE 77
        TIMEOUT 180
        LABELS "ui;standalone_gpu;native_input;texture_image;skin"
        ENVIRONMENT "NWB_UI_LAYER_IMAGE=0;NWB_UI_LAYER_IMAGE_SKIN=0;NWB_UI_LAYER_PROGRESS=0;NWB_UI_LAYER_PROGRESS_SKIN=0;NWB_UI_LAYER_RADIO_GROUP=0;NWB_UI_LAYER_RADIO_GROUP_SKIN=0;NWB_UI_LAYER_SLIDER=0;NWB_UI_LAYER_SLIDER_SKIN=0;NWB_UI_LAYER_TEXT_AREA=0;NWB_UI_LAYER_TEXT_AREA_SKIN=0;NWB_UI_LAYER_NUMERIC_EDIT=0;NWB_UI_LAYER_NUMERIC_EDIT_SKIN=0;NWB_UI_LAYER_INTERACTIVE=0;NWB_UI_LAYER_WINDOW=0;NWB_UI_LAYER_WINDOW_SKIN=0;NWB_UI_LAYER_EDIT=0;NWB_UI_LAYER_POPUP=0;NWB_UI_LAYER_POPUP_SKIN=0;NWB_UI_LAYER_LIST=0;NWB_UI_LAYER_LIST_SKIN=0;NWB_UI_LAYER_COMBO=0;NWB_UI_LAYER_COMBO_SKIN=0;NWB_UI_LAYER_SEARCH_COMBO=0;NWB_UI_LAYER_SEARCH_COMBO_SKIN=0;NWB_UI_LAYER_POPUP_TOOLS=0;NWB_UI_LAYER_POPUP_TOOLS_SKIN=0;NWB_UI_LAYER_NESTED_POPUP=0;NWB_UI_LAYER_NESTED_POPUP_SKIN=0;NWB_UI_LAYER_TEXTURE_IMAGE=1;NWB_UI_LAYER_TEXTURE_IMAGE_SKIN=${_nwb_ui_texture_image_skin_flag}"
    )
endforeach()

add_test(NAME nwb_ui_layer_raster_ir_parity_smoke
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/raster_ir_parity_smoke.py"
        --executable "$<TARGET_FILE:nwb_ui_layer_smoke>"
        --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_raster_ir_parity"
        --replay-marker "UiGpuRenderer: raster command IR replay active"
        ${_nwb_ui_smoke_logserver_args}
)
set_tests_properties(nwb_ui_layer_raster_ir_parity_smoke PROPERTIES
    RESOURCE_LOCK nwb_display
    SKIP_RETURN_CODE 77
    TIMEOUT 720
    LABELS "ui;standalone_gpu;command_ir"
)

unset(_nwb_ui_texture_image_skin)
unset(_nwb_ui_texture_image_test)
unset(_nwb_ui_texture_image_skin_flag)

unset(_nwb_ui_image_skin)
unset(_nwb_ui_image_test)
unset(_nwb_ui_image_skin_flag)

unset(_nwb_ui_progress_skin)
unset(_nwb_ui_progress_test)
unset(_nwb_ui_progress_skin_flag)

unset(_nwb_ui_slider_skin)
unset(_nwb_ui_slider_test)
unset(_nwb_ui_slider_skin_flag)

unset(_nwb_ui_radio_group_skin)
unset(_nwb_ui_radio_group_test)
unset(_nwb_ui_radio_group_skin_flag)

unset(_nwb_ui_text_area_skin)
unset(_nwb_ui_text_area_test)
unset(_nwb_ui_text_area_skin_flag)

unset(_nwb_ui_smoke_runtime_root)
unset(_nwb_ui_smoke_logserver_args)
unset(_nwb_ui_smoke_asset_inputs)
unset(_nwb_ui_smoke_asset_stamp)
unset(_nwb_ui_smoke_test)
unset(_nwb_ui_smoke_mode)
unset(_nwb_ui_popup_skin)
unset(_nwb_ui_popup_test)
unset(_nwb_ui_popup_skin_flag)
unset(_nwb_ui_list_skin)
unset(_nwb_ui_list_test)
unset(_nwb_ui_list_skin_flag)
unset(_nwb_ui_combo_skin)
unset(_nwb_ui_combo_test)
unset(_nwb_ui_combo_skin_flag)
unset(_nwb_ui_search_combo_skin)
unset(_nwb_ui_search_combo_test)
unset(_nwb_ui_search_combo_skin_flag)
unset(_nwb_ui_popup_tools_skin)
unset(_nwb_ui_popup_tools_test)
unset(_nwb_ui_popup_tools_skin_flag)
unset(_nwb_ui_nested_popup_skin)
unset(_nwb_ui_nested_popup_test)
unset(_nwb_ui_nested_popup_skin_flag)
unset(_nwb_ui_numeric_edit_skin)
unset(_nwb_ui_numeric_edit_test)
unset(_nwb_ui_numeric_edit_skin_flag)
