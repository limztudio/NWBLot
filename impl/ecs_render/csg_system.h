// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "subsystem_base.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererCsgSystem final : public RendererSystemSubsystemBase<RendererSystem>{
public:
    explicit RendererCsgSystem(RendererSystem& renderer);

public:
    [[nodiscard]] CsgFrameState buildFrameState(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] bool createCsgClipResources();
    void destroyCsgClipBindingSet();
    [[nodiscard]] bool createCsgPeelTargets(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCsgIntervalPeelResources(DeferredFrameTargets& targets);
    [[nodiscard]] bool createCsgIntervalSampleResources(DeferredFrameTargets& targets);
    void destroyCsgIntervalPeelBindingSet();
    void dispatchCsgIntervalPeels(Core::CommandList& commandList, DeferredFrameTargets& targets, const CsgFrameGpuData& csgFrameData);
    void renderCsgIntervalCaps(Core::CommandList& commandList, DeferredFrameTargets& targets);
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    [[nodiscard]] bool reserveCsgParameterByteBufferCapacity(usize byteCount);
    [[nodiscard]] bool prepareCsgFrameBuffers(const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void setCsgClipBufferStates(Core::CommandList& commandList);
    [[nodiscard]] bool resolveCsgReceiverEvaluatorVariant(
        const CsgFrameReceiverLookup& receiverLookup,
        Core::ECS::EntityID entity,
        const CsgReceiverCpuBounds& receiverBounds,
        const NWB::Impl::Scene::TransformComponent* transform,
        Name& outEvaluatorVariant
    )const;
    [[nodiscard]] u32 countCsgReceiverClipCutters(
        const CsgFrameReceiverLookup& receiverLookup,
        Core::ECS::EntityID entity,
        const CsgReceiverCpuBounds& receiverBounds,
        const NWB::Impl::Scene::TransformComponent* transform
    )const;
    [[nodiscard]] bool appendCsgReceiverClipData(
        const CsgFrameReceiverLookup& receiverLookup,
        Core::ECS::EntityID entity,
        const CsgReceiverCpuBounds& receiverBounds,
        const NWB::Impl::Scene::TransformComponent* transform,
        CsgFrameGpuData& csgFrameData,
        CsgReceiverRangeGpuData& outRange
    )const;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
