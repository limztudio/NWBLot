add_test(NAME nwb_ui_layer_input_bindings_smoke COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/input_bindings_smoke.py" --executable "$<TARGET_FILE:nwb_ui_layer_smoke>" --working-directory "${_nwb_ui_smoke_runtime_root}"
        --output-directory "${CMAKE_BINARY_DIR}/Testing/smoke/$<CONFIG>/ui_layer_input_bindings" ${_nwb_ui_smoke_capture_args})
get_property(_nwb_ui_input_bindings_environment TEST nwb_ui_layer_edit_smoke PROPERTY ENVIRONMENT)
set_tests_properties(nwb_ui_layer_input_bindings_smoke PROPERTIES RESOURCE_LOCK ${NWB_TEST_DISPLAY_RESOURCE_LOCK} SKIP_RETURN_CODE ${NWB_TEST_SKIP_RETURN_CODE} TIMEOUT ${_nwb_ui_smoke_default_ctest_timeout_seconds} LABELS "ui;standalone_gpu;native_input;edit;input_bindings" ENVIRONMENT "${_nwb_ui_input_bindings_environment}")
unset(_nwb_ui_input_bindings_environment)
