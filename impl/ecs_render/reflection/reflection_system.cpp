// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_system.h"
#include "timing_names.h"

#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/vulkan/backend.h>

#include <global/basic_string.h>


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
    , m_statistics(arena, graphics)
    , m_postprocess(arena, graphics, shaders)
{}

void RendererReflectionSystem::invalidateResources(){
    m_statistics.invalidateResources();
    releaseTargets();
    m_resources.classifyPipeline = nullptr;
    m_resources.buildArgsPipeline = nullptr;
    m_resources.hardwarePipeline = nullptr;
    m_resources.depthPyramid.pipeline = nullptr;
    m_classifyShader = nullptr;
    m_buildArgsShader = nullptr;
    m_hardwareShader = nullptr;
    m_depthShader = nullptr;
    m_bindingLayout = nullptr;
    m_depthBindingLayout = nullptr;
}

bool RendererReflectionSystem::prepareResources(
    const u32 width,
    const u32 height,
    const bool prepareHardware,
    const ReflectionSettings& settings){
    using namespace __hidden_reflection_resources;
    const u64 pixelCount = static_cast<u64>(width) * height;
    // Packed pixels cover both surface families; queue addresses additionally require u32 byte offsets.
    if(width == 0u || height == 0u || pixelCount > static_cast<u64>(s_MaxU32) / 2u)
        return false;
    const u64 boundedCapacity = Max(static_cast<u64>(1u), Min(pixelCount * 2u, static_cast<u64>(settings.maxHardwareRaysPerFrame)));
    if(boundedCapacity > static_cast<u64>(s_MaxU32) / sizeof(u32))
        return false;
    const u32 capacity = static_cast<u32>(boundedCapacity);
    if(
        m_resources.valid() && m_resources.parameters.width == width && m_resources.parameters.height == height
        && (!prepareHardware || m_resources.hardwarePipeline)
    )
        return prepareQueue(capacity) && m_statistics.prepareResources() && m_postprocess.prepareResources(width, height, settings);
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
    if(!m_graphics.gpuTiming().prepareScopeQueries(
        ReflectionGpuTimingScope::s_DepthPyramid.identity, device, 2u * ReflectionDepthPyramidSnapshot::s_MaxMipCount
    ))
        return false;
    if(m_resources.valid() && m_resources.parameters.width == width && m_resources.parameters.height == height)
        return prepareQueue(capacity) && m_statistics.prepareResources() && m_postprocess.prepareResources(width, height, settings);
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
    m_resources.queue = createBuffer(Name("engine/reflection/ray_queue"), static_cast<u64>(capacity) * sizeof(u32), false, false);
    m_resources.counters = createBuffer(Name("engine/reflection/counters"), NWB_REFLECTION_COUNTER_SIZE, false, false);
    m_resources.indirectArgs = createBuffer(Name("engine/reflection/indirect_args"), 3u * sizeof(u32), false, true);
    m_resources.frameParameters = createBuffer(
        Name("engine/reflection/frame_parameters"), sizeof(ReflectionFrameParameters), true, false
    );
    ReflectionDepthPyramidSnapshot& depthPyramid = m_resources.depthPyramid;
    u32 mipWidth = width;
    u32 mipHeight = height;
    do{
        ReflectionDepthPyramidMip& mip = depthPyramid.mips[depthPyramid.mipCount++];
        const auto taskIdentity = StringFormat(m_arena, "render.reflection.depth_reduce_{}", depthPyramid.mipCount - 1u);
        mip.taskIdentity = ToName(taskIdentity);
        mip.width = mipWidth;
        mip.height = mipHeight;
        if(mipWidth == 1u && mipHeight == 1u)
            break;
        mipWidth = Max(mipWidth / 2u, 1u);
        mipHeight = Max(mipHeight / 2u, 1u);
    }while(depthPyramid.mipCount < ReflectionDepthPyramidSnapshot::s_MaxMipCount);
    constexpr Core::FormatSupport::Mask depthSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;
    Core::Format::Enum depthFormat = Core::Format::RG32_FLOAT;
    if((device.queryFormatSupport(depthFormat) & depthSupport) != depthSupport)
        depthFormat = Core::Format::RGBA32_FLOAT;
    if((device.queryFormatSupport(depthFormat) & depthSupport) != depthSupport){
        releaseTargets();
        return false;
    }
    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(width)
        .setHeight(height)
        .setMipLevels(depthPyramid.mipCount)
        .setFormat(depthFormat)
        .setInUAV(true)
        .setName(Name("engine/reflection/depth_pyramid"))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    depthPyramid.texture = m_graphics.createTexture(depthDesc);
    if(
        !m_resources.opaqueRadiance || !m_resources.glassRadiance || !m_resources.queue
        || !m_resources.counters || !m_resources.indirectArgs || !m_resources.frameParameters || !depthPyramid.texture
    ){
        releaseTargets();
        return false;
    }

    const auto registerDescriptor = [&](
        Core::GpuDescriptorHandle& descriptor,
        const Core::GpuDescriptorClass::Enum descriptorClass,
        const Core::DescriptorWriteItem& item){
        descriptor = heap.allocate(descriptorClass);
        return descriptor.valid() && heap.write(descriptor, item);
    };
    if(
        !registerDescriptor(
            m_descriptors[OpaqueSampled], Core::GpuDescriptorClass::SampledImage,
            Core::DescriptorWriteItem::Texture_SRV(0u, m_resources.opaqueRadiance.get())
        )
        || !registerDescriptor(
            m_descriptors[OpaqueStorage], Core::GpuDescriptorClass::StorageImage,
            Core::DescriptorWriteItem::Texture_UAV(0u, m_resources.opaqueRadiance.get())
        )
        || !registerDescriptor(
            m_descriptors[GlassSampled], Core::GpuDescriptorClass::SampledImage,
            Core::DescriptorWriteItem::Texture_SRV(0u, m_resources.glassRadiance.get())
        )
        || !registerDescriptor(
            m_descriptors[GlassStorage], Core::GpuDescriptorClass::StorageImage,
            Core::DescriptorWriteItem::Texture_UAV(0u, m_resources.glassRadiance.get())
        )
        || !registerDescriptor(
            m_descriptors[Queue], Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.queue.get())
        )
        || !registerDescriptor(
            m_descriptors[Counters], Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.counters.get())
        )
        || !registerDescriptor(
            m_descriptors[Args], Core::GpuDescriptorClass::StorageBuffer,
            Core::DescriptorWriteItem::RawBuffer_UAV(0u, m_resources.indirectArgs.get())
        )
        || !registerDescriptor(
            m_descriptors[Parameters], Core::GpuDescriptorClass::UniformBuffer,
            Core::DescriptorWriteItem::ConstantBuffer(0u, m_resources.frameParameters.get())
        )
    ){
        releaseTargets();
        return false;
    }
    if(!registerDescriptor(
        m_depthSampledDescriptor, Core::GpuDescriptorClass::SampledImage,
        Core::DescriptorWriteItem::Texture_SRV(0u, depthPyramid.texture.get())
    )){
        releaseTargets();
        return false;
    }
    depthPyramid.sampledSlot = m_depthSampledDescriptor.slot();
    for(u32 mipIndex = 0u; mipIndex < depthPyramid.mipCount; ++mipIndex){
        const Core::TextureSubresourceSet subresources(mipIndex, 1u, 0u, 1u);
        if(
            !registerDescriptor(
                m_depthMipSampledDescriptors[mipIndex], Core::GpuDescriptorClass::SampledImage,
                Core::DescriptorWriteItem::Texture_SRV(0u, depthPyramid.texture.get(), depthFormat, subresources)
            )
            || !registerDescriptor(
                m_depthMipStorageDescriptors[mipIndex], Core::GpuDescriptorClass::StorageImage,
                Core::DescriptorWriteItem::Texture_UAV(0u, depthPyramid.texture.get(), depthFormat, subresources)
            )
        ){
            releaseTargets();
            return false;
        }
        depthPyramid.mips[mipIndex].sampledSlot = m_depthMipSampledDescriptors[mipIndex].slot();
        depthPyramid.mips[mipIndex].storageSlot = m_depthMipStorageDescriptors[mipIndex].slot();
    }
    m_resources.parameters.width = width;
    m_resources.parameters.height = height;
    m_resources.parameters.queueCapacity = capacity;
    m_resources.parameters.opaqueOutputSlot = m_descriptors[OpaqueStorage].slot();
    m_resources.parameters.glassOutputSlot = m_descriptors[GlassStorage].slot();
    m_resources.parameters.opaqueRadianceSlot = m_descriptors[OpaqueSampled].slot();
    m_resources.parameters.glassRadianceSlot = m_descriptors[GlassSampled].slot();
    m_resources.parameters.queueSlot = m_descriptors[Queue].slot();
    m_resources.parameters.counterSlot = m_descriptors[Counters].slot();
    m_resources.parameters.argsSlot = m_descriptors[Args].slot();
    m_resources.parameters.depthPyramidSlot = depthPyramid.sampledSlot;
    m_resources.parameters.depthMipCount = depthPyramid.mipCount;
    m_resources.frameParametersSlot = m_descriptors[Parameters].slot();
    return m_statistics.prepareResources() && m_postprocess.prepareResources(width, height, settings);
}

