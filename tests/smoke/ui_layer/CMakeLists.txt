if(NOT TARGET nwb_loader OR NOT TARGET nwb_ecs_ui OR NOT TARGET nwb_asset_builder OR NOT Python3_Interpreter_FOUND)
    return()
endif()

set(_nwb_ui_smoke_runtime_root "${CMAKE_BINARY_DIR}/Testing/ui_layer_runtime/$<CONFIG>")
set(_nwb_ui_smoke_ctest_margin_seconds 120)
set(_nwb_ui_smoke_default_timeout_seconds 90)
math(EXPR _nwb_ui_smoke_default_ctest_timeout_seconds "${_nwb_ui_smoke_default_timeout_seconds} + ${_nwb_ui_smoke_ctest_margin_seconds}")
set(_nwb_ui_layer_fixture_flags INTERACTIVE WINDOW EDIT POPUP POPUP_TOOLS NESTED_POPUP LIST COMBO SEARCH_COMBO NUMERIC_EDIT TEXT_AREA RADIO_GROUP SLIDER PROGRESS IMAGE TEXTURE_IMAGE)

function(nwb_ui_layer_env OUT_VAR)
    cmake_parse_arguments(PARSE_ARGV 1 _arg "" "ACTIVE_FLAG;ACTIVE_SKIN" "")
    set(_env_items)
    foreach(_flag IN LISTS _nwb_ui_layer_fixture_flags)
        if(_arg_ACTIVE_FLAG AND _flag STREQUAL _arg_ACTIVE_FLAG)
            list(APPEND _env_items "NWB_UI_LAYER_${_flag}=1")
        else()
            list(APPEND _env_items "NWB_UI_LAYER_${_flag}=0")
        endif()
        if(NOT _flag STREQUAL "INTERACTIVE" AND NOT _flag STREQUAL "EDIT")
            if(_arg_ACTIVE_FLAG AND _flag STREQUAL _arg_ACTIVE_FLAG AND DEFINED _arg_ACTIVE_SKIN)
                list(APPEND _env_items "NWB_UI_LAYER_${_flag}_SKIN=${_arg_ACTIVE_SKIN}")
            else()
                list(APPEND _env_items "NWB_UI_LAYER_${_flag}_SKIN=0")
            endif()
        endif()
    endforeach()
    set(${OUT_VAR} "${_env_items}" PARENT_SCOPE)
endfunction()

