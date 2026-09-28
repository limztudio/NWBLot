nwb_declare_gtest_executable(nwb_os_tests)
target_sources(nwb_os_tests PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_async_tests.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/clipboard_text_tests.cpp"
)
target_link_libraries(nwb_os_tests PRIVATE nwb_os)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(nwb_os_tests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/pipe_io_tests.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/x11_clipboard_tests.cpp"
    )
    target_link_libraries(nwb_os_tests PRIVATE nwb::x11)

    find_program(NWB_XVFB_RUN_EXECUTABLE xvfb-run)
    if(NWB_XVFB_RUN_EXECUTABLE)
        add_test(NAME nwb_os_x11_clipboard_isolated
            COMMAND "${CMAKE_COMMAND}" -E env NWB_X11_CLIPBOARD_TEST_ISOLATED=1
                "${NWB_XVFB_RUN_EXECUTABLE}" -a -s "-screen 0 640x480x24"
                "$<TARGET_FILE:nwb_os_tests>" "--gtest_filter=X11ClipboardFixture.*"
        )
        set_tests_properties(nwb_os_x11_clipboard_isolated PROPERTIES
            TIMEOUT 30
            RUN_SERIAL TRUE
        )
    endif()
endif()
