// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "postprocess_resources.h"
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererReflectionPostprocess::invalidateResources(){
    releaseTargets();
    m_temporalPipeline = nullptr;
    for(Core::ComputePipelineHandle& pipeline : m_spatialPipelines)
        pipeline = nullptr;
    m_temporalShader = nullptr;
    for(Core::ShaderHandle& shader : m_spatialShaders)
        shader = nullptr;
    m_layout = nullptr;
}

b8 RendererReflectionPostprocess::prepareResources(const u32 width, const u32 height, const ReflectionSettings& settings){
    if(settings.spatialRadius > NWB_REFLECTION_SPATIAL_MAX_RADIUS)
        return false;
    auto& device = m_graphics.getDevice();
    if(width != m_width || height != m_height){
        releaseTargets();
        m_width = width;
        m_height = height;
        if(m_control)
            m_control->reset(device.getDeviceGeneration());
    }
    if(!m_control){
        auto controlResult = CreateStatisticsControl<ReflectionHistoryControl>(m_arena, device.getDeviceGeneration());
        if(!controlResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection history: failed to allocate acceptance control"));
            return false;
        }
        m_control = Move(*controlResult);
    }
    const b8 active = settings.traceMode != ReflectionTraceMode::Disabled;
    if(settings.temporalEnabled && active){
        if(
            !preparePipeline(&ReflectionSubsystem::temporalCs, Core::ShaderArchive::s_DefaultVariant, m_temporalPipeline, m_temporalShader)
            || !prepareImage(m_history, s_ReflectionHistoryDescriptorIndex, s_ReflectionOpaqueHistoryName)
            || !m_graphics.gpuTiming().prepareScopeQueries(ReflectionGpuTimingScope::s_Temporal.identity, device, s_ReflectionGpuTimingQueriesPerRange)
        )
            return false;
    }
    if(settings.spatialFilterEnabled && settings.spatialRadius > 0u && active){
        const u32 radiusIndex = settings.spatialRadius - 1u;
        if(
            !preparePipeline(&ReflectionSubsystem::spatialCs, ResolveReflectionSpatialVariant(radiusIndex), m_spatialPipelines[radiusIndex], m_spatialShaders[radiusIndex])
            || !prepareImage(m_spatial, s_ReflectionSpatialDescriptorIndex, s_ReflectionOpaqueSpatialName)
            || !m_graphics.gpuTiming().prepareScopeQueries(ReflectionGpuTimingScope::s_Spatial.identity, device, s_ReflectionGpuTimingQueriesPerRange)
        )
            return false;
    }
    return true;
}

ReflectionPostprocessSnapshot RendererReflectionPostprocess::snapshot(const ReflectionRadianceBinding& base, const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const u64 graphicsFrameIndex)const noexcept{
    ReflectionPostprocessSnapshot result;
    if(!m_control)
        return result;
    result.control = m_control;
    result.history = m_control->plan(stamp, settings, graphicsFrameIndex);
    result.current = result.history.currentBank == 0u ? base : m_history;
    result.previous = result.history.previousBank == 0u ? base : m_history;
    result.spatial = m_spatial;
    result.temporalPipeline = m_temporalPipeline;
    result.spatialEnabled = settings.spatialFilterEnabled && settings.spatialRadius > 0u && settings.spatialRadius <= NWB_REFLECTION_SPATIAL_MAX_RADIUS && settings.traceMode != ReflectionTraceMode::Disabled;
    if(result.spatialEnabled)
        result.spatialPipeline = m_spatialPipelines[settings.spatialRadius - 1u];
    return result;
}

void RendererReflectionPostprocess::releaseTargets(){
    if(m_control)
        m_control->reset(0u);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    for(Core::GpuDescriptorHandle& descriptor : m_descriptors){
        if(descriptor.valid() && heap.isInitialized())
            heap.free(descriptor);
        descriptor = Core::GpuDescriptorHandle::Invalid();
    }
    m_history = {};
    m_spatial = {};
    m_width = 0u;
    m_height = 0u;
}

b8 RendererReflectionPostprocess::preparePipeline(
    const Core::Assets::AssetRef<ComputeShader> ReflectionSubsystem::* shaderMember,
    const AStringView variant,
    Core::ComputePipelineHandle& pipeline,
    Core::ShaderHandle& shader
){
    const Name shaderName = m_shaders.resolveSubsystemAsset<ReflectionSubsystem>(shaderMember);
    return RendererComputePipelineDetail::EnsurePushConstantComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaders,
        shaderMember,
        variant,
        shaderName,
        NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES,
        false,
        m_layout,
        shader,
        pipeline
    );
}

b8 RendererReflectionPostprocess::prepareImage(ReflectionRadianceBinding& image, const u32 descriptorIndex, const Name name){
    if(image.texture)
        return true;
    Core::TextureDesc desc;
    desc.setWidth(m_width).setHeight(m_height).setFormat(Core::Format::RGBA16_FLOAT).setIsUAV(true).setName(name).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true);
    auto textureResult = m_graphics.createTexture(desc);
    if(!textureResult)
        return false;
    Core::TextureHandle texture = Move(*textureResult);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    const Expected<Core::GpuDescriptorHandle> sampled = RendererDescriptorRegisterDetail::RegisterSampledTexture(heap, Core::GpuDescriptorClass::SampledImage, texture);
    const Expected<Core::GpuDescriptorHandle> storage = RendererDescriptorRegisterDetail::RegisterStorageTexture(heap, texture);
    if(!sampled || !storage){
        if(sampled)
            heap.free(*sampled);
        if(storage)
            heap.free(*storage);
        return false;
    }
    m_descriptors[descriptorIndex] = *sampled;
    m_descriptors[descriptorIndex + 1u] = *storage;
    image = ReflectionRadianceBinding{Move(texture), sampled->slot(), storage->slot()};
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

