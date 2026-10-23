// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"

#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_light_space_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr StringView s_DefaultLightVariant = "default";
static constexpr StringView s_LightSpaceNoCsgOccluder0 = "NWB_LIGHT_SPACE_CSG_ENABLED=0;NWB_LIGHT_SPACE_OCCLUDER=0";
static constexpr StringView s_LightSpaceNoCsgOccluder1 = "NWB_LIGHT_SPACE_CSG_ENABLED=0;NWB_LIGHT_SPACE_OCCLUDER=1";
static constexpr StringView s_LightSpaceCsgShade0 = "NWB_LIGHT_SPACE_CSG_SHADE=0";
static constexpr StringView s_LightSpaceCsgShade1 = "NWB_LIGHT_SPACE_CSG_SHADE=1";
static constexpr StringView s_LightSpaceCsgOccluder0 = "NWB_LIGHT_SPACE_CSG_ENABLED=1;NWB_LIGHT_SPACE_OCCLUDER=0";
static constexpr StringView s_LightSpaceCsgOccluder1 = "NWB_LIGHT_SPACE_CSG_ENABLED=1;NWB_LIGHT_SPACE_OCCLUDER=1";

// Inclusive index ranges into the shader table and the pipeline output table of ensureLightSpaceShadowPipelines.
static constexpr u32 s_FirstCsgOnlyShaderIndex = 8u;
static constexpr u32 s_LastCsgOnlyShaderIndex = 12u;
static constexpr u32 s_FirstCsgFallbackShaderIndex = 11u;
static constexpr u32 s_LastCsgFallbackShaderIndex = 12u;
static constexpr u32 s_FirstCsgOnlyPipelineIndex = 6u;
static constexpr u32 s_LastCsgOnlyPipelineIndex = 10u;
static constexpr u32 s_FirstCsgResolvePipelineIndex = 7u;
static constexpr u32 s_LastCsgResolvePipelineIndex = 8u;
static constexpr u32 s_FirstCsgFallbackPipelineIndex = 9u;
static constexpr u32 s_LastCsgFallbackPipelineIndex = 10u;
static constexpr u32 s_CapturePipelineKindCount = 2u;
static constexpr u32 s_DrawArgumentsBufferIndex = 3u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::ensureLightSpaceShadowPipelines(){
    auto& state = m_lightSpaceShadow;
    auto& snapshot = state.m_snapshot;
    const bool csg = state.m_csg.snapshot.hasCsg;
    const bool hardwareCsg = csg && m_shadowVisibilityHardwareSupported;
    if(state.m_pipelineFailed)
        return false;
    if(
        snapshot.viewPipeline && snapshot.cullPipeline && snapshot.opaqueResolve && snapshot.transparentResolve
        && snapshot.opaqueFallback && snapshot.transparentFallback && snapshot.shadePipeline
        && snapshot.opaqueCapture && snapshot.transparentCapture
        && (!csg || (snapshot.csgShadePipeline && snapshot.csgOpaqueResolve && snapshot.csgTransparentResolve && (hardwareCsg || (snapshot.csgOpaqueFallback && snapshot.csgTransparentFallback))))
    )
        return true;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    // Fragment stores/atomics are mandatory baseline device features, independently of ray-query support.
    constexpr auto s_DepthSupport = Core::FormatSupport::Texture | Core::FormatSupport::DepthStencil | Core::FormatSupport::ShaderLoad;
    if(!heap.isInitialized() || (hardwareCsg && !heap.hasAccelStructLayout()) || (device.queryFormatSupport(Core::Format::D32) & s_DepthSupport) != s_DepthSupport)
        return false;
    if(!snapshot.layout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::All).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(LightSpaceShadowPush)));
        auto layoutResult = device.createBindingLayout(desc);
        if(layoutResult)
            snapshot.layout = Move(*layoutResult);
        else
            snapshot.layout.reset();
        if(!layoutResult)
            return false;
    }
    const Name names[] = { AssetsGraphicsShadow::s_LightSpaceViewShaderName, AssetsGraphicsShadow::s_LightSpaceCaptureVertexShaderName,
        AssetsGraphicsShadow::s_LightSpaceCapturePixelShaderName, AssetsGraphicsShadow::s_LightSpaceResolveShaderName,
        AssetsGraphicsShadow::s_LightSpaceResolveShaderName, AssetsGraphicsShadow::s_LightSpaceFallbackShaderName,
        AssetsGraphicsShadow::s_LightSpaceFallbackShaderName, AssetsGraphicsShadow::s_LightSpaceShadeShaderName,
        AssetsGraphicsShadow::s_LightSpaceShadeShaderName,
        hardwareCsg ? AssetsGraphicsShadow::s_LightSpaceResolveHwShaderName : AssetsGraphicsShadow::s_LightSpaceResolveShaderName,
        hardwareCsg ? AssetsGraphicsShadow::s_LightSpaceResolveHwShaderName : AssetsGraphicsShadow::s_LightSpaceResolveShaderName,
        AssetsGraphicsShadow::s_LightSpaceFallbackShaderName,
        AssetsGraphicsShadow::s_LightSpaceFallbackShaderName, AssetsGraphicsShadow::s_LightSpaceCullShaderName };
    const Core::ShaderType::Mask stages[] = { Core::ShaderType::Compute, Core::ShaderType::Vertex, Core::ShaderType::Pixel,
        Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute,
        Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute,
        Core::ShaderType::Compute, Core::ShaderType::Compute, Core::ShaderType::Compute };
    const AStringView variants[] = { __hidden_light_space_resources::s_DefaultLightVariant, __hidden_light_space_resources::s_DefaultLightVariant, __hidden_light_space_resources::s_DefaultLightVariant,
        __hidden_light_space_resources::s_LightSpaceNoCsgOccluder0,
        __hidden_light_space_resources::s_LightSpaceNoCsgOccluder1,
        __hidden_light_space_resources::s_LightSpaceNoCsgOccluder0,
        __hidden_light_space_resources::s_LightSpaceNoCsgOccluder1,
        __hidden_light_space_resources::s_LightSpaceCsgShade0, __hidden_light_space_resources::s_LightSpaceCsgShade1,
        __hidden_light_space_resources::s_LightSpaceCsgOccluder0,
        __hidden_light_space_resources::s_LightSpaceCsgOccluder1,
        __hidden_light_space_resources::s_LightSpaceCsgOccluder0,
        __hidden_light_space_resources::s_LightSpaceCsgOccluder1, __hidden_light_space_resources::s_DefaultLightVariant };
    for(u32 index = 0u; index < LengthOf(state.m_shaders); ++index){
        if(!csg && index >= __hidden_light_space_resources::s_FirstCsgOnlyShaderIndex && index <= __hidden_light_space_resources::s_LastCsgOnlyShaderIndex)
            continue;
        if(hardwareCsg && index >= __hidden_light_space_resources::s_FirstCsgFallbackShaderIndex && index <= __hidden_light_space_resources::s_LastCsgFallbackShaderIndex)
            continue;
        if(!state.m_shaders[index] && !m_shaderSystem.loadShaderForStage(state.m_shaders[index], names[index], variants[index], stages[index], Name("ECSRender_LightSpaceShadow"))){
            state.m_pipelineFailed = true;
            return false;
        }
    }
    const NotNull<Core::ComputePipelineHandle*> outputs[] = { MakeNotNull(&snapshot.viewPipeline), MakeNotNull(&snapshot.opaqueResolve), MakeNotNull(&snapshot.transparentResolve),
        MakeNotNull(&snapshot.opaqueFallback), MakeNotNull(&snapshot.transparentFallback), MakeNotNull(&snapshot.shadePipeline), MakeNotNull(&snapshot.csgShadePipeline),
        MakeNotNull(&snapshot.csgOpaqueResolve), MakeNotNull(&snapshot.csgTransparentResolve), MakeNotNull(&snapshot.csgOpaqueFallback), MakeNotNull(&snapshot.csgTransparentFallback), MakeNotNull(&snapshot.cullPipeline) };
    const u32 shaderIndices[] = { 0u, 3u, 4u, 5u, 6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u };
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        if(!csg && index >= __hidden_light_space_resources::s_FirstCsgOnlyPipelineIndex && index <= __hidden_light_space_resources::s_LastCsgOnlyPipelineIndex)
            continue;
        if(hardwareCsg && index >= __hidden_light_space_resources::s_FirstCsgFallbackPipelineIndex && index <= __hidden_light_space_resources::s_LastCsgFallbackPipelineIndex)
            continue;
        if(*outputs[index])
            continue;
        Core::ComputePipelineDesc desc;
        desc.setComputeShader(state.m_shaders[shaderIndices[index]]).addBindingLayout(snapshot.layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        if(hardwareCsg && index >= __hidden_light_space_resources::s_FirstCsgResolvePipelineIndex && index <= __hidden_light_space_resources::s_LastCsgResolvePipelineIndex)
            desc.addBindingLayout(heap.getAccelStructLayout());
        auto outputsResult = device.createComputePipeline(desc);
        if(outputsResult)
            *outputs[index] = Move(*outputsResult);
        else
            (*outputs[index]).reset();
        if(!outputsResult)
            return false;
    }
    for(u32 transparent = 0u; transparent < __hidden_light_space_resources::s_CapturePipelineKindCount; ++transparent){
        auto& output = transparent != 0u ? snapshot.transparentCapture : snapshot.opaqueCapture;
        if(output)
            continue;
        Core::RasterState raster;
        raster.setCullMode(Core::RasterCullMode::None).enableDepthClip();
        Core::DepthStencilState depth;
        depth.setDepthTestEnable(transparent == 0u).setDepthWriteEnable(transparent == 0u).setDepthFunc(Core::ComparisonFunc::Less);
        Core::RenderState render;
        render.setRasterState(raster).setDepthStencilState(depth);
        Core::GraphicsPipelineDesc desc;
        desc.setVertexShader(state.m_shaders[1]).setRenderState(render).addBindingLayout(snapshot.layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        if(transparent != 0u)
            desc.setPixelShader(state.m_shaders[2]);
        auto outputResult = device.createGraphicsPipeline(desc, Core::FramebufferInfo{}.setDepthFormat(Core::Format::D32));
        if(outputResult)
            output = Move(*outputResult);
        else
            output.reset();
        if(!outputResult)
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
    if(m_lightSpaceShadow.m_csg.snapshot.hasCsg){
        auto& diagnostics = m_lightSpaceShadow.m_diagnostics;
        if(!diagnostics.readback){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizeof(u32)).setCpuAccess(Core::CpuAccessMode::Read)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(Name("light_space_csg_diagnostic_readback"))
                .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
            ;
            auto readbackResult = m_graphics.createBuffer(desc);
            if(readbackResult)
                diagnostics.readback = Move(*readbackResult);
            else
                diagnostics.readback.reset();
            if(!readbackResult)
                return false;
        }
        const u64 contextSize = m_lightSpaceShadow.m_csg.bytes.size();
        if(contextSize == 0u || contextSize > maximumRange || contextSize > Limit<u32>::s_Max)
            return false;
        if(!snapshot.csgContext || snapshot.csgContext->getCreationDescription().byteSize < contextSize || !snapshot.csgOpaqueDepth || snapshot.csgOpaqueDepth->getCreationDescription().byteSize < plan.countByteSize){
            Core::BufferHandle csgBuffers[2];
            Core::GpuDescriptorHandle csgDescriptors[2];
            Core::GpuDescriptorRetirementGuard retireCsg(heap, Span<const Core::GpuDescriptorHandle>(csgDescriptors));
            const u64 csgSizes[] = { contextSize, plan.countByteSize };
            const Name csgNames[] = { Name("light_space_csg_context"), Name("light_space_csg_opaque_depth") };
            for(u32 index = 0u; index < LengthOf(csgBuffers); ++index){
                Core::BufferDesc desc;
                desc
                    .setByteSize(csgSizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
                    .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(csgNames[index])
                    .enableAutomaticStateTracking(Core::ResourceStates::Common)
                ;
                auto csgBuffersResult = m_graphics.createBuffer(desc);
                if(csgBuffersResult)
                    csgBuffers[index] = Move(*csgBuffersResult);
                else
                    csgBuffers[index].reset();
                if(!csgBuffersResult)
                    return false;
                const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *csgBuffers[index], Core::GpuDescriptorClass::StorageBuffer, true);
                if(!descriptor)
                    return false;
                csgDescriptors[index] = *descriptor;
            }
            RayTracingDetail::RetireHeapHandle(heap, snapshot.csgContextDescriptor);
            RayTracingDetail::RetireHeapHandle(heap, snapshot.csgOpaqueDepthDescriptor);
            snapshot.csgContext = Move(csgBuffers[0]);
            diagnostics.contextInitialized = {};
            snapshot.csgOpaqueDepth = Move(csgBuffers[1]);
            snapshot.csgContextDescriptor = csgDescriptors[0];
            snapshot.csgOpaqueDepthDescriptor = csgDescriptors[1];
            for(auto& descriptor : csgDescriptors)
                descriptor = Core::GpuDescriptorHandle::Invalid();
            m_lightSpaceShadow.m_captureHistory.invalidate();
        }
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
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
    const Name names[] = { Name("light_space_counts"), Name("light_space_events"), Name("light_space_views"), Name("light_space_draw_arguments") };
    for(u32 index = 0u; index < LengthOf(buffers); ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(sizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(index == __hidden_light_space_resources::s_DrawArgumentsBufferIndex)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setDebugName(names[index])
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        auto buffersResult = m_graphics.createBuffer(desc);
        if(buffersResult)
            buffers[index] = Move(*buffersResult);
        else
            buffers[index].reset();
        if(!buffersResult)
            return false;
        const auto descriptor = RayTracingDetail::RegisterHeapBuffer(heap, *buffers[index], Core::GpuDescriptorClass::StorageBuffer, true);
        if(!descriptor)
            return false;
        descriptors[index] = *descriptor;
    }
    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(plan.textureResolution).setHeight(plan.textureResolution).setArraySize(plan.viewCount)
        .setDimension(Core::TextureDimension::Texture2DArray).setFormat(Core::Format::D32).setInRenderTarget(true)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName(Name("light_space_depth"))
    ;
    auto depthResult = device.createTexture(depthDesc);
    if(!depthResult)
        return false;
    Core::TextureHandle depth = Move(*depthResult);
    const Expected<Core::GpuDescriptorHandle> depthDescriptor = heap.allocate(Core::GpuDescriptorClass::SampledImage2DArray);
    if(!depthDescriptor)
        return false;
    descriptors[4] = *depthDescriptor;
    if(!heap.write(descriptors[4], Core::DescriptorWriteItem::TextureSrv(0u, depth.get(), Core::Format::D32, Core::TextureSubresourceSet(0u, 1u, 0u, plan.viewCount), Core::TextureDimension::Texture2DArray)))
        return false;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> opaque;
    Array<Core::FramebufferHandle, NWB_SCENE_SHADOW_SLOT_COUNT * s_PointLightViewCount> transparent;
    for(u32 view = 0u; view < plan.viewCount; ++view){
        Core::FramebufferAttachment attachment;
        attachment.setTexture(depth.get()).setArraySlice(view);
        auto opaqueResult = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        if(opaqueResult)
            opaque[view] = Move(*opaqueResult);
        else
            opaque[view].reset();
        attachment.setReadOnly(true);
        auto transparentResult = device.createFramebuffer(Core::FramebufferDesc{}.setDepthAttachment(attachment));
        if(transparentResult)
            transparent[view] = Move(*transparentResult);
        else
            transparent[view].reset();
        if(!opaqueResult || !transparentResult)
            return false;
    }
    RayTracingDetail::RetireHeapHandle(heap, snapshot.countsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.eventsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.viewsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.drawArgumentsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, snapshot.depthDescriptor);
    m_lightSpaceShadow.m_captureHistory.invalidate();
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
        descriptor = Core::GpuDescriptorHandle::Invalid();
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
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgContextDescriptor);
        RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgOpaqueDepthDescriptor);
    }
    state.m_captureHistory.invalidate();
    state.m_diagnostics = {};
    state.m_captureSceneTrusted = false;
    state.m_captureReuseLogged = false;
    state.m_sceneBuffers.clear();
    state.m_captureBuffers.clear();
    state.m_sceneTextures.clear();
    state.m_captureTextures.clear();
    state.m_snapshot = {};
    state.m_casters.clear();
    for(auto& shader : state.m_shaders)
        shader = nullptr;
    state.m_sceneEligible = false;
    state.m_resourcesPrepared = false;
    state.m_pipelineFailed = false;
    state.m_dispatchLogged = false;
    state.m_csgDispatchLogged = false;
    state.m_csg.snapshot.hasCsg = false;
    state.m_csg.bytes.clear();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

