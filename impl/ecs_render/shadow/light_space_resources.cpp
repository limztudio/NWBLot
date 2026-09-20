// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/vulkan/backend.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureLightSpaceShadowPipelines(){
    auto& state = m_lightSpaceShadow;
    auto& snapshot = state.m_snapshot;
    if(state.m_pipelineFailed)
        return false;
    if(
        snapshot.viewPipeline && snapshot.opaqueResolve && snapshot.transparentResolve
        && snapshot.opaqueFallback && snapshot.transparentFallback && snapshot.shadePipeline
        && snapshot.opaqueCapture && snapshot.transparentCapture
    )
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    // Fragment stores/atomics are mandatory baseline device features, independently of ray-query support.
    constexpr auto depthSupport = Core::FormatSupport::Texture | Core::FormatSupport::DepthStencil | Core::FormatSupport::ShaderLoad;
    if(!heap.isInitialized() || (device.queryFormatSupport(Core::Format::D32) & depthSupport) != depthSupport)
        return false;
    if(!snapshot.layout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::All).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(LightSpaceShadowPush)));
        snapshot.layout = device.createBindingLayout(desc);
        if(!snapshot.layout)
            return false;
    }
    const Name names[] = { AssetsGraphicsShadow::s_LightSpaceViewShaderName, AssetsGraphicsShadow::s_LightSpaceCaptureVertexShaderName,
        AssetsGraphicsShadow::s_LightSpaceCapturePixelShaderName, AssetsGraphicsShadow::s_LightSpaceResolveShaderName,
        AssetsGraphicsShadow::s_LightSpaceResolveShaderName, AssetsGraphicsShadow::s_LightSpaceFallbackShaderName,
        AssetsGraphicsShadow::s_LightSpaceFallbackShaderName, AssetsGraphicsShadow::s_LightSpaceShadeShaderName };
    const Core::ShaderType::Mask stages[] = { Core::ShaderType::Compute, Core::ShaderType::Vertex, Core::ShaderType::Pixel,
        Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute,
        Core::ShaderType::Compute };
    const AStringView variants[] = { AStringView("default"), AStringView("default"), AStringView("default"),
        AStringView("NWB_LIGHT_SPACE_OCCLUDER=0"), AStringView("NWB_LIGHT_SPACE_OCCLUDER=1"),
        AStringView("NWB_LIGHT_SPACE_OCCLUDER=0"), AStringView("NWB_LIGHT_SPACE_OCCLUDER=1"), AStringView("default") };
    for(u32 index = 0u; index < LengthOf(state.m_shaders); ++index){
        if(
            !state.m_shaders[index] && !m_shaderSystem.loadShader(state.m_shaders[index], names[index], variants[index], stages[index],
            Name("ECSRender_LightSpaceShadow"))
        ){
            state.m_pipelineFailed = true;
            return false;
        }
    }
    Core::ComputePipelineHandle* outputs[] = { &snapshot.viewPipeline, &snapshot.opaqueResolve, &snapshot.transparentResolve,
        &snapshot.opaqueFallback, &snapshot.transparentFallback, &snapshot.shadePipeline };
    const u32 shaderIndices[] = { 0u, 3u, 4u, 5u, 6u, 7u };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        if(*outputs[index])
            continue;
        Core::ComputePipelineDesc desc;
        desc
            .setComputeShader(state.m_shaders[shaderIndices[index]]).addBindingLayout(snapshot.layout)
            .addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout())
        ;
        *outputs[index] = device.createComputePipeline(desc);
        if(!*outputs[index])
            return false;
    }
    for(u32 transparent = 0u; transparent < 2u; ++transparent){
        auto& output = transparent != 0u ? snapshot.transparentCapture : snapshot.opaqueCapture;
        if(output)
            continue;
        Core::RasterState raster;
        raster.setCullMode(Core::RasterCullMode::None).enableDepthClip().enableScissor();
        Core::DepthStencilState depth;
        depth.setDepthTestEnable(transparent == 0u).setDepthWriteEnable(transparent == 0u).setDepthFunc(Core::ComparisonFunc::Less);
        Core::RenderState render;
        render.setRasterState(raster).setDepthStencilState(depth);
        Core::GraphicsPipelineDesc desc;
        desc
            .setVertexShader(state.m_shaders[1]).setRenderState(render).addBindingLayout(snapshot.layout)
            .addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout())
        ;
        if(transparent != 0u)
            desc.setPixelShader(state.m_shaders[2]);
        output = device.createGraphicsPipeline(desc, Core::FramebufferInfo{}.setDepthFormat(Core::Format::D32));
        if(!output)
            return false;
    }
    return true;
}

