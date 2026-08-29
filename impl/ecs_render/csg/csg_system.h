// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/renderer_types.h>

#include <impl/ecs_csg/frame_state.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Scene{
    struct TransformComponent;
};

namespace ECSRenderDetail{
    struct CsgGraphResourceBuffers{
        Core::BufferHandle receiverRanges;
        Core::BufferHandle cutters;
        Core::BufferHandle clipContextSlots;
        Core::BufferHandle intervalSampleState;
    };

    struct MeshFrameBindingSnapshot;
    struct MeshViewGpuData;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CsgShapeRegistry;
class IMaterialSurfaceLookup;
class RendererCsgState;
class RendererShaderSystem;
class RendererMeshSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem final : NoCopy{
public:
    RendererCsgSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        CsgShapeRegistry& csgShapeRegistry,
        RendererCsgState& csgState,
        RendererShaderSystem& shaderSystem,
        RendererMeshSystem& meshSystem
    );

public:
    void invalidateResources();
    [[nodiscard]] CsgFrameState buildFrameState(Core::Alloc::ScratchArena& scratchArena, IMaterialSurfaceLookup& materialSurfaceLookup);
    [[nodiscard]] bool createCsgClipResources();
    [[nodiscard]] bool createCsgPeelTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCsgIntervalPeelResources(DeferredFrameTargets& targets, bool capFillRequired);
    [[nodiscard]] bool createCsgIntervalSampleResources(DeferredFrameTargets& targets);
    void invalidateCsgIntervalPeelPipelines();
    void dispatchCsgIntervalPeels(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        bool intervalPeelTargetStatesGraphOwned = false,
        bool csgClipBufferStatesGraphOwned = false,
        bool materialFrameStatesGraphOwned = false
    );
    void dispatchCsgReceiverSpanBuild(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool receiverSpanOutputImageStatesGraphOwned = false,
        bool receiverSpanInputImageStatesGraphOwned = false
    );
    void dispatchCsgIntervalCombine(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        bool removedIntervalOutputImageStatesGraphOwned = false,
        bool intervalCombineInputImageStatesGraphOwned = false
    );
    void renderCsgIntervalCaps(
        Core::CommandList& commandList,
        DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        bool intervalSampleImageStatesGraphOwned = false,
        bool csgClipBufferStatesGraphOwned = false,
        bool materialFrameStatesGraphOwned = false
    );
    [[nodiscard]] bool createCsgIntervalSampleStateBuffer();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    // Renderer preparation owns CSG buffer growth and descriptor registration.  Material/draw paths only verify and
    // consume these resources after this prepass has completed.
    [[nodiscard]] bool prepareCsgFrameResources(usize receiverRangeCount, usize cutterCount);
    [[nodiscard]] bool csgFrameBuffersReady(const CsgFrameGpuData& csgFrameData)const;
    void populateCsgGraphResourceBuffers(ECSRenderDetail::CsgGraphResourceBuffers& outBuffers)const;
    [[nodiscard]] bool findCsgClipContextHeapSlot(u32& outHeapSlot)const;
    // Capture all descriptor-derived CSG uniform bytes while preflight has frozen the current buffer and target
    // generations. The deferred graph retains these values as immutable blobs before native recording begins.
    [[nodiscard]] bool prepareCsgClipContextSlotData(
        const DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        CsgClipContextSlots& outContextSlots
    )const;
    [[nodiscard]] bool prepareCsgIntervalSampleStateData(
        const DeferredFrameTargets& targets,
        const CsgFrameGpuData& csgFrameData,
        const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
        CsgIntervalSampleStateGpuData& outState
    )const;
    void setCsgReceiverSurfaceImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets);
    void setCsgIntervalSampleImageStates(Core::CommandList& commandList, const DeferredFrameTargets& targets);
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

private:
    void releaseCsgClipContextHeapHandles();

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    CsgShapeRegistry& m_csgShapeRegistry;
    RendererCsgState& m_csgState;
    RendererShaderSystem& m_shaderSystem;
    RendererMeshSystem& m_meshSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

