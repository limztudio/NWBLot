if(TARGET nwb::basis_universal)
    return()
endif()

add_library(nwb_vendor_basis_universal STATIC
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_backend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_basis_file.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_comp.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_enc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_etc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_frontend.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_gpu_texture.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_pvrtc1_4.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_resampler.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_resample_filters.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_ssim.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_uastc_enc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_bc7e_scalar.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_dds_export.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_bc7enc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/jpgd.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_kernels_sse.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_bc15_spmd.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_bc15_spmd_sse.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_opencl.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/pvpngreader.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_uastc_hdr_4x4_enc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_astc_hdr_6x6_enc.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_astc_hdr_common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_astc_ldr_common.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_astc_ldr_encode.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_astc_ldr_fencode.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_xbc7_encode.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/encoder/basisu_tinyexr.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/transcoder/basisu_transcoder.cpp"
)
add_library(nwb::basis_universal ALIAS nwb_vendor_basis_universal)

target_include_directories(nwb_vendor_basis_universal SYSTEM PUBLIC
    "${CMAKE_CURRENT_LIST_DIR}/encoder"
    "${CMAKE_CURRENT_LIST_DIR}/transcoder"
)
target_link_libraries(nwb_vendor_basis_universal PUBLIC
    nwb::miniz
    nwb::qoi
    nwb::tinyexr
    nwb::tinydds
    zstd
)
target_compile_definitions(nwb_vendor_basis_universal PRIVATE
    BASISD_SUPPORT_KTX2=1
    BASISD_SUPPORT_KTX2_ZSTD=1
    BASISU_SUPPORT_SSE=0
    BASISU_SUPPORT_OPENCL=0
    BASISU_SUPPORT_ASTCENC=0
    BASISU_DISABLE_ANDROID_ASTC_DECOMP=1
)
if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(nwb_vendor_basis_universal PRIVATE Threads::Threads m)
endif()

nwb_disable_vendor_warnings(nwb_vendor_basis_universal)
set_target_properties(nwb_vendor_basis_universal PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
nwb_apply_debug_symbols(nwb_vendor_basis_universal)
