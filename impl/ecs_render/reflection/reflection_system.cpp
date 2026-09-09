// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_system.h"
#include "timing_names.h"

#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_resources{
    enum DescriptorIndex : u32{
        OpaqueSampled,
        OpaqueStorage,
        GlassSampled,
        GlassStorage,
        Queue,
        Counters,
        Args,
        Parameters,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererReflectionSystem::RendererReflectionSystem(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    RendererShaderSystem& shaders
)
    : m_arena(arena)
    , m_graphics(graphics)
    , m_shaders(shaders)
{}

void RendererReflectionSystem::invalidateResources(){
    releaseTargets();
    m_resources.classifyPipeline = nullptr;
    m_resources.buildArgsPipeline = nullptr;
    m_resources.hardwarePipeline = nullptr;
    m_classifyShader = nullptr;
    m_buildArgsShader = nullptr;
    m_hardwareShader = nullptr;
    m_bindingLayout = nullptr;
}

bool RendererReflectionSystem::prepareResources(const u32 width, const u32 height, const bool prepareHardware){
    using namespace __hidden_reflection_resources;
    const u64 pixelCount = static_cast<u64>(width) * height;
    // Raw shader buffer addresses are u32 byte offsets, even when the host allocation size is wider.
    if(width == 0u || height == 0u || pixelCount > static_cast<u64>(s_MaxU32) / (2u * sizeof(u32)))
        return false;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !preparePipelines(prepareHardware))
        return false;
    const Core::GpuTimingScopeDefinition* const scopes[] = {
        &ReflectionGpuTimingScope::s_Classify,
        &ReflectionGpuTimingScope::s_BuildArgs,
        &ReflectionGpuTimingScope::s_Hardware,
    };
    for(const Core::GpuTimingScopeDefinition* const scope : scopes){
        if(!m_graphics.gpuTiming().prepareScopeQueries(scope->identity, device, 2u))
            return false;
    }
    if(m_resources.valid() && m_resources.parameters.width == width && m_resources.parameters.height == height)
        return true;
    releaseTargets();

    const auto createOutput = [&](const Name name){
        Core::TextureDesc desc;
        desc
            .setWidth(width)
            .setHeight(height)
            .setFormat(Core::Format::RGBA16_FLOAT)
            .setInUAV(true)
            .setName(name)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
            .setInitialState(Core::ResourceStates::Common)
            .setKeepInitialState(true)
        ;
        return m_graphics.createTexture(desc);
    };
    const auto createBuffer = [&](const Name name, const u64 byteSize, const bool uniform, const bool indirect){
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setIsConstantBuffer(uniform)
            .setCanHaveUAVs(!uniform)
            .setCanHaveRawViews(!uniform)
            .setIsDrawIndirectArgs(indirect)
            .setDebugName(name)
            .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        return m_graphics.createBuffer(desc);
    };
    m_resources.opaqueRadiance = createOutput(Name("engine/reflection/opaque_radiance"));
    m_resources.glassRadiance = createOutput(Name("engine/reflection/glass_radiance"));
    m_resources.queue = createBuffer(Name("engine/reflection/ray_queue"), pixelCount * 2u * sizeof(u32), false, false);
    m_resources.counters = createBuffer(Name("engine/reflection/counters"), NWB_REFLECTION_COUNTER_SIZE, false, false);
    m_resources.indirectArgs = createBuffer(Name("engine/reflection/indirect_args"), 3u * sizeof(u32), false, true);
    m_resources.frameParameters = createBuffer(
        Name("engine/reflection/frame_parameters"), sizeof(ReflectionFrameParameters), true, false
    );
    if(
        !m_resources.opaqueRadiance || !m_resources.glassRadiance || !m_resources.queue
        || !m_resources.counters || !m_resources.indirectArgs || !m_resources.frameParameters
    ){
        releaseTargets();
        return false;
    }

    const auto registerDescriptor = [&](
        const u32 index,
        const Core::GpuDescriptorClass::Enum descriptorClass,
        const Core::DescriptorWriteItem& item){
        Core::GpuDescriptorHandle& descriptor = m_descriptors[index];
        descriptor = heap.allocate(descriptorClass);
        return descriptor.valid() && heap.write(descriptor, item);
    };
    if(
        !registerDescriptor(
            OpaqueSampled, Core::GpuDescriptorClass::SampledImage,
            Core::DescriptorWriteItem::Texture_SRV(0u, m_resources.opaqueRadiance.get())
        )
        || !registerDescriptor(
            OpaqueStorage, Core::GpuDescriptorClass::StorageImage,
            Core::DescriptorWriteItem::Texture_UAV(0u, m_resources.opaqueRadiance.get())
        )
        || !registerDescriptor(
            GlassSampled, Core::GpuDescriptorClass::SampledImage,
            Core::DescriptorWriteItem::Texture_SRV(0u, m_resources.glassRadiance.get())
        )
        || !registerDescriptor(
            GlassStorage, Core::GpuDescriptorClass::StorageImage,
            Core::DescriptorWriteItem::Texture_UAV(0u, m_resources.glassRadiance.get())
        )
        || !registerDescriptor(
            Queue, Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.queue.get())
        )
        || !registerDescriptor(
            Counters, Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.counters.get())
        )
        || !registerDescriptor(
            Args, Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.indirectArgs.get())
        )
        || !registerDescriptor(
            Parameters, Core::GpuDescriptorClass::UniformBuffer,
            Core::DescriptorWriteItem::ConstantBuffer(0u, m_resources.frameParameters.get())
        )
    ){
        releaseTargets();
        return false;
    }
    m_resources.parameters.width = width;
    m_resources.parameters.height = height;
    m_resources.parameters.queueCapacity = static_cast<u32>(pixelCount * 2u);
    m_resources.parameters.opaqueOutputSlot = m_descriptors[OpaqueStorage].slot();
    m_resources.parameters.glassOutputSlot = m_descriptors[GlassStorage].slot();
    m_resources.parameters.opaqueRadianceSlot = m_descriptors[OpaqueSampled].slot();
    m_resources.parameters.glassRadianceSlot = m_descriptors[GlassSampled].slot();
    m_resources.parameters.queueSlot = m_descriptors[Queue].slot();
    m_resources.parameters.counterSlot = m_descriptors[Counters].slot();
    m_resources.parameters.argsSlot = m_descriptors[Args].slot();
    m_resources.frameParametersSlot = m_descriptors[Parameters].slot();
    return true;
}

