// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The caustic resolve chain works on targets reduced by this factor in each axis.
inline constexpr u32 s_CausticResolveDownscaleFactor = 2u;
inline constexpr Name s_CausticResolveActivityBufferNames[] = { Name("caustic_resolve_activity_a"), Name("caustic_resolve_activity_b") };

struct CausticResolveActivityLayout{
    u64 bufferByteSize = 0u;
    u32 tilesX = 0u;
    u32 tilesY = 0u;
};

// Both buffers are scratch for one complete resolve chain; every producing group overwrites its own word.
struct CausticResolveActivitySnapshot{
    Core::BufferHandle buffers[2];
    Core::GpuDescriptorHandle descriptors[2] = { Core::GpuDescriptorHandle::Invalid(), Core::GpuDescriptorHandle::Invalid() };
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;


public:
    [[nodiscard]] bool valid()const noexcept;
    [[nodiscard]] bool matches(const CausticResolveActivitySnapshot& other)const noexcept;
};

[[nodiscard]] Expected<CausticResolveActivityLayout> ResolveCausticActivityLayout(u32 halfWidth, u32 halfHeight)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

