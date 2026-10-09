include_guard(GLOBAL)

function(nwb_configure_name_symbols)
    if(NOT NWB_BUILDMODE)
        return()
    endif()

    add_compile_definitions(NWB_BUILDMODE=1 NWB_BUILD_SYMBOLS=1)
endfunction()
