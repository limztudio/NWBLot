if(TARGET nwb::rapidjson)
    return()
endif()

add_library(nwb_rapidjson INTERFACE)
add_library(nwb::rapidjson ALIAS nwb_rapidjson)
target_include_directories(nwb_rapidjson SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
