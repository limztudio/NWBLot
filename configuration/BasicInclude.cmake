include_guard(GLOBAL)

function(nwb_apply_basic_include target)
    target_include_directories(${target} PUBLIC
        "${PROJECT_SOURCE_DIR}"
        "${PROJECT_SOURCE_DIR}/global"
    )
endfunction()
