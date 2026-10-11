include_guard(GLOBAL)

function(nwb_configure_x11_include)
    if(TARGET nwb::x11)
        return()
    endif()

    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()

    find_package(X11 REQUIRED)

    add_library(nwb_x11 INTERFACE)

    target_link_libraries(nwb_x11 INTERFACE X11::X11)

    add_library(nwb::x11 ALIAS nwb_x11)
endfunction()
