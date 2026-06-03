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
    [[nodiscard]] bool reserveCsgReceiverRangeBufferCapacity(usize rangeCount);
    [[nodiscard]] bool reserveCsgCutterBufferCapacity(usize cutterCount);
    [[nodiscard]] bool reserveCsgParameterByteBufferCapacity(usize byteCount);
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
    [[nodiscard]] bool appendCsgReceiverCapGeometry(
        const MeshResources& mesh,
        const NWB::Impl::Scene::TransformComponent* transform,
        const Name& receiverGroup,
        CsgReceiverPass::Enum receiverPass,
        u32 receiverIndex,
        const CsgReceiverRangeGpuData& receiverRange,
        CsgFrameGpuData& csgFrameData,
        CsgCapDrawItemVector& capDrawItems
    )const;
    [[nodiscard]] bool createCsgCapSharedResources();
    [[nodiscard]] bool createCsgOpaqueCapResources(Core::Framebuffer* framebuffer);
    [[nodiscard]] bool createCsgTransparentCapResources(Core::Framebuffer* framebuffer, MaterialPipelinePass::Enum pass);
    [[nodiscard]] bool reserveCsgCapVertexBufferCapacity(usize vertexCount);
    [[nodiscard]] bool uploadCsgCapVertices(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void renderCsgCaps(const MaterialPassDrawContext& context, const CsgFrameGpuData& csgFrameData, const CsgCapDrawItemVector& capDrawItems, Core::GraphicsPipeline* pipeline);
    void renderCsgOpaqueCaps(const MaterialPassDrawContext& context, const CsgFrameGpuData& csgFrameData);
    void renderCsgTransparentCaps(const MaterialPassDrawContext& context, const CsgFrameGpuData& csgFrameData);
    [[nodiscard]] bool createCsgCapProxyResources(Core::Framebuffer* framebuffer, u32 shapeMask);
    [[nodiscard]] bool reserveCsgCapProxyBufferCapacity(usize proxyCount);
    [[nodiscard]] bool uploadCsgCapProxies(Core::CommandList& commandList, const CsgFrameGpuData& csgFrameData);
    void renderCsgOpaqueCapProxies(const MaterialPassDrawContext& context, const CsgFrameGpuData& csgFrameData, Core::Texture* openingMaskTarget);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