void RendererReflectionSystem::pollStatistics(){
    m_statistics.pollCompleted();
}

bool RendererReflectionSystem::tryGetLatestStatistics(ReflectionStatistics& outStatistics)const{
    return m_statistics.tryGetLatestStatistics(outStatistics);
}

ReflectionFrameSnapshot RendererReflectionSystem::snapshotFrameResources(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& view,
    const RayTracingSceneGraphResources& scene,
    const ReflectionSettings& settings,
    const u32 frameIndex,
    const ReflectionSceneContentStamp& stamp)const{
    if(
        !m_resources.valid() || !view.bindingValid() || !ValidateReflectionSettings(settings)
        || targets.width != m_resources.parameters.width || targets.height != m_resources.parameters.height
        || !targets.bindless.slotsBufferDescriptor.valid() || !targets.bindless.gbufferSpecularRoughness.valid()
        || !targets.bindless.refractionSpecularRoughness.valid()
        || !targets.bindless.gbufferDepth.valid()
    )
        return {};
    ReflectionFrameSnapshot snapshot = m_resources;
    const ReflectionRadianceBinding base{
        m_resources.opaqueRadiance, m_resources.parameters.opaqueRadianceSlot, m_resources.parameters.opaqueOutputSlot,
    };
    snapshot.postprocess = m_postprocess.snapshot(
        base, stamp, settings, m_graphics.getFrameIndex()
    );
    if(!snapshot.postprocess.control || !snapshot.postprocess.current.texture)
        return {};
    snapshot.opaqueRadiance = snapshot.postprocess.current.texture;
    ReflectionFrameParameters& parameters = snapshot.parameters;
    parameters.traceMode = static_cast<u32>(settings.traceMode);
    const bool hardwareRequested = settings.traceMode == ReflectionTraceMode::Hardware || settings.traceMode == ReflectionTraceMode::Hybrid;
    parameters.hardwareEnabled = scene.valid() && snapshot.hardwarePipeline && hardwareRequested ? 1u : 0u;
    parameters.opaqueSpecularSlot = targets.bindless.gbufferSpecularRoughness.slot();
    parameters.glassSpecularSlot = targets.bindless.refractionSpecularRoughness.slot();
    parameters.maxHardwareRays = Min(settings.maxHardwareRaysPerFrame, parameters.queueCapacity);
    parameters.sampleIndex = snapshot.postprocess.history.sampleIndex;
    parameters.samplingSeed = settings.samplingSeed;
    parameters.opaqueOutputSlot = snapshot.postprocess.current.storageSlot;
    parameters.opaqueRadianceSlot = snapshot.postprocess.spatialEnabled
        ? snapshot.postprocess.spatial.sampledSlot : snapshot.postprocess.current.sampledSlot;
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
    parameters.screenMaxSteps = settings.screenMaxSteps;
    parameters.screenThickness = settings.screenThickness;
    parameters.screenConfidenceThreshold = settings.screenConfidenceThreshold;
    parameters.screenEdgeFade = settings.screenEdgeFade;
    parameters.diagnosticsEnabled = settings.diagnosticsEnabled ? 1u : 0u;
    snapshot.postprocess.deferredResourcesSlot = parameters.deferredResourcesSlot;
    snapshot.postprocess.opaqueSpecularSlot = parameters.opaqueSpecularSlot;
    snapshot.postprocess.viewSlot = parameters.viewSlot;
    snapshot.depthPyramid.sourceDepthSlot = targets.bindless.gbufferDepth.slot();
    if(parameters.hardwareEnabled != 0u)
        snapshot.scene = scene;
    if(settings.diagnosticsEnabled){
        ReflectionStatistics metadata;
        metadata.frameIndex = frameIndex;
        metadata.graphicsFrameIndex = m_graphics.getFrameIndex();
        metadata.samplingSeed = settings.samplingSeed;
        metadata.width = parameters.width;
        metadata.height = parameters.height;
        metadata.requestedHardwareBudget = settings.maxHardwareRaysPerFrame;
        metadata.effectiveHardwareBudget = parameters.maxHardwareRays;
        metadata.queueCapacity = parameters.queueCapacity;
        metadata.traceMode = settings.traceMode;
        metadata.hardwareRequested = hardwareRequested;
        metadata.hardwareAvailable = scene.valid() && snapshot.hardwarePipeline;
        snapshot.statistics = m_statistics.snapshot(metadata);
    }
    return snapshot;
}


