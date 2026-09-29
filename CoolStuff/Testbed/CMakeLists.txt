nwb_declare_executable(testbed)
target_sources(testbed PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/ui_slider_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_slider_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_radio_group_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_radio_group_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_area_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_text_area_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_controls.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_skin_preview.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_skin_preview.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_widget_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_widget_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_edit_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_edit_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_combo_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_search_combo_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_search_combo_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_search_combo_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_search_combo_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_tools_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_tools_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_tools_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_tools_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_nested_popup_source.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_nested_popup_source.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_nested_popup_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_nested_popup_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_numeric_edit_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_numeric_edit_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_combo_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_list_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_list_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_gallery.h"
    "${CMAKE_CURRENT_LIST_DIR}/ui_popup_gallery.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project.h"
)
set_target_properties(testbed PROPERTIES
    DEBUGGER_WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
    WIN32_EXECUTABLE ON
    VS_DEBUGGER_WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
)
nwb_target_link_libraries_whole_archive(testbed
    nwb_assets_mesh
    nwb_assets_skeleton
    nwb_assets_model
    nwb_assets_material
    nwb_assets_shader
    nwb_assets_texture
    nwb_assets_ui_skin
    nwb_assets_font
    nwb_assets_font_atlas
    nwb_assets_sampler
)
target_link_libraries(testbed PRIVATE
    nwb_common
    nwb_alloc
    nwb_loader
    nwb_ecs_scene
    nwb_ecs_mesh
    nwb_ecs_skeleton
    nwb_ecs_model
    nwb_ecs_mesh_skinning
    nwb_ecs_render
    nwb_ecs_model_renderer
    nwb_ecs_ui
    nwb_assets_graphics
    nwb_ui_widgets
)

# Place the NVIDIA Aftermath runtime next to testbed so the device-side dynamic load resolves it.
nwb_copy_aftermath_runtime(testbed)

if(TARGET nwb_logserver)
    add_dependencies(testbed nwb_logserver)
endif()

if(TARGET nwb_asset_builder)
    # Object-cache paths include long content hashes. Keep the root compact so Windows
    # builds remain below the legacy MAX_PATH limit.
    set(_nwb_testbed_asset_cache_directory "${PROJECT_SOURCE_DIR}/__build_obj/c/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}")

    file(GLOB_RECURSE _nwb_testbed_asset_inputs
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${PROJECT_SOURCE_DIR}/impl/assets/*"
        "${PROJECT_SOURCE_DIR}/CoolStuff/Testbed/assets/*"
    )

    set(_nwb_testbed_asset_stamp "${NWB_OUTPUT_ROOT}/res/.nwb_testbed_assets_$<CONFIG>.stamp")
    add_custom_command(
        OUTPUT "${_nwb_testbed_asset_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NWB_OUTPUT_ROOT}/res"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/pipeline/launch.py" --skip-build
            --tool-directory "$<TARGET_FILE_DIR:nwb_asset_builder>"
            --repo-root "${PROJECT_SOURCE_DIR}"
            --asset-root "impl/assets"
            --asset-root "CoolStuff/Testbed/assets"
            --output-directory "${NWB_OUTPUT_ROOT}/res"
            --cache-directory "${_nwb_testbed_asset_cache_directory}"
            --configuration "$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_testbed_asset_stamp}"
        DEPENDS
            nwb_dependeny_computer nwb_asset_builder nwb_asset_gatherer
            "${PROJECT_SOURCE_DIR}/pipeline/launch.py"
            ${_nwb_testbed_asset_inputs}
        WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
        COMMENT "Cooking NWB graphics assets for $<CONFIG>..."
        VERBATIM
        USES_TERMINAL
    )
    add_custom_target(nwb_testbed_assets DEPENDS "${_nwb_testbed_asset_stamp}")
    add_dependencies(testbed nwb_testbed_assets)
    unset(_nwb_testbed_asset_stamp)
    unset(_nwb_testbed_asset_inputs)
    unset(_nwb_testbed_asset_cache_directory)
endif()

