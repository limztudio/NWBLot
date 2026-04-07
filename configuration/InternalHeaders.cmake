include_guard(GLOBAL)

function(nwb_configure_internal_headers)
    if(TARGET nwb_internal_headers)
        return()
    endif()

    add_library(nwb_internal_headers INTERFACE)
    target_link_libraries(nwb_internal_headers INTERFACE nwb::tbb_headers)
endfunction()