bool RendererRayTracingSystem::ensureLightSpaceShadowStorage(const LightSpacePlan& plan){
    auto& snapshot = m_lightSpaceShadow.m_snapshot;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const u64 sizes[] = { plan.countByteSize, plan.eventByteSize, plan.viewByteSize, plan.drawArgumentByteSize };
    const u64 maximumRange = device.getMaxStorageBufferRange();
    for(const u64 size : sizes){
        if(size == 0u || size > maximumRange || size > Limit<u32>::s_Max)
            return false;
    }
    if(
        snapshot.counts && snapshot.events && snapshot.views && snapshot.drawArguments && snapshot.depth
        && snapshot.counts->getCreationDescription().byteSize == plan.countByteSize
        && snapshot.events->getCreationDescription().byteSize == plan.eventByteSize
        && snapshot.views->getCreationDescription().byteSize == plan.viewByteSize
        && snapshot.drawArguments->getCreationDescription().byteSize == plan.drawArgumentByteSize
        && snapshot.depth->getCreationDescription().width == plan.textureResolution
        && snapshot.depth->getCreationDescription().arraySize == plan.viewCount
    )
        return true;
    Core::BufferHandle buffers[4];
    Core::GpuDescriptorHandle descriptors[5];
    ScopeExit retireUnpublished([&]()noexcept{
        for(auto& descriptor : descriptors)
            RayTracingDetail::RetireHeapHandle(heap, descriptor);
    });
    const Name names[] = { Name("light_space_counts"), Name("light_space_events"), Name("light_space_views"), Name("light_space_draw_arguments") };
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(sizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(index == 3u)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(names[index])
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        buffers[index] = m_graphics.createBuffer(desc);
        if(
            !buffers[index] || !RayTracingDetail::RegisterHeapBuffer(heap, *buffers[index], Core::GpuDescriptorClass::StorageBuffer,
            true, descriptors[index])
        )
            return false;
    }
    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(plan.textureResolution).setHeight(plan.textureResolution).setArraySize(plan.viewCount)
        .setDimension(Core::TextureDimension::Texture2DArray).setFormat(Core::Format::D32).setInRenderTarget(true)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName(Name("light_space_depth"))
    ;
    Core::TextureHandle depth = device.createTexture(depthDesc);
    if(!depth)
        return false;
    descriptors[4] = heap.allocate(Core::GpuDescriptorClass::SampledImage2DArray);
    if(
        !descriptors[4].valid() || !heap.write(descriptors[4], Core::DescriptorWriteItem::Texture_SRV(0u, depth.get(), Core::Format::D32,
        Core::TextureSubresourceSet(0u, 1u, 0u, plan.viewCount), Core::TextureDimension::Texture2DArray))
    )
        return false;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> opaque;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * 6u> transparent;
    for(u32 view = 0u; view < plan.viewCount; ++view){
        Core::FramebufferAttachment attachment;
        attachment.setTexture(depth.get()).setArraySlice(view);
        opaque[view] = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        attachment.setReadOnly(true);
        transparent[view] = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        if(!opaque[view] || !transparent[view])
            return false;
    }
    RayTracingDetail::RetireHeapHandle(heap, snapshot.countsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.eventsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.viewsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.drawArgumentsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.depthDescriptor);
    snapshot.counts = Move(buffers[0]);
    snapshot.events = Move(buffers[1]);
    snapshot.views = Move(buffers[2]);
    snapshot.drawArguments = Move(buffers[3]);
    snapshot.depth = Move(depth);
    snapshot.countsDescriptor = descriptors[0];
    snapshot.eventsDescriptor = descriptors[1];
    snapshot.viewsDescriptor = descriptors[2];
    snapshot.drawArgumentsDescriptor = descriptors[3];
    snapshot.depthDescriptor = descriptors[4];
    snapshot.opaqueFramebuffers = Move(opaque);
    snapshot.transparentFramebuffers = Move(transparent);
    for(auto& descriptor : descriptors)
        descriptor = Core::GpuDescriptorHandle::invalid();
    return true;
}

void RendererRayTracingSystem::releaseLightSpaceShadowResources(){
    auto& state = m_lightSpaceShadow;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    if(heap.isInitialized()){
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.countsDescriptor);
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.eventsDescriptor);
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.viewsDescriptor);
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.drawArgumentsDescriptor);
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.depthDescriptor);
    }
    state.m_snapshot = {};
    state.m_casters.clear();
    for(auto& shader : state.m_shaders)
        shader = nullptr;
    state.m_sceneEligible = false;
    state.m_resourcesPrepared = false;
    state.m_pipelineFailed = false;
    state.m_dispatchLogged = false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

