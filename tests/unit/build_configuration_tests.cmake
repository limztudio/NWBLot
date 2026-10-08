cmake_minimum_required(VERSION 3.25)

if(DEFINED NWB_TEST_CONFIGURATION_CASE)
    if(NWB_TEST_CONFIGURATION_CASE STREQUAL "omitted")
        unset(CMAKE_BUILD_TYPE)
        unset(CMAKE_BUILD_TYPE CACHE)
    else()
        set(CMAKE_BUILD_TYPE "${NWB_TEST_REQUESTED_CONFIGURATION}" CACHE STRING "")
    endif()
    if(NWB_TEST_CONFIGURATION_CASE STREQUAL "multi")
        set(CMAKE_CONFIGURATION_TYPES "Debug;Release;unknown" CACHE STRING "")
    endif()
    include("${NWB_TEST_SOURCE_DIR}/configuration/BuildConfigurations.cmake")
    nwb_configure_build_configs()
    if(NWB_TEST_CONFIGURATION_CASE STREQUAL "multi")
        if(NOT CMAKE_CONFIGURATION_TYPES STREQUAL "dbg;opt;fin")
            message(FATAL_ERROR "Multi-config admission did not select the canonical configurations")
        endif()
    elseif(NOT CMAKE_BUILD_TYPE STREQUAL NWB_TEST_EXPECTED_CONFIGURATION)
        message(FATAL_ERROR "Single-config admission changed the requested canonical configuration")
    endif()
    message(STATUS "configuration-boundary-admitted")
    return()
endif()

function(nwb_check_configuration_admission case_name configuration expected_configuration accepted)
    execute_process(
        COMMAND "${CMAKE_COMMAND}"
            "-DNWB_TEST_SOURCE_DIR=${NWB_TEST_SOURCE_DIR}"
            "-DNWB_TEST_CONFIGURATION_CASE=${case_name}"
            "-DNWB_TEST_REQUESTED_CONFIGURATION=${configuration}"
            "-DNWB_TEST_EXPECTED_CONFIGURATION=${expected_configuration}"
            -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(accepted)
        if(NOT result EQUAL 0 OR NOT stdout MATCHES "configuration-boundary-admitted")
            message(FATAL_ERROR "Canonical configuration control '${case_name}' failed:\n${stdout}\n${stderr}")
        endif()
    else()
        if(result EQUAL 0)
            message(FATAL_ERROR "Retired or invalid configuration '${configuration}' was accepted")
        endif()
        string(REGEX REPLACE "[ \t\r\n]+" " " diagnostic "${stderr}")
        string(FIND "${diagnostic}" "Unsupported CMAKE_BUILD_TYPE '${configuration}'" diagnostic_position)
        if(diagnostic_position EQUAL -1 OR NOT diagnostic MATCHES "Expected exactly dbg, opt, or fin")
            message(FATAL_ERROR "Rejected configuration '${configuration}' omitted its canonical diagnostic:\n${stdout}\n${stderr}")
        endif()
    endif()
endfunction()

# Native CMake configuration names and case variants must fail before losing canonical code-generation settings.
foreach(configuration IN ITEMS Debug Release RelWithDebInfo MinSizeRel unknown DBG Opt FIN OFF 0 " dbg")
    nwb_check_configuration_admission("invalid" "${configuration}" "" FALSE)
endforeach()
foreach(configuration IN ITEMS dbg opt fin)
    nwb_check_configuration_admission("canonical" "${configuration}" "${configuration}" TRUE)
endforeach()
nwb_check_configuration_admission("omitted" "" "dbg" TRUE)
nwb_check_configuration_admission("empty" "" "dbg" TRUE)
nwb_check_configuration_admission("multi" "Release" "" TRUE)
