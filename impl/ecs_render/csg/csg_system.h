// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Scene{
    struct TransformComponent;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererCsgSystem(RendererSystem& renderer);

public:
    [[nodiscard]] CsgFrameState buildFrameState(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool createCsgClipResources();
    void releaseCsgClipContextHeapHandles();
    [[nodiscard]] bool createCsgPeelTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCsgIntervalPeelResources(DeferredFrameTargets& targets, bool capFillRequired);
    [[nodiscard]] bool createCsgIntervalSampleResources(DeferredFrameTargets& targets);
    void invalidateCsgIntervalPeelPipelines();
    void dispatchCsgIntervalPeels(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void dispatchCsgReceiverSpanBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void dispatchCsgIntervalCombine(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool createCsgIntervalSampleStateBuffer();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    // Renderer preparation owns CSG buffer growth and descriptor registration.  Material/draw paths only verify and
    // consume these resources after this prepass has completed.
    [[nodiscard]] bool prepareCsgFrameResources(usize receiverRangeCount, usize cutterCount);
    [[nodiscard]] bool csgFrameBuffersReady(const CsgFrameGpuData& csgFrameData)const;
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    // The clip-context slot payload is a specialized descriptor indirection.  Generic receiver/cutter stream
    // uploads may be graph-owned, while this narrow compatibility write remains adjacent to the native CSG draws.
    [[nodiscard]] bool uploadCsgFrameContextSlots(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool uploadCsgIntervalSampleState(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void setCsgClipBufferStates(Core::CommandList& commandList);
    [[nodiscard]] bool resolveCsgReceiverClipDrawInfo(
        const CsgFrameReceiverLookup& receiverLookup,
        const CsgReceiverDrawState& receiverDrawState,
        const CsgReceiverCpuBounds& receiverBounds,
        const NWB::Impl::Scene::TransformComponent* transform,
        CsgReceiverClipDrawInfo& outInfo
    )const;
    [[nodiscard]] bool appendCsgReceiverClipData(
        const CsgFrameReceiverLookup& receiverLookup,
        const CsgReceiverDrawState& receiverDrawState,
        const CsgReceiverCpuBounds& receiverBounds,
        const NWB::Impl::Scene::TransformComponent* transform,
        u32 frameWidth,
        u32 frameHeight,
        CsgFrameGpuData& csgFrameData,
        CsgReceiverRangeGpuData& outRange
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

