if(TARGET nwb::tsl_headers)
    return()
endif()

add_library(nwb_tsl_headers INTERFACE)
add_library(nwb::tsl_headers ALIAS nwb_tsl_headers)
target_include_directories(nwb_tsl_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(nwb_tsl_headers INTERFACE
    _SILENCE_CXX23_ALIGNED_STORAGE_DEPRECATION_WARNING
)
