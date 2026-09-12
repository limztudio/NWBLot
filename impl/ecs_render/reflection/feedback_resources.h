// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "feedback.h"

#include <core/graphics/rhi/gpu_descriptor_heap.h>
#include <core/graphics/rhi/resource.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ReflectionFeedbackExtent{
    u32 width = 0u;
    u32 height = 0u;
    u32 tilesX = 0u;
    u32 tilesY = 0u;
    u32 entryCount = 0u;
    u32 byteCount = 0u;

    [[nodiscard]] bool valid()const noexcept{ return byteCount != 0u; }
};

[[nodiscard]] ReflectionFeedbackExtent ComputeReflectionFeedbackExtent(u32 width, u32 height)noexcept;

struct ReflectionFeedbackBinding{
    Core::BufferHandle buffer;
    Core::GpuDescriptorHandle descriptor = Core::GpuDescriptorHandle::invalid();

    [[nodiscard]] bool valid()const noexcept{ return buffer && descriptor.valid(); }
};

struct ReflectionFeedbackSnapshot{
    ReflectionFeedbackControlHandle control;
    ReflectionFeedbackPlan plan;
    ReflectionFeedbackExtent extent;
    ReflectionFeedbackBinding current;
    ReflectionFeedbackBinding previous;

    [[nodiscard]] bool valid()const noexcept{
        return control && (!plan.eligible || (extent.valid() && current.valid() && previous.valid()));
    }
};

// Creation belongs to preflight; frozen snapshots retain buffers.
class RendererReflectionFeedback final : NoCopy{
public:
    RendererReflectionFeedback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics);


public:
    void invalidateResources();
    [[nodiscard]] bool prepareResources(u32 width, u32 height, const ReflectionSettings& settings, bool enabled);
    [[nodiscard]] ReflectionFeedbackSnapshot snapshot(
        const ReflectionSceneContentStamp& stamp,
        const ReflectionSettings& settings,
        bool enabled,
        u64 graphicsFrameIndex
    )const;

private:
    void releaseBuffers();
    [[nodiscard]] bool prepareBank(ReflectionFeedbackBinding& bank, u32 byteCount, Name name);

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    ReflectionFeedbackControlHandle m_control;
    ReflectionFeedbackBinding m_banks[2];
    ReflectionFeedbackExtent m_extent;
    u16 m_deviceGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

