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


template<typename AllocateFunc>
[[nodiscard]] void* RetryOperatorNew(AllocateFunc allocate){
    void* result = allocate();
    while(!result){
        const std::new_handler handler = std::get_new_handler();
        if(!handler)
            throw std::bad_alloc();

        (*handler)();
        result = allocate();
    }
    return result;
}

[[nodiscard]] void* InternalOperatorNew(const std::size_t size){
    return RetryOperatorNew([size](){
        return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(size));
    });
}

[[nodiscard]] void* InternalOperatorNew(const std::size_t size, const std::size_t alignment){
    return RetryOperatorNew([size, alignment](){
        return NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(size), static_cast<usize>(alignment));
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void operator delete(void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete[](void* p)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete(void* p, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

void operator delete[](void* p, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFree(p);
}

__nwb__decl_new(n) void* operator new(std::size_t n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n);
}

__nwb__decl_new(n) void* operator new[](std::size_t n)noexcept(false){
    return __hidden_new::InternalOperatorNew(n);
}

__nwb__decl_new_nothrow(n) void* operator new(std::size_t n, const std::nothrow_t&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n));
}

__nwb__decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t&)noexcept{
    return NWB::Core::Alloc::CoreAlloc(static_cast<usize>(n));
}


#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n));
}

void operator delete[](void* p, std::size_t n)noexcept{
    NWB::Core::Alloc::CoreFreeSize(p, static_cast<usize>(n));
}
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete[](void* p, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete(void* p, std::size_t n, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n));
}

void operator delete[](void* p, std::size_t n, std::align_val_t)noexcept{
    NWB::Core::Alloc::CoreFreeSizeAligned(p, static_cast<usize>(n));
}

void operator delete(void* p, std::align_val_t, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void operator delete[](void* p, std::align_val_t, const std::nothrow_t&)noexcept{
    NWB::Core::Alloc::CoreFreeAligned(p);
}

void* operator new(std::size_t n, std::align_val_t alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<std::size_t>(alignment));
}

void* operator new[](std::size_t n, std::align_val_t alignment)noexcept(false){
    return __hidden_new::InternalOperatorNew(n, static_cast<std::size_t>(alignment));
}

void* operator new(std::size_t n, std::align_val_t alignment, std::nothrow_t const&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(n), static_cast<usize>(alignment));
}

void* operator new[](std::size_t n, std::align_val_t alignment, std::nothrow_t const&)noexcept{
    return NWB::Core::Alloc::CoreAllocAligned(static_cast<usize>(n), static_cast<usize>(alignment));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef __nwb__decl_new
#undef __nwb__decl_new_nothrow

#undef __nwb__decl_nodiscard


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

