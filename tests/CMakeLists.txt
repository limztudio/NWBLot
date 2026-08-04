# Declare a GoogleTest-based test executable: wires the engine application entry point + common init
# (common/gtest_nwb_main.cpp), links the vendored gtest, and registers the binary with ctest. Suites can still add
# their own sources / dependencies afterwards (e.g. add_dependencies for a child-process helper binary).
function(nwb_declare_gtest_executable target)
    nwb_declare_executable(${target})
    target_sources(${target} PRIVATE "${CMAKE_SOURCE_DIR}/tests/common/gtest_nwb_main.cpp")
    # gtest, plus the engine runtime the shared entry point needs (Core::Common::InitializerGuard and the test
    # arenas) — this also puts oneTBB's import-lib directory on the link path for its auto-link pragma.
    target_link_libraries(${target} PRIVATE nwb::gtest nwb_common nwb_alloc)
    add_test(NAME ${target} COMMAND ${target})
endfunction()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(smoke)
