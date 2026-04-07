include_guard(GLOBAL)

function(nwb_apply_unicode_char target)
    if(WIN32)
        target_compile_definitions(${target} PRIVATE UNICODE _UNICODE)
    endif()
endfunction()
