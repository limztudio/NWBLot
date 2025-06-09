// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <new>
#include <tbb/scalable_allocator.h>


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


static inline void* InternalOperatorNew(size_t sz){
    void* res = scalable_malloc(sz);
#if TBB_USE_EXCEPTIONS
    while (!res){
        std::new_handler handler;
#if __TBB_CPP11_GET_NEW_HANDLER_PRESENT
        handler = std::get_new_handler();
#else
        {
            ProxyMutex::scoped_lock lock(new_lock);
            handler = std::set_new_handler(0);
            std::set_new_handler(handler);
        }
#endif
        if (handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = scalable_malloc(sz);
}
#endif
    return res;
}

static inline void* InternalOperatorNew(size_t sz, size_t align){
    void* res = scalable_aligned_malloc(sz, align);
#if TBB_USE_EXCEPTIONS
    while (!res){
        std::new_handler handler;
#if __TBB_CPP11_GET_NEW_HANDLER_PRESENT
        handler = std::get_new_handler();
#else
        {
            ProxyMutex::scoped_lock lock(new_lock);
            handler = std::set_new_handler(0);
            std::set_new_handler(handler);
        }
#endif
        if (handler)
            (*handler)();
        else
            throw std::bad_alloc();
        res = scalable_aligned_malloc(sz, align);
    }
#endif
    return res;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void operator delete(void* p)noexcept{ scalable_free(p); };
void operator delete[](void* p)noexcept{ scalable_free(p); };

void operator delete(void* p, const std::nothrow_t&)noexcept{ scalable_free(p); }
void operator delete[](void* p, const std::nothrow_t&)noexcept{ scalable_free(p); }

__nwb__decl_new(n) void* operator new(std::size_t n)noexcept(false){ return InternalOperatorNew(n); }
__nwb__decl_new(n) void* operator new[](std::size_t n)noexcept(false){ return InternalOperatorNew(n); }

__nwb__decl_new_nothrow(n) void* operator new(std::size_t n, const std::nothrow_t& tag)noexcept{ (void)(tag); return scalable_malloc(n); }
__nwb__decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t& tag)noexcept{ (void)(tag); return scalable_malloc(n); }

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n)noexcept{ (void)n; scalable_free(p); };
void operator delete[](void* p, std::size_t n)noexcept{ (void)n; scalable_free(p); };
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t al)noexcept{ (void)al; scalable_free(p); }
void operator delete[](void* p, std::align_val_t al)noexcept{ (void)al; scalable_free(p); }
void operator delete(void* p, std::size_t n, std::align_val_t al)noexcept{ (void)n; (void)al; scalable_free(p); };
void operator delete[](void* p, std::size_t n, std::align_val_t al)noexcept{ (void)n; (void)al; scalable_free(p); };
void operator delete(void* p, std::align_val_t al, const std::nothrow_t&)noexcept{ (void)al; scalable_free(p); }
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t&)noexcept{ (void)al; scalable_free(p); }

void* operator new(std::size_t n, std::align_val_t al)noexcept(false){ return InternalOperatorNew(n, static_cast<size_t>(al)); }
void* operator new[](std::size_t n, std::align_val_t al)noexcept(false){ return InternalOperatorNew(n, static_cast<size_t>(al)); }
void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&)noexcept{ return scalable_aligned_malloc(n, static_cast<size_t>(al)); }
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&)noexcept{ return scalable_aligned_malloc(n, static_cast<size_t>(al)); }
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef __nwb__decl_new
#undef __nwb__decl_new_nothrow

#undef __nwb__decl_nodiscard


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

