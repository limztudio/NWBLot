// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "postprocess_resources.h"
#include "timing_names.h"

#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/ecs_render/shader/shader_system.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_postprocess_resources{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SpatialVariants[] = {
    "NWB_REFLECTION_SPATIAL_RADIUS=1",
    "NWB_REFLECTION_SPATIAL_RADIUS=2",
    "NWB_REFLECTION_SPATIAL_RADIUS=3",
};
static_assert(LengthOf(s_SpatialVariants) == NWB_REFLECTION_SPATIAL_MAX_RADIUS);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererReflectionPostprocess::RendererReflectionPostprocess(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    RendererShaderSystem& shaders)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_shaders(shaders)
{}

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

bool RendererReflectionPostprocess::prepareResources(const u32 width, const u32 height, const ReflectionSettings& settings){
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
    if(!m_control)
        m_control = CreateReflectionHistoryControl(m_arena, device.getDeviceGeneration());
    if(!m_control){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection history: failed to allocate acceptance control"));
        return false;
    }
    const bool active = settings.traceMode != ReflectionTraceMode::Disabled;
    if(settings.temporalEnabled && active){
        if(
            !preparePipeline(
                m_temporalPipeline, m_temporalShader, Name("engine/graphics/reflection/temporal_cs"), Core::ShaderArchive::s_DefaultVariant
            )
            || !prepareImage(m_history, 0u, Name("engine/reflection/opaque_history"))
            || !m_graphics.gpuTiming().prepareScopeQueries(ReflectionGpuTimingScope::s_Temporal.identity, device, 2u)
        )
            return false;
    }
    if(settings.spatialFilterEnabled && settings.spatialRadius > 0u && active){
        const u32 radiusIndex = settings.spatialRadius - 1u;
        if(
            !preparePipeline(
                m_spatialPipelines[radiusIndex], m_spatialShaders[radiusIndex], Name("engine/graphics/reflection/spatial_cs"),
                __hidden_reflection_postprocess_resources::s_SpatialVariants[radiusIndex]
            )
            || !prepareImage(m_spatial, 2u, Name("engine/reflection/opaque_spatial"))
            || !m_graphics.gpuTiming().prepareScopeQueries(ReflectionGpuTimingScope::s_Spatial.identity, device, 2u)
        )
            return false;
    }
    return true;
}

ReflectionPostprocessSnapshot RendererReflectionPostprocess::snapshot(
    const ReflectionRadianceBinding& base,
    const ReflectionSceneContentStamp& stamp,
    const ReflectionSettings& settings,
    const u64 graphicsFrameIndex)const{
    ReflectionPostprocessSnapshot result;
    if(!m_control)
        return result;
    result.control = m_control;
    result.history = m_control->plan(stamp, settings, graphicsFrameIndex);
    result.current = result.history.currentBank == 0u ? base : m_history;
    result.previous = result.history.previousBank == 0u ? base : m_history;
    result.spatial = m_spatial;
    result.temporalPipeline = m_temporalPipeline;
    result.spatialEnabled = settings.spatialFilterEnabled && settings.spatialRadius > 0u
        && settings.spatialRadius <= NWB_REFLECTION_SPATIAL_MAX_RADIUS && settings.traceMode != ReflectionTraceMode::Disabled;
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
        descriptor = Core::GpuDescriptorHandle::invalid();
    }
    m_history = {};
    m_spatial = {};
    m_width = 0u;
    m_height = 0u;
}

bool RendererReflectionPostprocess::preparePipeline(
    Core::ComputePipelineHandle& pipeline, Core::ShaderHandle& shader, const Name name, const AStringView variant){
    if(pipeline)
        return true;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!m_layout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES));
        m_layout = device.createBindingLayout(desc);
        if(!m_layout)
            return false;
    }
    if(!m_shaders.loadShader(shader, name, variant, Core::ShaderType::Compute, name))
        return false;
    Core::ComputePipelineDesc desc;
    desc
        .setComputeShader(shader)
        .addBindingLayout(m_layout)
        .addBindingLayout(heap.getResourceLayout())
        .addBindingLayout(heap.getSamplerLayout())
    ;
    pipeline = device.createComputePipeline(desc);
    return static_cast<bool>(pipeline);
}

bool RendererReflectionPostprocess::prepareImage(ReflectionRadianceBinding& image, const u32 descriptorIndex, const Name name){
    if(image.texture)
        return true;
    Core::TextureDesc desc;
    desc
        .setWidth(m_width)
        .setHeight(m_height)
        .setFormat(Core::Format::RGBA16_FLOAT)
        .setInUAV(true)
        .setName(name)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    Core::TextureHandle texture = m_graphics.createTexture(desc);
    if(!texture)
        return false;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    Core::GpuDescriptorHandle sampled = heap.allocate(Core::GpuDescriptorClass::SampledImage);
    Core::GpuDescriptorHandle storage = heap.allocate(Core::GpuDescriptorClass::StorageImage);
    if(
        !sampled.valid() || !storage.valid()
        || !heap.write(sampled, Core::DescriptorWriteItem::Texture_SRV(0u, texture.get()))
        || !heap.write(storage, Core::DescriptorWriteItem::Texture_UAV(0u, texture.get()))
    ){
        if(sampled.valid())
            heap.free(sampled);
        if(storage.valid())
            heap.free(storage);
        return false;
    }
    m_descriptors[descriptorIndex] = sampled;
    m_descriptors[descriptorIndex + 1u] = storage;
    image = ReflectionRadianceBinding{Move(texture), sampled.slot(), storage.slot()};
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

