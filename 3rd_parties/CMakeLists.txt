function(nwb_disable_vendor_warnings target)
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# RGD and its vendored AMD dependencies are C/C++ static libraries linked into engine targets, so their MSVC
# runtime (debug vs release CRT) and the _DEBUG-driven _ITERATOR_DEBUG_LEVEL must match the engine's per-config
# choice from nwb_apply_codegen — otherwise lld-link rejects the mix via /failifmismatch ('_ITERATOR_DEBUG_LEVEL'
# / 'RuntimeLibrary'). This applies ONLY the CRT + debug-macro half of nwb_apply_codegen, deliberately not its
# warning / -fno-rtti flags, which would break the vendored code's RTTI and exception use. Config names are the
# engine's custom dbg/opt/fin (CMake's default MSVC_RUNTIME_LIBRARY keys off 'Debug', so it would mis-select).
function(nwb_match_engine_runtime target)
    if(NOT TARGET ${target})
        return()
    endif()
    if(WIN32)
        set_property(TARGET ${target} PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:dbg>:Debug>DLL")
    endif()
    set_property(TARGET ${target} APPEND PROPERTY COMPILE_DEFINITIONS
        $<$<CONFIG:dbg>:_DEBUG>
        $<$<CONFIG:opt>:NDEBUG>
        $<$<CONFIG:fin>:NDEBUG>)
endfunction()

add_subdirectory(CLI11)
add_subdirectory(tsl)
add_subdirectory(miniz)
add_subdirectory(nlohmann_json)
add_subdirectory(rapidjson)
add_subdirectory(zlib)
add_subdirectory(volk)
add_subdirectory(aftermath)
add_subdirectory(VulkanMemoryAllocator)
add_subdirectory(imgui)
add_subdirectory(ufbx)
add_subdirectory(curl)
add_subdirectory(libmicrohttpd)
add_subdirectory(oneTBB)
add_subdirectory(googletest)                       # unit-test framework (gtest) for the engine test suites
nwb_match_engine_runtime(gtest)

# Radeon GPU Detective (RGD) in-process .rgd decoder + its vendored dependencies.
add_subdirectory(zstd)                             # rdf's compression backend (flattened out of rdf/imported)
add_subdirectory(rdf)
# system_info gates its rdf::ChunkFile / rdfChunkFile* Parse overloads (and the amdrdf link that supplies the
# rdf C++ bindings) behind this flag — upstream's top-level sets it; our flattened wiring must too, or RGD and
# the RMT backend reference SystemInfoReader::Parse / DriverOverridesReader::Parse overloads that were compiled
# out of system_info.lib.
set(SYSTEM_INFO_BUILD_RDF_INTERFACES ON)
add_subdirectory(system_info_utils/source)
add_subdirectory(isa_spec_manager/source/isa_decoder)
add_subdirectory(comgr_utils)
add_subdirectory(pix_events)
add_subdirectory(rmv/source/parser)                # RMT parser  (page-fault resource history)
add_subdirectory(rmv/source/backend)               # RMT backend (page-fault resource history)
add_subdirectory(radeon_gpu_detective/source/radeon_gpu_detective_backend)

# Align every RGD-stack static lib's CRT + debug iterator level with the engine's (see nwb_match_engine_runtime).
foreach(_nwb_rgd_target amdrdf zstd system_info isa_decoder ComgrUtils pix_events_decoder RmvParser RmvBackend rgd_backend)
    nwb_match_engine_runtime(${_nwb_rgd_target})
endforeach()