void RendererReflectionSystem::releaseTargets(){
    m_postprocess.invalidateResources();
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    const auto retireDescriptor = [&](Core::GpuDescriptorHandle& descriptor){
        if(descriptor.valid() && heap.isInitialized())
            heap.free(descriptor);
        descriptor = Core::GpuDescriptorHandle::invalid();
    };
    for(Core::GpuDescriptorHandle& descriptor : m_descriptors)
        retireDescriptor(descriptor);
    retireDescriptor(m_depthSampledDescriptor);
    for(Core::GpuDescriptorHandle& descriptor : m_depthMipSampledDescriptors)
        retireDescriptor(descriptor);
    for(Core::GpuDescriptorHandle& descriptor : m_depthMipStorageDescriptors)
        retireDescriptor(descriptor);
    m_resources.opaqueRadiance = nullptr;
    m_resources.glassRadiance = nullptr;
    m_resources.queue = nullptr;
    m_resources.counters = nullptr;
    m_resources.indirectArgs = nullptr;
    m_resources.frameParameters = nullptr;
    m_resources.parameters = {};
    m_resources.frameParametersSlot = 0u;
    m_resources.scene = {};
    m_resources.statistics = {};
    m_resources.postprocess = {};
    m_resources.depthPyramid.texture = nullptr;
    m_resources.depthPyramid.mipCount = 0u;
    m_resources.depthPyramid.sampledSlot = 0u;
    m_resources.depthPyramid.sourceDepthSlot = 0u;
    for(ReflectionDepthPyramidMip& mip : m_resources.depthPyramid.mips)
        mip = {};
}

