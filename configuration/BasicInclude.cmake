include_guard(GLOBAL)

function(nwb_apply_basic_include target)
    target_include_directories(${target} PRIVATE
        "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}>"
    )
endfunction()
