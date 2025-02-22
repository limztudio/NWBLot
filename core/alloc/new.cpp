// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <new>
#include "mimalloc.h"


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


void operator delete(void* p)noexcept{ mi_free(p); };
void operator delete[](void* p)noexcept{ mi_free(p); };

void operator delete(void* p, const std::nothrow_t&)noexcept{ mi_free(p); }
void operator delete[](void* p, const std::nothrow_t&)noexcept{ mi_free(p); }

__nwb__decl_new(n) void* operator new(std::size_t n)noexcept(false){ return mi_new(n); }
__nwb__decl_new(n) void* operator new[](std::size_t n)noexcept(false){ return mi_new(n); }

__nwb__decl_new_nothrow(n) void* operator new(std::size_t n, const std::nothrow_t& tag)noexcept{ (void)(tag); return mi_new_nothrow(n); }
__nwb__decl_new_nothrow(n) void* operator new[](std::size_t n, const std::nothrow_t& tag)noexcept{ (void)(tag); return mi_new_nothrow(n); }

#if (__cplusplus >= 201402L || _MSC_VER >= 1916)
void operator delete(void* p, std::size_t n)noexcept{ mi_free_size(p, n); };
void operator delete[](void* p, std::size_t n)noexcept{ mi_free_size(p, n); };
#endif

#if (__cplusplus > 201402L || defined(__cpp_aligned_new))
void operator delete(void* p, std::align_val_t al)noexcept{ mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete[](void* p, std::align_val_t al)noexcept{ mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete(void* p, std::size_t n, std::align_val_t al)noexcept{ mi_free_size_aligned(p, n, static_cast<size_t>(al)); };
void operator delete[](void* p, std::size_t n, std::align_val_t al)noexcept{ mi_free_size_aligned(p, n, static_cast<size_t>(al)); };
void operator delete(void* p, std::align_val_t al, const std::nothrow_t&)noexcept{ mi_free_aligned(p, static_cast<size_t>(al)); }
void operator delete[](void* p, std::align_val_t al, const std::nothrow_t&)noexcept{ mi_free_aligned(p, static_cast<size_t>(al)); }

void* operator new(std::size_t n, std::align_val_t al)noexcept(false){ return mi_new_aligned(n, static_cast<size_t>(al)); }
void* operator new[](std::size_t n, std::align_val_t al)noexcept(false){ return mi_new_aligned(n, static_cast<size_t>(al)); }
void* operator new(std::size_t n, std::align_val_t al, const std::nothrow_t&)noexcept{ return mi_new_aligned_nothrow(n, static_cast<size_t>(al)); }
void* operator new[](std::size_t n, std::align_val_t al, const std::nothrow_t&)noexcept{ return mi_new_aligned_nothrow(n, static_cast<size_t>(al)); }
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#undef __nwb__decl_new
#undef __nwb__decl_new_nothrow

#undef __nwb__decl_nodiscard


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

