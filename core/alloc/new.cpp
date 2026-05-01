// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "core.h"

#include <new>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define __nwb__decl_nodiscard [[nodiscard]]

#if defined(_MSC_VER) && defined(_Ret_notnull_) && defined(_Post_writable_byte_size_)
// stay consistent with VCRT definitions
#define __nwb__decl_new(n) __nwb__decl_nodiscard _Ret_notnull_ _Post_writable_byte_size_(n)
#define __nwb__decl_new_nothrow(n) __nwb__decl_nodiscard _Ret_maybenull_ _Success_(return != NULL) _Post_writable_byte_size_(n)
#else
#define __nwb__decl_new(n) __nwb__decl_nodiscard
#define __nwb__decl_new_nothrow(n) __nwb__decl_nodiscard
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_new{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] void* InternalOperatorNew(const std::size_t size, const char* log){
    void* result = NWB::Core::Alloc::CoreAlloc(static_cast<usize>(size), log);
#if TBB_USE_EXCEPTIONS
    while(!result){
        const std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        result = NWB::Core::Alloc::CoreAlloc(static_cast<usize>(size), log);
    }
#endif
    return result;
}

[[nodiscard]] void* InternalOperatorNew(const std::size_t size, const std::size_t alignment, const char* log){
    void* result = NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(size), static_cast<usize>(alignment), log);
#if TBB_USE_EXCEPTIONS
    while(!result){
        const std::new_handler handler = std::get_new_handler();
        if(handler)
            (*handler)();
        else
            throw std::bad_alloc();
        result = NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(size), static_cast<usize>(alignment), log);
    }
#endif
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void operator delete(void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p, "delete(void*)");
}

void operator delete[](void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p, "delete[](void*)");
}

void operator delete(void* p, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFree(p, "delete(void*, const std::nothrow_t&)");
}

void operator delete[](void* p, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFree(p, "delete[](void*, const std::nothrow_t&)");
}

__nwb__decl_new(n) void* operator new(std::size_t n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, "new(std::size_t)");
}

__nwb__decl_new(n) void* operator new[](std::size_t n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, "new[](std::size_t)");
}

__nwb__decl_new_nothrow(n) void* operator new(std::size_t n, const std::nothrow_t&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n), "new(std::size_t, const std::nothrow_t&)");
}

__nwb__decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n), "new[](std::size_t, const std::nothrow_t&)");
}


#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n), "delete(void*, std::size_t)");
}

void operator delete[](void* p, std::size_t n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n), "delete[](void*, std::size_t)");
}
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p, "delete(void*, std::align_val_t)");
}

void operator delete[](void* p, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p, "delete[](void*, std::align_val_t)");
}

void operator delete(void* p, std::size_t n, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n), "delete(void*, std::size_t, std::align_val_t)");
}

void operator delete[](void* p, std::size_t n, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n), "delete[](void*, std::size_t, std::align_val_t)");
}

void operator delete(void* p, std::align_val_t, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p, "delete(void*, std::align_val_t, const std::nothrow_t&)");
}

void operator delete[](void* p, std::align_val_t, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p, "delete[](void*, std::align_val_t, const std::nothrow_t&)");
}

void* operator new(std::size_t n, std::align_val_t alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<std::size_t>(alignment), "new(std::size_t, std::align_val_t)");
}

void* operator new[](std::size_t n, std::align_val_t alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<std::size_t>(alignment), "new[](std::size_t, std::align_val_t)");
}

void* operator new(std::size_t n, std::align_val_t alignment, std::nothrow_t const&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(
        static_cast<usize>(n),
        static_cast<usize>(alignment),
        "new(std::size_t, std::align_val_t, const std::nothrow_t&)"
    );
}

void* operator new[](std::size_t n, std::align_val_t alignment, std::nothrow_t const&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(
        static_cast<usize>(n),
        static_cast<usize>(alignment),
        "new[](std::size_t, std::align_val_t, const std::nothrow_t&)"
    );
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef __nwb__decl_new
#undef __nwb__decl_new_nothrow

#undef __nwb__decl_nodiscard


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

