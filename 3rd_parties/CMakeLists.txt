include("${CMAKE_CURRENT_LIST_DIR}/../configuration/UnityBuild.cmake")

function(nwb_declare_vendor_headers target alias include_directory)
    if(TARGET ${alias})
        return()
    endif()
    add_library(${target} INTERFACE)
    add_library(${alias} ALIAS ${target})
    target_include_directories(${target} SYSTEM INTERFACE "${include_directory}")
endfunction()

function(nwb_disable_vendor_warnings target)
    if(NWB_COMPILER_FRONTEND_MSVC)
        target_compile_options(${target} PRIVATE /W0)
    else()
        target_compile_options(${target} PRIVATE -w)
    endif()
endfunction()

# Match the engine CRT and iterator-debug level to avoid lld-link mismatches.
# Keep vendor warning and RTTI settings; the engine uses dbg/opt/fin configurations.
function(nwb_match_engine_runtime target)
    if(NOT TARGET ${target})
        return()
    endif()
    nwb_apply_engine_runtime(${target})
endfunction()

add_subdirectory(CLI11)
add_subdirectory(cxxopts)
add_subdirectory(tsl)
add_subdirectory(miniz)
add_subdirectory(qoi)
add_subdirectory(tinyexr)
add_subdirectory(tinydds)
add_subdirectory(tinyxml2)
add_subdirectory(nlohmann_json)
add_subdirectory(rapidjson)
add_subdirectory(zlib)
add_subdirectory(harfbuzz)
add_subdirectory(freetype)
add_subdirectory(vulkan_headers)
add_subdirectory(volk)
add_subdirectory(aftermath)
add_subdirectory(VulkanMemoryAllocator)
add_subdirectory(ufbx)
add_subdirectory(curl)
add_subdirectory(libmicrohttpd)
add_subdirectory(oneTBB)
add_subdirectory(siphash)
add_subdirectory(llvm_libc)
add_subdirectory(blake3)
add_subdirectory(xxhash)
add_subdirectory(llvm)
add_subdirectory(libclang)
add_subdirectory(googletest)                       # unit-test framework (gtest) for the engine test suites
nwb_match_engine_runtime(gtest)
add_subdirectory(wmi)

# Policy section 1.1: static C/C++ vendor libraries linked by engine targets must use the engine CRT and
# _ITERATOR_DEBUG_LEVEL. The helper is guarded so platform-specific targets (for example vendored curl) are safe.
foreach(_nwb_engine_static_vendor_target
    nwb_vendor_volk
    nwb_vendor_ufbx
    nwb_vendor_curl
    nwb_vendor_microhttpd
    nwb_vendor_miniz
    nwb_vendor_zlib
    nwb_vendor_freetype
    nwb_vendor_harfbuzz
    nwb_vendor_wmi
    nwb_vendor_xxhash
    nwb_vendor_blake3
    nwb_vendor_llvm
    nwb_vendor_libclang
)
    nwb_match_engine_runtime(${_nwb_engine_static_vendor_target})
endforeach()

# Radeon GPU Detective (RGD) in-process .rgd decoder + its vendored dependencies.
add_subdirectory(zstd)                             # rdf's compression backend (flattened out of rdf/imported)
add_subdirectory(jpeg_compressor)
add_subdirectory(basis_universal)                  # UASTC encoder; shares miniz/zstd and official image codecs
nwb_match_engine_runtime(nwb_vendor_jpeg_compressor)
nwb_match_engine_runtime(nwb_vendor_basis_universal)
add_subdirectory(rdf)
add_subdirectory(comgr)
# system_info gates its rdf::ChunkFile / rdfChunkFile* Parse overloads (and the amdrdf link that supplies the
# rdf C++ bindings) behind this flag — upstream's top-level sets it; our flattened wiring must too, or RGD and
# the RMT backend reference SystemInfoReader::Parse / DriverOverridesReader::Parse overloads that were compiled
# out of system_info.lib.
set(SYSTEM_INFO_BUILD_RDF_INTERFACES ON)
add_subdirectory(system_info_utils)
add_subdirectory(isa_spec_manager)
add_subdirectory(comgr_utils)
add_subdirectory(dev_driver)
add_subdirectory(pevents)
add_subdirectory(pix_events)
add_subdirectory(rmv)                              # RMT parser/backend (page-fault resource history)
add_subdirectory(radeon_gpu_detective)

# Align every RGD-stack static lib's CRT + debug iterator level with the engine's (see nwb_match_engine_runtime).
foreach(_nwb_rgd_target amdrdf zstd system_info isa_decoder ComgrUtils pix_events_decoder RmvParser RmvBackend rgd_backend)
    nwb_match_engine_runtime(${_nwb_rgd_target})
endforeach()
