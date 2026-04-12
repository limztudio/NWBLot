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
    nwb_ecs_graphics
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,nwb_assets_graphics>"
    nwb_logclient
)

if(TARGET nwb_logserver)
    add_dependencies(testbed nwb_logserver)
endif()

if(TARGET nwb_resource_cooker)
    add_dependencies(testbed nwb_resource_cooker)
    add_custom_command(TARGET testbed POST_BUILD
        COMMAND "$<TARGET_FILE:nwb_resource_cooker>"
            --repo-root "${PROJECT_SOURCE_DIR}"
            --asset-root "impl/assets"
            --asset-root "Testbed/assets"
            --output-directory "${NWB_OUTPUT_ROOT}/res"
            --cache-directory "${PROJECT_SOURCE_DIR}/__build_obj/shader_cache/${NWB_OUTPUT_PLATFORM}/${NWB_OUTPUT_ARCH}/${NWB_OUTPUT_DOMAIN}/$<CONFIG>"
            --configuration "$<CONFIG>"
        WORKING_DIRECTORY "${NWB_OUTPUT_ROOT}"
        COMMENT "Cooking NWB graphics assets..."
    )
endif()
