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

namespace ECSRenderDetail{
    struct MeshViewGpuData;
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
    void dispatchCsgIntervalPeels(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool intervalPeelTargetStatesGraphOwned = false,
        bool csgClipBufferStatesGraphOwned = false
    );
    void dispatchCsgReceiverSpanBuild(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool receiverSpanOutputImageStatesGraphOwned = false
    );
    void dispatchCsgIntervalCombine(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool removedIntervalOutputImageStatesGraphOwned = false
    );
    void renderCsgIntervalCaps(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool intervalSampleImageStatesGraphOwned = false,
        bool csgClipBufferStatesGraphOwned = false
    );
    [[nodiscard]] bool createCsgIntervalSampleStateBuffer();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    // Renderer preparation owns CSG buffer growth and descriptor registration.  Material/draw paths only verify and
    // consume these resources after this prepass has completed.
    [[nodiscard]] bool prepareCsgFrameResources(usize receiverRangeCount, usize cutterCount);
    [[nodiscard]] bool csgFrameBuffersReady(const CsgFrameGpuData& csgFrameData)const;
    // Capture all descriptor-derived CSG uniform bytes while preflight has frozen the current buffer and target
    // generations. The deferred graph retains these values as immutable blobs before native recording begins.
    [[nodiscard]] bool prepareCsgClipContextSlotData(
        const CsgFrameGpuData& csgFrameData,
        CsgClipContextSlots& outContextSlots
    )const;
    [[nodiscard]] bool prepareCsgIntervalSampleStateData(
        const DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        CsgIntervalSampleStateGpuData& outState
    )const;
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    // Retained for transparent/legacy native paths. The opaque deferred graph instead snapshots this descriptor
    // indirection during declaration and uploads the captured bytes through its graph task chain.
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
        CsgReceiverRangeGpuData& outRange,
        const ECSRenderDetail::MeshViewGpuData* csgWorkRegionMeshViewState = nullptr
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

