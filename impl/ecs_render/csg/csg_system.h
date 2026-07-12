// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/subsystem_base.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Scene{
    struct TransformComponent;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererCsgSystem(RendererSystem& renderer);

public:
    [[nodiscard]] CsgFrameState buildFrameState(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool createCsgClipResources();
    void destroyCsgClipBindingSet();
    [[nodiscard]] bool createCsgPeelTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCsgIntervalPeelResources(DeferredFrameTargets& targets, bool capFillRequired);
    [[nodiscard]] bool createCsgIntervalSampleResources(DeferredFrameTargets& targets);
    void destroyCsgIntervalPeelBindingSet();
    void dispatchCsgIntervalPeels(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void dispatchCsgReceiverSpanBuild(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void dispatchCsgIntervalCombine(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool createCsgIntervalSampleStateBuffer();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    [[nodiscard]] bool prepareCsgFrameBuffers(const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool csgFrameBuffersReady(const CsgFrameGpuData& csgFrameData)const;
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
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

