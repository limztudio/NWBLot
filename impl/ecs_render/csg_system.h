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
    [[nodiscard]] bool createCsgOpeningMaskWriteResources(Core::Texture* openingMask);
    void destroyCsgClipBindingSet();
    void destroyCsgOpeningMaskWriteBindingSet();
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    [[nodiscard]] bool reserveCsgParameterByteBufferCapacity(usize byteCount);
    [[nodiscard]] bool uploadCsgFrameBuffers(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void setCsgClipBufferStates(Core::CommandList& commandList);
    void setCsgOpeningMaskWriteTextureState(Core::CommandList& commandList);
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
    [[nodiscard]] bool appendCsgReceiverCapProxies(
        const MeshResources& mesh,
        const NWB::Impl::Scene::TransformComponent* transform,
        CsgReceiverPass::Enum receiverPass,
        u32 receiverIndex,
        const CsgReceiverRangeGpuData& receiverRange,
        const Float4& color,
        CsgFrameGpuData& csgFrameData
    )const;
    [[nodiscard]] bool createCsgCapProxyOpeningMaskResources(Core::Texture* openingMask);
    [[nodiscard]] bool createCsgCapProxyResources(Core::Framebuffer* framebuffer, const CsgCapProxyShapeTypeVector& shapeTypes);
    [[nodiscard]] bool reserveCsgCapProxyBufferCapacity(usize proxyCount);
    [[nodiscard]] bool reserveCsgCapProxyEmulationVertexBufferCapacity(usize proxyCount);
    [[nodiscard]] bool uploadCsgCapProxies(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void renderCsgOpaqueCapProxies(const MaterialPassDrawContext& context, const CsgFrameGpuData& csgFrameData, Core::Texture* openingMaskTarget);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