bool RendererReflectionSystem::prepareQueue(const u32 capacity){
    using namespace __hidden_reflection_resources;
    if(m_resources.queue && m_resources.parameters.queueCapacity == capacity)
        return true;
    Core::BufferDesc desc;
    desc
        .setByteSize(static_cast<u64>(capacity) * sizeof(u32))
        .setCanHaveUAVs(true)
        .setCanHaveRawViews(true)
        .setDebugName(Name("engine/reflection/ray_queue"))
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle queue = m_graphics.createBuffer(desc);
    if(!queue)
        return false;
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    const Core::GpuDescriptorHandle descriptor = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
    if(!descriptor.valid())
        return false;
    if(!heap.write(descriptor, Core::DescriptorWriteItem::RawBuffer_UAV(0u, queue.get()))){
        heap.free(descriptor);
        return false;
    }
    // Publish the replacement only after allocation and descriptor writing succeeded. Accepted command lists and
    // frozen graph snapshots retain the old buffer; descriptor retirement protects its old selector independently.
    if(m_descriptors[Queue].valid())
        heap.free(m_descriptors[Queue]);
    m_descriptors[Queue] = descriptor;
    m_resources.queue = Move(queue);
    m_resources.parameters.queueCapacity = capacity;
    m_resources.parameters.queueSlot = descriptor.slot();
    return true;
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
    if(!m_depthBindingLayout){
        Core::BindingLayoutDesc desc(m_arena);
        desc.setVisibility(Core::ShaderType::Compute);
        desc.addItem(Core::BindingLayoutItem::PushConstants(0u, NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES));
        m_depthBindingLayout = device.createBindingLayout(desc);
        if(!m_depthBindingLayout)
            return false;
    }
    const auto preparePipeline = [&](
        Core::ComputePipelineHandle& pipeline,
        Core::ShaderHandle& shader,
        const Name shaderName,
        const Name debugName,
        const bool hardware,
        const Core::BindingLayoutHandle& bindingLayout){
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
            .addBindingLayout(bindingLayout)
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
            Name("engine/graphics/reflection/classify_cs"), Name("ECSRender_ReflectionClassify"), false, m_bindingLayout
        )
        || !preparePipeline(
            m_resources.buildArgsPipeline, m_buildArgsShader,
            Name("engine/graphics/reflection/build_args_cs"), Name("ECSRender_ReflectionBuildArgs"), false, m_bindingLayout
        )
        || !preparePipeline(
            m_resources.depthPyramid.pipeline, m_depthShader,
            Name("engine/graphics/reflection/depth_reduce_cs"), Name("ECSRender_ReflectionDepthReduce"), false, m_depthBindingLayout
        )
    )
        return false;
    if(
        prepareHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout()
        && !preparePipeline(
            m_resources.hardwarePipeline, m_hardwareShader,
            Name("engine/graphics/reflection/resolve_hw_cs"), Name("ECSRender_ReflectionHardware"), true, m_bindingLayout
        )
    )
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

