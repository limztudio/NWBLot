// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_target_bindings{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddDepthSamplerBindings(
    Core::BindingSetDesc& bindingSetDesc,
    Core::Texture* depth,
    const Core::Format::Enum depthFormat,
    Core::Sampler* sampler
){
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_AVBOIT_BINDING_OPAQUE_DEPTH,
        depth,
        depthFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_AVBOIT_BINDING_POINT_SAMPLER, sampler));
}

static bool CreateBindingSet(
    Core::Device& device,
    Core::BindingSetHandle& outBindingSet,
    const Core::BindingSetDesc& desc,
    const Core::BindingLayoutHandle& layout
){
    outBindingSet = device.createBindingSet(desc, layout);
    if(outBindingSet)
        return true;

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererAvboitSystem::createAvboitFrameTargetBindingSets(
    DeferredFrameTargets& createdTargets,
    AvboitFrameTargets& avboitTargets
){
    auto* device = graphics().getDevice();

    Core::BindingSetDesc occupancyBindingSetDesc(arena());
    __hidden_avboit_target_bindings::AddDepthSamplerBindings(
        occupancyBindingSetDesc,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        deferredState().m_sampler.get()
    );
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS, avboitTargets.coverageBuffer.get()));
    if(!__hidden_avboit_target_bindings::CreateBindingSet(
        *device,
        avboitTargets.occupancyBindingSet,
        occupancyBindingSetDesc,
        avboitState().m_occupancyBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding set"));
        return false;
    }

    Core::BindingSetDesc depthWarpBindingSetDesc(arena());
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_DEPTH_WARP_BINDING_COVERAGE_WORDS, avboitTargets.coverageBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    if(!__hidden_avboit_target_bindings::CreateBindingSet(
        *device,
        avboitTargets.depthWarpBindingSet,
        depthWarpBindingSetDesc,
        avboitState().m_depthWarpBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding set"));
        return false;
    }

    Core::BindingSetDesc extinctionBindingSetDesc(arena());
    __hidden_avboit_target_bindings::AddDepthSamplerBindings(
        extinctionBindingSetDesc,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        deferredState().m_sampler.get()
    );
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_EXTINCTION, avboitTargets.extinctionBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_OVERFLOW_DEPTH, avboitTargets.extinctionOverflowBuffer.get()));
    if(!__hidden_avboit_target_bindings::CreateBindingSet(
        *device,
        avboitTargets.extinctionBindingSet,
        extinctionBindingSetDesc,
        avboitState().m_extinctionBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding set"));
        return false;
    }

    Core::BindingSetDesc integrateBindingSetDesc(arena());
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_EXTINCTION, avboitTargets.extinctionBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::Texture_UAV(
        NWB_AVBOIT_INTEGRATE_BINDING_TRANSMITTANCE,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_INTEGRATE_BINDING_OVERFLOW_DEPTH, avboitTargets.extinctionOverflowBuffer.get()));
    if(!__hidden_avboit_target_bindings::CreateBindingSet(
        *device,
        avboitTargets.integrateBindingSet,
        integrateBindingSetDesc,
        avboitState().m_integrateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding set"));
        return false;
    }

    Core::BindingSetDesc accumulateBindingSetDesc(arena());
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, avboitState().m_linearSampler.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SCENE_SHADING_AVBOIT_ACCUMULATE_BINDING, deferredState().m_sceneShadingBuffer.get()));
    if(!__hidden_avboit_target_bindings::CreateBindingSet(
        *device,
        avboitTargets.accumulateBindingSet,
        accumulateBindingSetDesc,
        avboitState().m_accumulateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding set"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

