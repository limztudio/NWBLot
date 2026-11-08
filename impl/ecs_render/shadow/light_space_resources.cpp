// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_shadow.h"
#include "light_space_resource_set.h"

#include <impl/assets_graphics/shadow_subsystem.adhd.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/shadow/light_space_settings.adhd.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 LightSpaceResourceSet::ensureLightSpaceShadowPipelines(){
    auto& state = m_state;
    auto& snapshot = state.m_snapshot;
    const b8 csg = state.m_csg.snapshot.hasCsg;
    const b8 hardwareCsg = csg && m_hardwareSupported;
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
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(LightSpaceShadowPush), Core::ShaderType::All, snapshot.layout))
        return false;
    using ComputeShaderMember = const Core::Assets::AssetRef<ComputeShader> ShadowSubsystem::*;
    const ComputeShaderMember csgResolveMember = hardwareCsg ? &ShadowSubsystem::lightSpaceResolveHwCs : &ShadowSubsystem::lightSpaceResolveCs;
    const Array<ComputeShaderMember, 12u> computeMembers = {
        &ShadowSubsystem::lightSpaceViewCs,
        &ShadowSubsystem::lightSpaceResolveCs,
        &ShadowSubsystem::lightSpaceResolveCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceShadeCs,
        &ShadowSubsystem::lightSpaceShadeCs,
        csgResolveMember,
        csgResolveMember,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceFallbackCs,
        &ShadowSubsystem::lightSpaceCullCs
    };
    const Array<AStringView, 12u> computeVariants = {
        Core::ShaderArchive::s_DefaultVariant,
        s_LightSpaceNoCsgOccluder0,
        s_LightSpaceNoCsgOccluder1,
        s_LightSpaceNoCsgOccluder0,
        s_LightSpaceNoCsgOccluder1,
        s_LightSpaceCsgShade0,
        s_LightSpaceCsgShade1,
        s_LightSpaceCsgOccluder0,
        s_LightSpaceCsgOccluder1,
        s_LightSpaceCsgOccluder0,
        s_LightSpaceCsgOccluder1,
        Core::ShaderArchive::s_DefaultVariant
    };
    const Array<NotNull<Core::ComputePipelineHandle*>, 12u> outputs = { MakeNotNull(&snapshot.viewPipeline), MakeNotNull(&snapshot.opaqueResolve), MakeNotNull(&snapshot.transparentResolve),
        MakeNotNull(&snapshot.opaqueFallback), MakeNotNull(&snapshot.transparentFallback), MakeNotNull(&snapshot.shadePipeline), MakeNotNull(&snapshot.csgShadePipeline),
        MakeNotNull(&snapshot.csgOpaqueResolve), MakeNotNull(&snapshot.csgTransparentResolve), MakeNotNull(&snapshot.csgOpaqueFallback), MakeNotNull(&snapshot.csgTransparentFallback), MakeNotNull(&snapshot.cullPipeline) };
    static_assert(computeMembers.size() == outputs.size());
    static_assert(computeVariants.size() == outputs.size());
    for(u32 index = 0u; index < outputs.size(); ++index){
        if(ShouldSkipLightSpacePipeline(index, csg, hardwareCsg))
            continue;
        if(!state.m_shaders[index] && !m_shaderSystem.loadShader<ComputeShader>(computeMembers[index], computeVariants[index], Name(s_LightSpaceShaderScope), state.m_shaders[index])){
            state.m_pipelineFailed = true;
            return false;
        }
    }
    Core::ShaderHandle& captureVs = state.m_shaders[s_LightSpaceCaptureVertexShaderIndex];
    Core::ShaderHandle& capturePs = state.m_shaders[s_LightSpaceCapturePixelShaderIndex];
    if(
        (!captureVs && !m_shaderSystem.loadShader<VertexShader>(&ShadowSubsystem::lightSpaceCaptureVs, Core::ShaderArchive::s_DefaultVariant, Name(s_LightSpaceShaderScope), captureVs))
        || (!capturePs && !m_shaderSystem.loadShader<PixelShader>(&ShadowSubsystem::lightSpaceCapturePs, Core::ShaderArchive::s_DefaultVariant, Name(s_LightSpaceShaderScope), capturePs))
    ){
        state.m_pipelineFailed = true;
        return false;
    }
    for(u32 index = 0u; index < LengthOf(outputs); ++index){
        if(ShouldSkipLightSpacePipeline(index, csg, hardwareCsg))
            continue;
        if(*outputs[index])
            continue;
        const b8 includeAccelStruct = hardwareCsg && index >= s_LightSpaceFirstCsgResolvePipelineIndex && index <= s_LightSpaceLastCsgResolvePipelineIndex;
        if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, state.m_shaders[index], snapshot.layout, includeAccelStruct, *outputs[index]))
            return false;
    }
    for(u32 transparent = 0u; transparent < s_LightSpaceCapturePipelineKindCount; ++transparent){
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
        desc.setVertexShader(state.m_shaders[s_LightSpaceCaptureVertexShaderIndex]).setRenderState(render).addBindingLayout(snapshot.layout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
        if(transparent != 0u)
            desc.setPixelShader(state.m_shaders[s_LightSpaceCapturePixelShaderIndex]);
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

b8 LightSpaceResourceSet::ensureLightSpaceShadowStorage(const LightSpacePlan& plan){
    auto& snapshot = m_state.m_snapshot;
    auto& device = m_graphics.getDevice();
    auto& heap = device.getDescriptorHeap();
    const Array<u64, 4u> sizes = { plan.countByteSize, plan.eventByteSize, plan.viewByteSize, plan.drawArgumentByteSize };
    const u64 maximumRange = device.getMaxStorageBufferRange();
    NWB_ASSERT(
        sizes[0] != 0u && sizes[0] <= maximumRange && sizes[0] <= Limit<u32>::s_Max
        && sizes[1] != 0u && sizes[1] <= maximumRange && sizes[1] <= Limit<u32>::s_Max
        && sizes[2] != 0u && sizes[2] <= maximumRange && sizes[2] <= Limit<u32>::s_Max
        && sizes[3] != 0u && sizes[3] <= maximumRange && sizes[3] <= Limit<u32>::s_Max
    );
    if(m_state.m_csg.snapshot.hasCsg){
        auto& diagnostics = m_state.m_diagnostics;
        if(!diagnostics.readback){
            Core::BufferDesc desc;
            desc
                .setByteSize(sizeof(u32)).setCpuAccess(Core::CpuAccessMode::Read)
                .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
                .setDebugName(Name(s_LightSpaceResourceCsgDiagnosticReadback))
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
        const u64 contextSize = m_state.m_csg.bytes.size();
        if(contextSize == 0u || contextSize > maximumRange || contextSize > Limit<u32>::s_Max)
            return false;
        if(!snapshot.csgContext || snapshot.csgContext->getCreationDescription().byteSize < contextSize || !snapshot.csgOpaqueDepth || snapshot.csgOpaqueDepth->getCreationDescription().byteSize < plan.countByteSize){
            Array<Core::BufferHandle, 2u> csgBuffers{};
            Array<Core::GpuDescriptorHandle, 2u> csgDescriptors{};
            Core::GpuDescriptorRetirementGuard retireCsg(heap, Span<const Core::GpuDescriptorHandle>(csgDescriptors));
            const Array<u64, 2u> csgSizes = { contextSize, plan.countByteSize };
            const Array<Name, 2u> csgNames = { Name(s_LightSpaceResourceCsgContext), Name(s_LightSpaceResourceCsgOpaqueDepth) };
            for(u32 index = 0u; index < csgBuffers.size(); ++index){
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
            m_state.m_captureHistory.invalidate();
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
    Array<Core::BufferHandle, s_LightSpaceBufferCount> buffers{};
    Array<Core::GpuDescriptorHandle, s_LightSpaceDescriptorCount> descriptors{};
    Core::GpuDescriptorRetirementGuard retireUnpublished(heap, Span<const Core::GpuDescriptorHandle>(descriptors));
    const Array<Name, 4u> names = { Name(s_LightSpaceResourceCounts), Name(s_LightSpaceResourceEvents), Name(s_LightSpaceResourceViews), Name(s_LightSpaceResourceDrawArguments) };
    for(u32 index = 0u; index < buffers.size(); ++index){
        Core::BufferDesc desc;
        desc
            .setByteSize(sizes[index]).setStructStride(sizeof(u32)).setCanHaveRawViews(true).setCanHaveUAVs(true)
            .setIsDrawIndirectArgs(index == s_LightSpaceDrawArgumentsBufferIndex)
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
        .setDimension(Core::TextureDimension::Texture2DArray).setFormat(Core::Format::D32).setIsRenderTarget(true)
        .setInitialState(Core::ResourceStates::Common).setKeepInitialState(true)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName(Name(s_LightSpaceResourceDepth))
    ;
    auto depthResult = device.createTexture(depthDesc);
    if(!depthResult)
        return false;
    Core::TextureHandle depth = Move(*depthResult);
    if(!RendererDescriptorRegisterDetail::AssignSampledTexture(
        heap,
        Core::GpuDescriptorClass::SampledImage2DArray,
        depth.get(),
        Core::Format::D32,
        Core::TextureSubresourceSet(0u, 1u, 0u, plan.viewCount),
        Core::TextureDimension::Texture2DArray,
        descriptors[s_LightSpaceDepthDescriptorIndex]
    ))
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
        attachment.setIsReadOnly(true);
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
    m_state.m_captureHistory.invalidate();
    snapshot.counts = Move(buffers[s_LightSpaceCountsBufferIndex]);
    snapshot.events = Move(buffers[s_LightSpaceEventsBufferIndex]);
    snapshot.views = Move(buffers[s_LightSpaceViewsBufferIndex]);
    snapshot.drawArguments = Move(buffers[s_LightSpaceDrawArgumentsBufferIndex]);
    snapshot.depth = Move(depth);
    snapshot.countsDescriptor = descriptors[s_LightSpaceCountsBufferIndex];
    snapshot.eventsDescriptor = descriptors[s_LightSpaceEventsBufferIndex];
    snapshot.viewsDescriptor = descriptors[s_LightSpaceViewsBufferIndex];
    snapshot.drawArgumentsDescriptor = descriptors[s_LightSpaceDrawArgumentsBufferIndex];
    snapshot.depthDescriptor = descriptors[s_LightSpaceDepthDescriptorIndex];
    snapshot.opaqueFramebuffers = Move(opaque);
    snapshot.transparentFramebuffers = Move(transparent);
    for(auto& descriptor : descriptors)
        descriptor = Core::GpuDescriptorHandle::Invalid();
    return true;
}

void LightSpaceResourceSet::releaseLightSpaceShadowResources(){
    auto& state = m_state;
    auto& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.countsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.eventsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.viewsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.drawArgumentsDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.depthDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgContextDescriptor);
    RayTracingDetail::RetireHeapHandle(heap, state.m_snapshot.csgOpaqueDepthDescriptor);
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