ReflectionFrameSnapshot RendererReflectionSystem::snapshotFrameResources(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& view,
    const RayTracingSceneGraphResources& scene,
    const ReflectionSettings& settings,
    const u32 frameIndex)const{
    if(
        !m_resources.valid() || !view.bindingValid() || !ValidateReflectionSettings(settings)
        || targets.width != m_resources.parameters.width || targets.height != m_resources.parameters.height
        || !targets.bindless.slotsBufferDescriptor.valid() || !targets.bindless.gbufferSpecularRoughness.valid()
        || !targets.bindless.refractionSpecularRoughness.valid()
    )
        return {};
    ReflectionFrameSnapshot snapshot = m_resources;
    ReflectionFrameParameters& parameters = snapshot.parameters;
    parameters.traceMode = static_cast<u32>(settings.traceMode);
    const bool hardwareRequested = settings.traceMode == ReflectionTraceMode::Hardware || settings.traceMode == ReflectionTraceMode::Hybrid;
    parameters.hardwareEnabled = scene.valid() && snapshot.hardwarePipeline && hardwareRequested ? 1u : 0u;
    parameters.opaqueSpecularSlot = targets.bindless.gbufferSpecularRoughness.slot();
    parameters.glassSpecularSlot = targets.bindless.refractionSpecularRoughness.slot();
    parameters.maxHardwareRays = Min(settings.maxHardwareRaysPerFrame, parameters.queueCapacity);
    parameters.frameIndex = frameIndex;
    parameters.deferredResourcesSlot = targets.bindless.slotsBufferDescriptor.slot();
    parameters.viewSlot = view.heapHandle.slot();
    parameters.materialContextSlot = parameters.hardwareEnabled != 0u ? scene.materialContextSlotsHeapSlot : 0u;
    parameters.debugView = static_cast<u32>(settings.debugView);
    parameters.maxRayDistance = settings.maxRayDistance;
    parameters.distanceFadeStart = settings.distanceFadeStart;
    parameters.roughnessCutoff = settings.roughnessCutoff;
    parameters.environmentTopR = settings.environmentTop.x;
    parameters.environmentTopG = settings.environmentTop.y;
    parameters.environmentTopB = settings.environmentTop.z;
    parameters.environmentBottomR = settings.environmentBottom.x;
    parameters.environmentBottomG = settings.environmentBottom.y;
    parameters.environmentBottomB = settings.environmentBottom.z;
    if(parameters.hardwareEnabled != 0u)
        snapshot.scene = scene;
    return snapshot;
}


void RendererReflectionSystem::releaseTargets(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    for(Core::GpuDescriptorHandle& descriptor : m_descriptors){
        if(descriptor.valid() && heap.isInitialized())
            heap.free(descriptor);
        descriptor = Core::GpuDescriptorHandle::invalid();
    }
    m_resources.opaqueRadiance = nullptr;
    m_resources.glassRadiance = nullptr;
    m_resources.queue = nullptr;
    m_resources.counters = nullptr;
    m_resources.indirectArgs = nullptr;
    m_resources.frameParameters = nullptr;
    m_resources.parameters = {};
    m_resources.frameParametersSlot = 0u;
    m_resources.scene = {};
}

bool RendererReflectionSystem::preparePipelines(const bool prepareHardware){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!m_bindingLayout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(u32)));
        m_bindingLayout = device.createBindingLayout(desc);
        if(!m_bindingLayout)
            return false;
    }
    const auto preparePipeline = [&](
        Core::ComputePipelineHandle& pipeline,
        Core::ShaderHandle& shader,
        const Name shaderName,
        const Name debugName,
        const bool hardware){
        if(pipeline)
            return true;
        if(!m_shaders.loadShader(
            shader, shaderName,
            hardware ? AStringView("NWB_BINDLESS_TLAS=1") : Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute, debugName
        ))
            return false;
        Core::ComputePipelineDesc desc;
        desc
            .setComputeShader(shader)
            .addBindingLayout(m_bindingLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        if(hardware)
            desc.addBindingLayout(heap.getAccelStructLayout());
        pipeline = device.createComputePipeline(desc);
        return static_cast<bool>(pipeline);
    };
    if(
        !preparePipeline(
            m_resources.classifyPipeline, m_classifyShader,
            Name("engine/graphics/reflection/classify_cs"), Name("ECSRender_ReflectionClassify"), false
        )
        || !preparePipeline(
            m_resources.buildArgsPipeline, m_buildArgsShader,
            Name("engine/graphics/reflection/build_args_cs"), Name("ECSRender_ReflectionBuildArgs"), false
        )
    )
        return false;
    if(
        prepareHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout()
        && !preparePipeline(
            m_resources.hardwarePipeline, m_hardwareShader,
            Name("engine/graphics/reflection/resolve_hw_cs"), Name("ECSRender_ReflectionHardware"), true
        )
    )
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

