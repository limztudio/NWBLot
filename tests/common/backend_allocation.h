// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <tests/common/memory_sdk.h>
#include <tbb/scalable_allocator.h>
#include <cerrno>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

[[nodiscard]] inline RawAllocation BackendReallocate(const RawAllocation block, const usize count, const usize alignment)noexcept{ return { static_cast<u8*>(scalable_aligned_realloc(block.bytes, count, alignment)) }; }
inline void SetRangeError()noexcept{ errno = ERANGE; }
[[nodiscard]] inline i32 AllocationError()noexcept{ return errno; }

[[nodiscard]] inline RawAllocation AllocateBacking(usize bytes){ return { static_cast<u8*>(Core::Alloc::CoreAlloc(bytes)) }; }
[[nodiscard]] inline RawAllocation AllocateAlignedBacking(usize bytes, usize alignment){ return { static_cast<u8*>(Core::Alloc::CoreAllocAligned(bytes, alignment)) }; }
[[nodiscard]] inline RawAllocation ResizeBacking(RawAllocation data, usize bytes){ return { static_cast<u8*>(Core::Alloc::CoreRealloc(data.bytes, bytes)) }; }
[[nodiscard]] inline RawAllocation ResizeAlignedBacking(RawAllocation data, usize bytes, usize alignment){ return { static_cast<u8*>(Core::Alloc::CoreReallocAligned(data.bytes, bytes, alignment)) }; }
inline void FreeBacking(RawAllocation data){ Core::Alloc::CoreFree(data.bytes); }
inline void FreeAlignedBacking(RawAllocation data, usize bytes){ Core::Alloc::CoreFreeSizeAligned(data.bytes, bytes); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

