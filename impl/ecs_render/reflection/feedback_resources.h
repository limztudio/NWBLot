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


[[nodiscard]] Expected<ReflectionFeedbackExtent> ComputeReflectionFeedbackExtent(u32 width, u32 height)noexcept;

struct ReflectionFeedbackBinding{
    Core::BufferHandle buffer;
    Core::GpuDescriptorHandle descriptor = Core::GpuDescriptorHandle::Invalid();


public:
    [[nodiscard]] b8 valid()const noexcept{ return buffer && descriptor.valid(); }
};

struct ReflectionFeedbackSnapshot{
    ReflectionFeedbackControlHandle control;
    ReflectionFeedbackPlan plan;
    ReflectionFeedbackExtent extent;
    ReflectionFeedbackBinding current;
    ReflectionFeedbackBinding previous;


public:
    [[nodiscard]] b8 valid()const noexcept{
        return control && (!plan.eligible || (extent.valid() && current.valid() && previous.valid()));
    }
};

// Creation belongs to preflight; frozen snapshots retain buffers.
class RendererReflectionFeedback final : NoCopy{
public:
    RendererReflectionFeedback(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics)noexcept;


public:
    void invalidateResources();
    [[nodiscard]] b8 prepareResources(u32 width, u32 height, const ReflectionSettings& settings, b8 enabled);
    [[nodiscard]] ReflectionFeedbackSnapshot snapshot(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, b8 enabled, u64 graphicsFrameIndex)const noexcept;

private:
    void releaseBuffers();
    [[nodiscard]] Expected<ReflectionFeedbackBinding> prepareBank(u32 byteCount, Name name);


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    static constexpr usize s_FeedbackBankCount = 2u;

    ReflectionFeedbackControlHandle m_control;
    ReflectionFeedbackBinding m_banks[s_FeedbackBankCount];
    ReflectionFeedbackExtent m_extent;
    u16 m_deviceGeneration = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

