include_guard(GLOBAL)

function(nwb_configure_string_literals)
    # Clang pools read-only literals by default on every target. /GF makes the same policy explicit for clang-cl.
    if(NWB_COMPILER_FRONTEND_MSVC)
        add_compile_options($<$<COMPILE_LANGUAGE:C,CXX>:/GF>)
    endif()

    option(NWB_OBFUSCATE_STRING_LITERALS "Pool and encode compiler string literals, decoding once before static initialization." ON)
    set(NWB_LLVM_C_LIBRARY "" CACHE FILEPATH "Matching compiler-host LLVM C API shared library for string obfuscation.")
    if(NOT NWB_OBFUSCATE_STRING_LITERALS)
        message(STATUS "String literals: Clang pooling enabled; obfuscation disabled")
        return()
    endif()
    if(NOT CMAKE_GENERATOR MATCHES "Ninja|Makefiles")
        message(FATAL_ERROR "String obfuscation requires a Ninja or Makefiles generator that executes compiler and linker launchers.")
    endif()

    find_package(Python3 3.8 REQUIRED COMPONENTS Interpreter)
    set(Python3_EXECUTABLE "${Python3_EXECUTABLE}" PARENT_SCOPE)
    set(_nwb_literal_directory "${PROJECT_SOURCE_DIR}/launcher/string_literals")
    set(_nwb_probe_arguments
        --compiler "${CMAKE_CXX_COMPILER}"
        --c-compiler "${CMAKE_C_COMPILER}"
    )
    if(NWB_LLVM_C_LIBRARY)
        list(APPEND _nwb_probe_arguments --library "${NWB_LLVM_C_LIBRARY}")
    endif()
    execute_process(
        COMMAND "${Python3_EXECUTABLE}" "${_nwb_literal_directory}/configure.py" ${_nwb_probe_arguments}
        RESULT_VARIABLE _nwb_probe_result
        OUTPUT_VARIABLE _nwb_probe_json
        ERROR_VARIABLE _nwb_probe_error
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT _nwb_probe_result EQUAL 0)
        message(FATAL_ERROR "${_nwb_probe_error}")
    endif()
    string(JSON _nwb_literal_library GET "${_nwb_probe_json}" library)
    string(JSON _nwb_literal_version GET "${_nwb_probe_json}" version)
    set(NWB_STRING_LITERAL_LLVM_VERSION "${_nwb_literal_version}" PARENT_SCOPE)
    set(NWB_LLVM_C_LIBRARY "${_nwb_literal_library}" CACHE FILEPATH
        "Matching compiler-host LLVM C API shared library for string obfuscation." FORCE)

    # Include the policy contents in compile commands so editing any stage invalidates existing objects.
    file(GLOB _nwb_literal_scripts CONFIGURE_DEPENDS "${_nwb_literal_directory}/*.py")
    list(APPEND _nwb_literal_scripts "${PROJECT_SOURCE_DIR}/global/string_literal_runtime.cpp")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_nwb_literal_scripts})
    set(_nwb_policy_hashes "")
    foreach(_nwb_literal_script IN LISTS _nwb_literal_scripts)
        file(SHA256 "${_nwb_literal_script}" _nwb_literal_hash)
        string(APPEND _nwb_policy_hashes "${_nwb_literal_hash}")
    endforeach()
    string(SHA256 _nwb_policy_hash "${_nwb_policy_hashes}")
    set(_nwb_literal_launcher
        "${Python3_EXECUTABLE}" "${_nwb_literal_directory}/compiler.py"
        --llvm-library "${_nwb_literal_library}"
        --llvm-version "${_nwb_literal_version}"
        --policy-hash "${_nwb_policy_hash}" --
    )
    if(CMAKE_C_COMPILER_LAUNCHER OR CMAKE_CXX_COMPILER_LAUNCHER)
        message(FATAL_ERROR "String obfuscation owns the compiler launcher. Remove the competing C/C++ compiler launcher.")
    endif()
    set(CMAKE_C_COMPILER_LAUNCHER "${_nwb_literal_launcher}" PARENT_SCOPE)
    set(CMAKE_CXX_COMPILER_LAUNCHER "${_nwb_literal_launcher}" PARENT_SCOPE)
    message(STATUS "String literals: pooled, encoded through LLVM ${_nwb_literal_version}, decoded once before static initialization")
endfunction()

function(nwb_attach_string_literal_runtime directory)
    get_property(_nwb_literal_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(_nwb_literal_target IN LISTS _nwb_literal_targets)
        get_target_property(_nwb_literal_target_type "${_nwb_literal_target}" TYPE)
        if(_nwb_literal_target_type MATCHES "^(EXECUTABLE|SHARED_LIBRARY|MODULE_LIBRARY)$")
            foreach(_nwb_literal_language C CXX)
                get_target_property(_nwb_existing_link_launcher "${_nwb_literal_target}" "${_nwb_literal_language}_LINKER_LAUNCHER")
                if(_nwb_existing_link_launcher)
                    message(FATAL_ERROR "String obfuscation owns the linker launcher for ${_nwb_literal_target}; remove the competing launcher.")
                endif()
            endforeach()
            target_sources("${_nwb_literal_target}" PRIVATE $<TARGET_OBJECTS:nwb_string_literal_runtime>)
            # Mach-O constructors follow link-input order. Put the one decoder before all consumer objects.
            set(_nwb_literal_link_launcher
                "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/launcher/string_literals/linker.py"
                --runtime-object $<TARGET_OBJECTS:nwb_string_literal_runtime> --
            )
            set_property(TARGET "${_nwb_literal_target}" PROPERTY C_LINKER_LAUNCHER "${_nwb_literal_link_launcher}")
            set_property(TARGET "${_nwb_literal_target}" PROPERTY CXX_LINKER_LAUNCHER "${_nwb_literal_link_launcher}")
        endif()
    endforeach()
    get_property(_nwb_literal_subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(_nwb_literal_subdirectory IN LISTS _nwb_literal_subdirectories)
        nwb_attach_string_literal_runtime("${_nwb_literal_subdirectory}")
    endforeach()
endfunction()

function(nwb_finalize_string_literals)
    if(NWB_OBFUSCATE_STRING_LITERALS)
        nwb_attach_string_literal_runtime("${PROJECT_SOURCE_DIR}")
    endif()
endfunction()
