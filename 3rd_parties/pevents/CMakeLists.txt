if(TARGET nwb::pevents_headers)
    return()
endif()

add_library(nwb_pevents_headers INTERFACE)
add_library(nwb::pevents_headers ALIAS nwb_pevents_headers)
target_include_directories(nwb_pevents_headers SYSTEM INTERFACE
    "${CMAKE_CURRENT_LIST_DIR}/.."
    "${CMAKE_CURRENT_LIST_DIR}/src")
