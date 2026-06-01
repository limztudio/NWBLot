// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "avboit_private.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_avboit_targets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static Core::TextureHandle CreateRenderTarget(
    Core::Graphics& graphics,
    const u32 width,
    const u32 height,
    const Core::Format::Enum format,
    const char* debugName,
    const Core::Color& clearValue
){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setFormat(format)
        .setInRenderTarget(true)
        .setName(debugName)
        .setClearValue(clearValue)
    ;
    Core::TextureHandle texture = graphics.createTexture(desc);
    if(texture)
        return texture;

    return {};
}

static Core::TextureHandle CreateTransmittanceVolume(
    Core::Graphics& graphics,
    const u32 width,
    const u32 height,
    const u32 depth,
    const Core::Format::Enum format
){
    Core::TextureDesc desc;
    desc
        .setWidth(width)
        .setHeight(height)
        .setDepth(depth)
        .setFormat(format)
        .setDimension(Core::TextureDimension::Texture3D)
        .setInUAV(true)
        .setName("engine/avboit/transmittance_volume")
        .setClearValue(Core::Color(1.f, 1.f, 1.f, 1.f))
    ;
    Core::TextureHandle texture = graphics.createTexture(desc);
    if(texture)
        return texture;

    return {};
}

static Core::BufferHandle CreateU32Buffer(
    Core::Graphics& graphics,
    const u64 byteSize,
    const char* debugName
){
    Core::BufferDesc desc;
    desc
        .setByteSize(byteSize)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName(debugName)
    ;
    Core::BufferHandle buffer = graphics.createBuffer(desc);
    if(buffer)
        return buffer;

    return {};
}

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


