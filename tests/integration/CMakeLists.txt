if(TARGET nwb_crash)
    add_subdirectory(crash)
endif()

add_subdirectory(logger_server)

if(TARGET nwb_fbx_to_nwb)
    add_subdirectory(fbx_to_nwb)
endif()

if(TARGET nwb_tex_conv)
    add_subdirectory(tex_conv)
endif()

if(TARGET nwb_assets_volume_cook)
    add_subdirectory(assets_graphics)
endif()
