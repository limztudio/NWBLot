nwb_declare_static_library(nwb_pipeline_command_line)
target_sources(nwb_pipeline_command_line PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/command_line.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/command_line.h"
)
target_link_libraries(nwb_pipeline_command_line PUBLIC nwb_assets_cook nwb_common nwb::cli11_headers)
target_compile_definitions(nwb_pipeline_command_line PRIVATE NWB_COOK=1)

add_subdirectory(dependency_computer)
add_subdirectory(asset_builder)
add_subdirectory(asset_gatherer)
add_subdirectory(adhd_compiler)

add_custom_target(nwb_pipeline DEPENDS nwb_dependency_computer nwb_asset_builder nwb_asset_gatherer nwb_adhd_compiler)

function(nwb_target_adhd_sources target_name)
    set(out_dir "${CMAKE_CURRENT_BINARY_DIR}/adhd_gen/${target_name}")
    file(MAKE_DIRECTORY "${out_dir}")
    target_include_directories(${target_name} PUBLIC "${out_dir}")

    foreach(adhd_file IN LISTS ARGN)
        get_filename_component(adhd_abs "${adhd_file}" ABSOLUTE)
        get_filename_component(adhd_stem "${adhd_file}" NAME_WE)
        set(gen_h "${out_dir}/${adhd_stem}.adhd.h")
        set(gen_cpp "${out_dir}/${adhd_stem}.adhd.cpp")

        add_custom_command(
            OUTPUT "${gen_h}" "${gen_cpp}"
            COMMAND $<TARGET_FILE:nwb_adhd_compiler> --input "${adhd_abs}" -o "${out_dir}" -I "${CMAKE_CURRENT_LIST_DIR}"
            DEPENDS nwb_adhd_compiler "${adhd_abs}"
            COMMENT "Compiling adhd source ${adhd_file}"
            VERBATIM
        )
        target_sources(${target_name} PRIVATE "${gen_h}" "${gen_cpp}")
    endforeach()
endfunction()
