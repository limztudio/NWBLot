if(TARGET nwb::vma_headers)
    return()
endif()

add_library(nwb_vma_headers INTERFACE)
add_library(nwb::vma_headers ALIAS nwb_vma_headers)
target_include_directories(nwb_vma_headers SYSTEM INTERFACE "${CMAKE_CURRENT_LIST_DIR}/include")
