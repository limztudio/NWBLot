if(TARGET nwb::llvm_libc)
    return()
endif()

nwb_declare_vendor_headers(nwb_vendor_llvm_libc nwb::llvm_libc "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(nwb_vendor_llvm_libc INTERFACE
    LIBC_NAMESPACE=__llvm_libc_common_utils
)
target_compile_features(nwb_vendor_llvm_libc INTERFACE cxx_std_17)
