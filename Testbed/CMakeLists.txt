nwb_declare_executable(testbed)
target_sources(testbed PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/project_testbed_entry.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project_testbed_runtime.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/project_testbed.h"
)
set_target_properties(testbed PROPERTIES
    DEBUGGER_WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
    WIN32_EXECUTABLE ON
    VS_DEBUGGER_WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
)
target_link_libraries(testbed PRIVATE
    nwb_loader
    nwb_scene
    nwb_ecs_graphics
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_graphics>"
    nwb_logclient
)

if(TARGET nwb_logserver)
    add_dependencies(testbed nwb_logserver)
endif()

if(TARGET nwb_resource_cooker)
    file(GLOB_RECURSE _nwb_testbed_asset_inputs
        CONFIGURE_DEPENDS
        LIST_DIRECTORIES false
        "${PROJECT_SOURCE_DIR}/impl/assets/*"
        "${PROJECT_SOURCE_DIR}/Testbed/assets/*"
    )

    set(_nwb_testbed_asset_stamp "${NWB_OUTPUT_ROOT}/res/.nwb_testbed_assets_$<CONFIG>.stamp")
    add_custom_command(
        OUTPUT "${_nwb_testbed_asset_stamp}"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${NWB_OUTPUT_ROOT}/res"
        COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
            --repo-root "${PROJECT_SOURCE_DIR}"
            --asset-root "impl/assets"
            --asset-root "Testbed/assets"
            --output-directory "${NWB_OUTPUT_ROOT}/res"
            --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/shader_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}"
            --configuration "$<CONFIG>"
        COMMAND "${CMAKE_COMMAND}" -E touch "${_nwb_testbed_asset_stamp}"
        DEPENDS
            nwb_resource_cooker
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
endif()