bool RendererSystem::createAvboitFrameTargets(
    DeferredFrameTargets& createdTargets,
    const Core::Format::Enum lowRasterFormat,
    const Core::Format::Enum accumColorFormat,
    const Core::Format::Enum accumExtinctionFormat,
    const Core::Format::Enum transmittanceFormat
){
    auto* device = m_graphics.getDevice();
    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.width), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample))
    );
    const u64 lowHeight = Max<u64>(
        1u,
        DivideUp(static_cast<u64>(createdTargets.height), static_cast<u64>(ECSRenderAvboitDetail::s_AvboitDownsample))
    );
    if(lowWidth > Limit<u32>::s_Max || lowHeight > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution dimensions exceed u32 limits"));
        return false;
    }
    avboitTargets.lowWidth = static_cast<u32>(lowWidth);
    avboitTargets.lowHeight = static_cast<u32>(lowHeight);
    avboitTargets.virtualSliceCount = ECSRenderAvboitDetail::s_AvboitVirtualSlices;
    avboitTargets.physicalSliceCount = ECSRenderAvboitDetail::s_AvboitPhysicalSlices;
    avboitTargets.lowRasterFormat = lowRasterFormat;
    avboitTargets.accumColorFormat = accumColorFormat;
    avboitTargets.accumExtinctionFormat = accumExtinctionFormat;
    avboitTargets.transmittanceFormat = transmittanceFormat;

    const Core::Color transparentBlack(0.f, 0.f, 0.f, 0.f);
    avboitTargets.lowRasterTarget = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.lowWidth,
        avboitTargets.lowHeight,
        avboitTargets.lowRasterFormat,
        "engine/avboit/low_raster",
        transparentBlack
    );
    if(!avboitTargets.lowRasterTarget){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }

    avboitTargets.accumColor = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.fullWidth,
        avboitTargets.fullHeight,
        avboitTargets.accumColorFormat,
        "engine/avboit/accum_color",
        transparentBlack
    );
    if(!avboitTargets.accumColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }

    avboitTargets.accumExtinction = __hidden_avboit_targets::CreateRenderTarget(
        m_graphics,
        avboitTargets.fullWidth,
        avboitTargets.fullHeight,
        avboitTargets.accumExtinctionFormat,
        "engine/avboit/accum_extinction",
        transparentBlack
    );
    if(!avboitTargets.accumExtinction){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), ECSRenderDetail::s_FramebufferSubresources);
    avboitTargets.lowFramebuffer = device->createFramebuffer(lowFramebufferDesc);
    if(!avboitTargets.lowFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferAttachment accumulationAttachments[NWB_AVBOIT_ACCUM_TARGET_COUNT] = {};
    accumulationAttachments[NWB_AVBOIT_ACCUM_COLOR_LOCATION]
        .setTexture(avboitTargets.accumColor.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;
    accumulationAttachments[NWB_AVBOIT_ACCUM_EXTINCTION_LOCATION]
        .setTexture(avboitTargets.accumExtinction.get())
        .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
    ;

    Core::FramebufferDesc accumulationFramebufferDesc;
    for(const Core::FramebufferAttachment& attachment : accumulationAttachments)
        accumulationFramebufferDesc.addColorAttachment(attachment);
    accumulationFramebufferDesc.setDepthAttachment(
        Core::FramebufferAttachment()
            .setTexture(createdTargets.depth.get())
            .setSubresources(ECSRenderDetail::s_FramebufferSubresources)
            .setReadOnly(true)
    );
    avboitTargets.accumulationFramebuffer = device->createFramebuffer(accumulationFramebufferDesc);
    if(!avboitTargets.accumulationFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u32 coverageWordCount = DivideUp(avboitTargets.virtualSliceCount, NWB_AVBOIT_COVERAGE_SLICES_PER_WORD);
    const u64 coverageBytes = static_cast<u64>(coverageWordCount) * sizeof(u32);
    const u64 depthWarpBytes = static_cast<u64>(avboitTargets.virtualSliceCount) * sizeof(u32);
    const u64 lowPixelCount = static_cast<u64>(avboitTargets.lowWidth) * avboitTargets.lowHeight;
    if(lowPixelCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution pixel count exceeds u32 limits"));
        return false;
    }
    const u32 physicalExtinctionWordCount = DivideUp(
        avboitTargets.physicalSliceCount,
        ECSRenderAvboitDetail::s_AvboitExtinctionSlicesPerWord
    );
    if(physicalExtinctionWordCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / physicalExtinctionWordCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT packed extinction word count exceeds u32 limits"));
        return false;
    }
    const u64 extinctionWordCount = lowPixelCount * physicalExtinctionWordCount;
    const u64 extinctionBytes = extinctionWordCount * sizeof(u32);
    const u64 extinctionOverflowBytes = lowPixelCount * sizeof(u32);

    avboitTargets.coverageBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        coverageBytes,
        "engine/avboit/depth_coverage"
    );
    if(!avboitTargets.coverageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }

    avboitTargets.depthWarpBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        depthWarpBytes,
        "engine/avboit/depth_warp_lut"
    );
    if(!avboitTargets.depthWarpBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }

    avboitTargets.controlBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        static_cast<u64>(ECSRenderAvboitDetail::s_AvboitControlWordCount) * sizeof(u32),
        "engine/avboit/control"
    );
    if(!avboitTargets.controlBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }

    avboitTargets.extinctionBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        extinctionBytes,
        "engine/avboit/packed_extinction_volume"
    );
    if(!avboitTargets.extinctionBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }

    avboitTargets.extinctionOverflowBuffer = __hidden_avboit_targets::CreateU32Buffer(
        m_graphics,
        extinctionOverflowBytes,
        "engine/avboit/extinction_overflow_depth"
    );
    if(!avboitTargets.extinctionOverflowBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction overflow buffer"));
        return false;
    }

    avboitTargets.transmittanceTexture = __hidden_avboit_targets::CreateTransmittanceVolume(
        m_graphics,
        avboitTargets.lowWidth,
        avboitTargets.lowHeight,
        avboitTargets.physicalSliceCount,
        avboitTargets.transmittanceFormat
    );
    if(!avboitTargets.transmittanceTexture){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }

    Core::BindingSetDesc occupancyBindingSetDesc(m_arena);
    __hidden_avboit_targets::AddDepthSamplerBindings(
        occupancyBindingSetDesc,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        m_deferredState.m_sampler.get()
    );
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_OCCUPANCY_BINDING_COVERAGE_WORDS, avboitTargets.coverageBuffer.get()));
    if(!__hidden_avboit_targets::CreateBindingSet(
        *device,
        avboitTargets.occupancyBindingSet,
        occupancyBindingSetDesc,
        m_avboitState.m_occupancyBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding set"));
        return false;
    }

    Core::BindingSetDesc depthWarpBindingSetDesc(m_arena);
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_DEPTH_WARP_BINDING_COVERAGE_WORDS, avboitTargets.coverageBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_DEPTH_WARP_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    if(!__hidden_avboit_targets::CreateBindingSet(
        *device,
        avboitTargets.depthWarpBindingSet,
        depthWarpBindingSetDesc,
        m_avboitState.m_depthWarpBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding set"));
        return false;
    }

    Core::BindingSetDesc extinctionBindingSetDesc(m_arena);
    __hidden_avboit_targets::AddDepthSamplerBindings(
        extinctionBindingSetDesc,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        m_deferredState.m_sampler.get()
    );
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_EXTINCTION_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_EXTINCTION, avboitTargets.extinctionBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(NWB_AVBOIT_EXTINCTION_BINDING_OVERFLOW_DEPTH, avboitTargets.extinctionOverflowBuffer.get()));
    if(!__hidden_avboit_targets::CreateBindingSet(
        *device,
        avboitTargets.extinctionBindingSet,
        extinctionBindingSetDesc,
        m_avboitState.m_extinctionBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding set"));
        return false;
    }

    Core::BindingSetDesc integrateBindingSetDesc(m_arena);
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
    if(!__hidden_avboit_targets::CreateBindingSet(
        *device,
        avboitTargets.integrateBindingSet,
        integrateBindingSetDesc,
        m_avboitState.m_integrateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding set"));
        return false;
    }

    Core::BindingSetDesc accumulateBindingSetDesc(m_arena);
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_DEPTH_WARP, avboitTargets.depthWarpBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        NWB_AVBOIT_ACCUMULATE_BINDING_TRANSMITTANCE,
        avboitTargets.transmittanceTexture.get(),
        avboitTargets.transmittanceFormat,
        ECSRenderDetail::s_FramebufferSubresources,
        Core::TextureDimension::Texture3D
    ));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(NWB_AVBOIT_ACCUMULATE_BINDING_CONTROL, avboitTargets.controlBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::Sampler(NWB_AVBOIT_ACCUMULATE_BINDING_LINEAR_SAMPLER, m_avboitState.m_linearSampler.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(NWB_SCENE_SHADING_AVBOIT_ACCUMULATE_BINDING, m_deferredState.m_sceneShadingBuffer.get()));
    if(!__hidden_avboit_targets::CreateBindingSet(
        *device,
        avboitTargets.accumulateBindingSet,
        accumulateBindingSetDesc,
        m_avboitState.m_accumulateBindingLayout
    )){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding set"));
        return false;
    }

    createdTargets.avboit = Move(avboitTargets);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

