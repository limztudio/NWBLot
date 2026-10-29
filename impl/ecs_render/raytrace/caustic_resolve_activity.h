// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/raytrace/types.adhd.h>

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_CausticResolveActivityBufferNames[] = { Name("caustic_resolve_activity_a"), Name("caustic_resolve_activity_b") };

// Both buffers are scratch for one complete resolve chain; every producing group overwrites its own word.
inline constexpr usize s_CausticResolveActivityBufferCount = LengthOf(s_CausticResolveActivityBufferNames);

struct CausticResolveActivitySnapshot{
    Core::BufferHandle buffers[s_CausticResolveActivityBufferCount];
    Core::GpuDescriptorHandle descriptors[s_CausticResolveActivityBufferCount] = { Core::GpuDescriptorHandle::Invalid(), Core::GpuDescriptorHandle::Invalid() };
    u32 halfWidth = 0u;
    u32 halfHeight = 0u;


public:
    [[nodiscard]] b8 valid()const noexcept;
    [[nodiscard]] b8 matches(const CausticResolveActivitySnapshot& other)const noexcept;
};

[[nodiscard]] Expected<CausticResolveActivityLayout> ResolveCausticActivityLayout(u32 halfWidth, u32 halfHeight)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