function(nwb_add_ui_layer_skin_smoke_pair FIXTURE_NAME ENV_FLAG TIMEOUT_SECONDS)
    math(EXPR _ctest_timeout "${TIMEOUT_SECONDS} + ${_nwb_ui_smoke_ctest_margin_seconds}")
    foreach(_skin IN ITEMS default alternate)
        if(_skin STREQUAL "alternate")
            set(_test_name "nwb_ui_layer_${FIXTURE_NAME}_skin_smoke")
            set(_skin_flag 1)
        else()
            set(_test_name "nwb_ui_layer_${FIXTURE_NAME}_smoke")
            set(_skin_flag 0)
        endif()
        nwb_ui_layer_env(_test_env ACTIVE_FLAG "${ENV_FLAG}" ACTIVE_SKIN ${_skin_flag})
        add_test(NAME "${_test_name}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/${FIXTURE_NAME}_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
                --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_${FIXTURE_NAME}_${_skin}" --skin "${_skin}" --timeout ${TIMEOUT_SECONDS} ${_nwb_ui_smoke_capture_args})
        set_tests_properties("${_test_name}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_ctest_timeout}
            LABELS "ui;standalone_gpu;native_input;${FIXTURE_NAME};skin" ENVIRONMENT "${_test_env}")
    endforeach()
endfunction()

nwb_declare_executable(nwb_ui_layer_smoke)
nwb_target_adhd_sources(nwb_ui_layer_smoke NAMESPACE "NWB::Tests::Smoke" SOURCES "${CMAKE_CURRENT_LIST_DIR}/*.adhd")
nwb_smoke_project_entry(nwb_ui_layer_smoke "tests/smoke/ui_layer/entry.adhd.h" "NWB::Tests::Smoke")
set_target_properties(nwb_ui_layer_smoke PROPERTIES WIN32_EXECUTABLE ON DEBUGGER_WORKING_DIRECTORY "${_nwb_ui_smoke_runtime_root}" VS_DEBUGGER_WORKING_DIRECTORY "${_nwb_ui_smoke_runtime_root}")
nwb_target_link_libraries_whole_archive(nwb_ui_layer_smoke nwb_assets_shader nwb_assets_texture nwb_assets_ui_skin nwb_assets_font nwb_assets_font_atlas nwb_assets_sampler nwb_assets_subsystem nwb_assets_scene)
target_link_libraries(nwb_ui_layer_smoke PRIVATE nwb_common nwb_alloc nwb_loader nwb_assets_graphics nwb_ecs_ui nwb_ui_widgets nwb_presentation_smoke_types nwb_smoke_runtime_helpers)
if(COMMAND nwb_copy_aftermath_runtime)
    nwb_copy_aftermath_runtime(nwb_ui_layer_smoke)
endif()
if(TARGET nwb_logserver)
    add_dependencies(nwb_ui_layer_smoke nwb_logserver)
endif()
set(_nwb_ui_smoke_capture_args ${_nwb_smoke_optional_logserver_args} "$<IF:$<CONFIG:fin>,--no-gpu-validation,--gpu-validation>")

nwb_add_fixture_smoke_assets(nwb_ui_layer_smoke_assets ui_layer "${_nwb_ui_smoke_runtime_root}" "UI" ASSET_ROOTS "impl/assets" "tests/smoke/ui_layer/assets"
    COMMENT "Cooking standalone UI smoke engine and fixture assets for $<CONFIG>...")
add_dependencies(nwb_ui_layer_smoke nwb_ui_layer_smoke_assets)

nwb_ui_layer_env(_nwb_ui_zero_env)
foreach(_nwb_ui_smoke_mode IN ITEMS framebuffer resize)
    set(_nwb_ui_smoke_test "nwb_ui_layer_${_nwb_ui_smoke_mode}_smoke")
    add_test(NAME "${_nwb_ui_smoke_test}" COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/capture_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
            --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_${_nwb_ui_smoke_mode}" --mode "${_nwb_ui_smoke_mode}" ${_nwb_ui_smoke_capture_args})
    set_tests_properties("${_nwb_ui_smoke_test}" PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu"
        ENVIRONMENT "${_nwb_ui_zero_env}")
endforeach()

add_test(NAME nwb_ui_layer_many_roots_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/capture_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_many_roots" --mode framebuffer --many-roots ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_many_roots_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu")

add_test(NAME nwb_ui_layer_persistent_state_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/capture_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_persistent_state" --mode framebuffer --persistent-state-test ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_persistent_state_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu")

nwb_ui_layer_env(_nwb_ui_interactive_env ACTIVE_FLAG INTERACTIVE)
add_test(NAME nwb_ui_layer_interaction_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/interaction_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_interaction" ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_interaction_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input"
    ENVIRONMENT "${_nwb_ui_interactive_env}")

add_test(NAME nwb_ui_layer_many_roots_interaction_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/interaction_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_many_roots_interaction" --many-roots ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_many_roots_interaction_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input")

set(_nwb_ui_window_timeout_seconds 180)
math(EXPR _nwb_ui_window_ctest_timeout_seconds "${_nwb_ui_window_timeout_seconds} + ${_nwb_ui_smoke_ctest_margin_seconds}")
nwb_ui_layer_env(_nwb_ui_window_default_env ACTIVE_FLAG WINDOW ACTIVE_SKIN 0)
nwb_ui_layer_env(_nwb_ui_window_alt_env ACTIVE_FLAG WINDOW ACTIVE_SKIN 1)

add_test(NAME nwb_ui_layer_window_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/window_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_window" --timeout ${_nwb_ui_window_timeout_seconds} ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_window_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_window_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input;window"
    ENVIRONMENT "${_nwb_ui_window_default_env}")

add_test(NAME nwb_ui_layer_window_skin_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/window_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_window_skin" --skin alternate --timeout ${_nwb_ui_window_timeout_seconds} ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_window_skin_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_window_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input;window;skin"
    ENVIRONMENT "${_nwb_ui_window_alt_env}")

set(_nwb_ui_edit_timeout_seconds 480)
math(EXPR _nwb_ui_edit_ctest_timeout_seconds "${_nwb_ui_edit_timeout_seconds} + ${_nwb_ui_smoke_ctest_margin_seconds}")
nwb_ui_layer_env(_nwb_ui_edit_env ACTIVE_FLAG EDIT)
add_test(NAME nwb_ui_layer_edit_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/edit_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_edit" --timeout ${_nwb_ui_edit_timeout_seconds} ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_edit_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_edit_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input;edit"
    ENVIRONMENT "${_nwb_ui_edit_env}")

include("${CMAKE_CURRENT_LIST_DIR}/input_bindings_smoke.cmake")

nwb_add_ui_layer_skin_smoke_pair(popup POPUP 240)
nwb_add_ui_layer_skin_smoke_pair(list LIST 300)
nwb_add_ui_layer_skin_smoke_pair(combo COMBO 480)
nwb_add_ui_layer_skin_smoke_pair(search_combo SEARCH_COMBO 360)
nwb_add_ui_layer_skin_smoke_pair(popup_tools POPUP_TOOLS 420)
nwb_add_ui_layer_skin_smoke_pair(nested_popup NESTED_POPUP 420)
nwb_add_ui_layer_skin_smoke_pair(numeric_edit NUMERIC_EDIT 720)
nwb_add_ui_layer_skin_smoke_pair(text_area TEXT_AREA 720)
nwb_add_ui_layer_skin_smoke_pair(radio_group RADIO_GROUP 300)
nwb_add_ui_layer_skin_smoke_pair(slider SLIDER 300)
nwb_add_ui_layer_skin_smoke_pair(progress PROGRESS 120)
nwb_add_ui_layer_skin_smoke_pair(image IMAGE 120)
nwb_add_ui_layer_skin_smoke_pair(texture_image TEXTURE_IMAGE 120)

set(_nwb_ui_raster_ir_parity_timeout_seconds 720)
add_test(NAME nwb_ui_layer_raster_ir_parity_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/raster_ir_parity_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_raster_ir_parity" --replay-marker "UiGpuRenderer: raster command IR replay active" ${_nwb_ui_smoke_capture_args})
set_tests_properties(nwb_ui_layer_raster_ir_parity_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_raster_ir_parity_timeout_seconds} LABELS "ui;standalone_gpu;command_ir")

unset(_nwb_ui_raster_ir_parity_timeout_seconds)
unset(_nwb_ui_smoke_runtime_root)
unset(_nwb_ui_smoke_ctest_margin_seconds)
unset(_nwb_ui_smoke_default_timeout_seconds)
unset(_nwb_ui_smoke_default_ctest_timeout_seconds)
unset(_nwb_ui_layer_fixture_flags)
unset(_nwb_ui_smoke_capture_args)
unset(_nwb_ui_zero_env)
unset(_nwb_ui_smoke_test)
unset(_nwb_ui_smoke_mode)
unset(_nwb_ui_interactive_env)
unset(_nwb_ui_window_timeout_seconds)
unset(_nwb_ui_window_ctest_timeout_seconds)
unset(_nwb_ui_window_default_env)
unset(_nwb_ui_window_alt_env)
unset(_nwb_ui_edit_timeout_seconds)
unset(_nwb_ui_edit_ctest_timeout_seconds)
unset(_nwb_ui_edit_env)
