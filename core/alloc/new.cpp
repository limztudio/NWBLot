// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <new>
#include <core/alloc/core.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(__cplusplus) && (__cplusplus >= 201703)
#define __nwb__decl_nodiscard [[nodiscard]]
#elif (defined(__GNUC__) && (__GNUC__ >= 4)) || defined(__clang__) // includes clang, icc, and clang-cl
#define __nwb__decl_nodiscard __attribute__((warn_unused_result))
#elif defined(_HAS_NODISCARD)
#define __nwb__decl_nodiscard _NODISCARD
#elif (_MSC_VER >= 1700)
#define __nwb__decl_nodiscard _Check_return_
#else
#define __nwb__decl_nodiscard
#endif

#if defined(_MSC_VER) && defined(_Ret_notnull_) && defined(_Post_writable_byte_size_)
// stay consistent with VCRT definitions
#define __nwb__decl_new(n) __nwb__decl_nodiscard _Ret_notnull_ _Post_writable_byte_size_(n)
#define __nwb__decl_new_nothrow(n) __nwb__decl_nodiscard _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n)
#else
#define __nwb__decl_new(n) __nwb__decl_nodiscard
#define __nwb__decl_new_nothrow(n) __nwb__decl_nodiscard
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static inline void* InternalOperatorNew(std::size_t sz, const char* log){
    void* res = NWB::Core::Alloc::CoreAlloc(static_cast<usize>(sz), log);
#if TBB_USE_EXCEPTIONS
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = NWB::Core::Alloc::CoreAlloc(static_cast<usize>(sz), log);
}
#endif
    return res;
}

static inline void* InternalOperatorNew(std::size_t sz, std::size_t align, const char* log){
    void* res = NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(sz), static_cast<usize>(align), log);
#if TBB_USE_EXCEPTIONS
    while(!res){
        std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(sz), static_cast<usize>(align), log);
    }
#endif
    return res;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void operator delete(void* p)noexcept{ NWB::Core::Alloc::CoreFree(p, "delete(void*)"); };
void operator delete[](void* p)noexcept{ NWB::Core::Alloc::CoreFree(p, "delete[](void*)"); };

void operator delete(void* p, const std::nothrow_t&)noexcept{ NWB::Core::Alloc::CoreFree(p, "delete(void*, const std::nothrow_t&)"); }
void operator delete[](void* p, const std::nothrow_t&)noexcept{ NWB::Core::Alloc::CoreFree(p, "delete[](void*, const std::nothrow_t&)"); }

__nwb__decl_new(n) void* operator new(std::size_t n)noexcept(false){ return InternalOperatorNew(n, "new(std::size_t)"); }
__nwb__decl_new(n) void* operator new[](std::size_t n)noexcept(false){ return InternalOperatorNew(n, "new[](std::size_t)"); }

__nwb__decl_new_nothrow(n) void* operator new(std::size_t n, const std::nothrow_t&)noexcept{ return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n), "new(std::size_t, const std::nothrow_t&)"); }
__nwb__decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t&)noexcept{ return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n), "new[](std::size_t, const std::nothrow_t&)"); }

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n)noexcept{ NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n), "delete(void*, std::size_t)"); };
void operator delete[](void* p, std::size_t n)noexcept{ NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n), "delete[](void*, std::size_t)"); };
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t)noexcept{ NWB::Core::Alloc::CoreFreeAligned(p, "delete(void*, std::align_val_t)"); }
void operator delete[](void* p, std::align_val_t)noexcept{ NWB::Core::Alloc::CoreFreeAligned(p, "delete[](void*, std::align_val_t)"); }
void operator delete(void* p, std::size_t n, std::align_val_t)noexcept{ NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n), "delete(void*, std::size_t, std::align_val_t)"); };
void operator delete[](void* p, std::size_t n, std::align_val_t)noexcept{ NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n), "delete[](void*, std::size_t, std::align_val_t)"); };
void operator delete(void* p, std::align_val_t, const std::nothrow_t&)noexcept{ NWB::Core::Alloc::CoreFreeAligned(p, "delete(void*, std::align_val_t, const std::nothrow_t&)"); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&)noexcept{ NWB::Core::Alloc::CoreFreeAligned(p, "delete[](void*, std::align_val_t, const std::nothrow_t&)"); }

void* operator new(std::size_t n, std::align_val_t al)noexcept(false){ return InternalOperatorNew(n, static_cast<size_t>(al), "new(std::size_t, std::align_val_t)"); }
void* operator new[](std::size_t n, std::align_val_t al)noexcept(false){ return InternalOperatorNew(n, static_cast<size_t>(al), "new[](std::size_t, std::align_val_t)"); }
void* operator new(std::size_t n, std::align_val_t al, std::nothrow_t const&)noexcept{ return NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(n), static_cast<usize>(al), "new(std::size_t, std::align_val_t, const std::nothrow_t&)"); }
void* operator new[](std::size_t n, std::align_val_t al, std::nothrow_t const&)noexcept{ return NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(n), static_cast<usize>(al), "new[](std::size_t, std::align_val_t, const std::nothrow_t&)");
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef __nwb__decl_new
#undef __nwb__decl_new_nothrow

#undef __nwb__decl_nodiscard


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

