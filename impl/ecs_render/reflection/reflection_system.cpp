// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_system.h"
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>

#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/backend_selection/backend.h>

#include <global/basic_string.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace DescriptorIndex = ReflectionDescriptorIndex;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererReflectionSystem::invalidateResources(){
    m_preparedCsgDispatch = nullptr;
    m_csgDispatchStates.clear();
    m_statistics.resetReadbackBuffers();
    releaseTargets();
    m_resources.classifyPipeline = nullptr;
    m_resources.buildArgsPipeline = nullptr;
    m_resources.hardwarePipeline = nullptr;
    m_resources.depthPyramid.pipeline = nullptr;
    m_classifyShader = nullptr;
    m_buildArgsShader = nullptr;
    m_hardwareShader = nullptr;
    m_plainHardwareShader = nullptr;
    m_plainHardwarePipeline = nullptr;
    m_unspecifiedHardwareShader = nullptr;
    m_unspecifiedHardwarePipeline = nullptr;
    m_csgBuildArgsShader = nullptr;
    m_csgBuildArgsPipeline = nullptr;
    m_csgHardwareShader = nullptr;
    m_csgPlainHardwareShader = nullptr;
    m_csgUnspecifiedHardwareShader = nullptr;
    m_csgHardwarePipeline = nullptr;
    m_csgPlainHardwarePipeline = nullptr;
    m_csgUnspecifiedHardwarePipeline = nullptr;
    m_depthShader = nullptr;
    m_bindingLayout = nullptr;
    m_depthBindingLayout = nullptr;
    m_csgBindingLayout = nullptr;
}

b8 RendererReflectionSystem::prepareResources(const u32 width, const u32 height, const b8 prepareHardware, const b8 prepareCsgHardware, const ReflectionSettings& settings){
    using namespace __hidden_reflection_system;
    const Expected<u32> capacityResult = TryResolveReflectionQueueCapacity(width, height, settings.maxHardwareRaysPerFrame);
    if(!capacityResult)
        return false;
    const u32 capacity = *capacityResult;
    const b8 csgHardwareWork = prepareCsgHardware && settings.maxHardwareRaysPerFrame > 0u && (settings.traceMode == ReflectionTraceMode::Hardware || settings.traceMode == ReflectionTraceMode::Hybrid);
    if(csgHardwareWork && !prepareCsgDispatchResources())
        return false;
    if(
        m_resources.valid() && m_resources.parameters.width == width && m_resources.parameters.height == height
        && (!prepareHardware || (m_resources.hardwarePipeline && m_plainHardwarePipeline && m_unspecifiedHardwarePipeline))
        && (!csgHardwareWork || (m_csgBuildArgsPipeline && m_csgHardwarePipeline && m_csgPlainHardwarePipeline && m_csgUnspecifiedHardwarePipeline))
    )
        return
            prepareQueue(capacity) && prepareArguments(capacity, csgHardwareWork && m_csgBuildArgsPipeline)
            && m_statistics.prepareResources()
            && m_postprocess.prepareResources(width, height, settings)
            && m_feedback.prepareResources(width, height, settings, settings.screenFeedbackEnabled)
        ;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !preparePipelines(prepareHardware, csgHardwareWork))
        return false;
    const Array<NotNull<const Core::GpuTimingScopeDefinition*>, 3> scopes = { MakeNotNull(&ReflectionGpuTimingScope::s_Classify), MakeNotNull(&ReflectionGpuTimingScope::s_BuildArgs), MakeNotNull(&ReflectionGpuTimingScope::s_Hardware), };
    for(const NotNull<const Core::GpuTimingScopeDefinition*> scope : scopes){
        if(!m_graphics.gpuTiming().prepareScopeQueries(scope->identity, device, s_ReflectionGpuTimingQueriesPerRange))
            return false;
    }
    if(!m_graphics.gpuTiming().prepareScopeQueries(ReflectionGpuTimingScope::s_DepthPyramid.identity, device, s_ReflectionGpuTimingQueriesPerRange * s_ReflectionMaxDepthMips))
        return false;
    if(m_resources.valid() && m_resources.parameters.width == width && m_resources.parameters.height == height)
        return
            prepareQueue(capacity) && prepareArguments(capacity, csgHardwareWork && m_csgBuildArgsPipeline)
            && m_statistics.prepareResources()
            && m_postprocess.prepareResources(width, height, settings)
            && m_feedback.prepareResources(width, height, settings, settings.screenFeedbackEnabled)
        ;
    releaseTargets();

    const auto createOutput = [&](const Name name){
        Core::TextureDesc desc;
        desc.setWidth(width).setHeight(height).setFormat(Core::Format::RGBA16_FLOAT).setIsUAV(true).setName(name).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setInitialState(Core::ResourceStates::Common).setKeepInitialState(true);
        return m_graphics.createTexture(desc);
    };
    const auto createBuffer = [&](const Name name, const u64 byteSize, const b8 uniform, const b8 indirect){
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
    auto opaqueRadianceResult = createOutput(s_ReflectionOpaqueRadianceName);
    if(opaqueRadianceResult)
        m_resources.opaqueRadiance = Move(*opaqueRadianceResult);
    else
        m_resources.opaqueRadiance.reset();
    auto glassRadianceResult = createOutput(s_ReflectionGlassRadianceName);
    if(glassRadianceResult)
        m_resources.glassRadiance = Move(*glassRadianceResult);
    else
        m_resources.glassRadiance.reset();
    auto queueResult = createBuffer(s_ReflectionRayQueueBufferName, static_cast<u64>(capacity) * sizeof(u32), false, false);
    if(queueResult)
        m_resources.queue = Move(*queueResult);
    else
        m_resources.queue.reset();
    auto countersResult = createBuffer(s_ReflectionCountersBufferName, NWB_REFLECTION_COUNTER_SIZE, false, false);
    if(countersResult)
        m_resources.counters = Move(*countersResult);
    else
        m_resources.counters.reset();
    const u32 argumentCount = ResolveReflectionIndirectArgumentCount(capacity, csgHardwareWork && m_csgBuildArgsPipeline);
    auto indirectArgsResult = createBuffer(s_ReflectionIndirectArgsBufferName, static_cast<u64>(argumentCount) * s_ReflectionIndirectArgumentBytes, false, true);
    if(indirectArgsResult)
        m_resources.indirectArgs = Move(*indirectArgsResult);
    else
        m_resources.indirectArgs.reset();
    auto frameParametersResult = createBuffer(s_ReflectionFrameParametersBufferName, sizeof(ReflectionFrameParameters), true, false);
    if(frameParametersResult)
        m_resources.frameParameters = Move(*frameParametersResult);
    else
        m_resources.frameParameters.reset();
    ReflectionDepthPyramidSnapshot& depthPyramid = m_resources.depthPyramid;
    depthPyramid.mipCount = BuildReflectionDepthPyramidMips(width, height, depthPyramid.mips);
    constexpr Core::FormatSupport::Mask s_DepthSupport = Core::FormatSupport::Texture | Core::FormatSupport::ShaderUavStore;
    Core::Format::Enum depthFormat = Core::Format::RG32_FLOAT;
    if((device.queryFormatSupport(depthFormat) & s_DepthSupport) != s_DepthSupport)
        depthFormat = Core::Format::RGBA32_FLOAT;
    if((device.queryFormatSupport(depthFormat) & s_DepthSupport) != s_DepthSupport){
        releaseTargets();
        return false;
    }
    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(width)
        .setHeight(height)
        .setMipLevels(depthPyramid.mipCount)
        .setFormat(depthFormat)
        .setIsUAV(true)
        .setName(s_ReflectionDepthPyramidName)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setInitialState(Core::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    auto textureResult = m_graphics.createTexture(depthDesc);
    if(textureResult)
        depthPyramid.texture = Move(*textureResult);
    else
        depthPyramid.texture.reset();
    if(!opaqueRadianceResult || !glassRadianceResult || !queueResult || !countersResult || !indirectArgsResult || !frameParametersResult || !textureResult){
        releaseTargets();
        return false;
    }

    const auto registerDescriptor = [&](Core::GpuDescriptorHandle& descriptor, const Core::GpuDescriptorClass::Enum descriptorClass, const Core::DescriptorWriteItem& item){
        return RendererDescriptorRegisterDetail::AssignDescriptor(heap, descriptorClass, item, descriptor);
    };
    if(
        !registerDescriptor(m_descriptors[DescriptorIndex::OpaqueSampled], Core::GpuDescriptorClass::SampledImage, Core::DescriptorWriteItem::TextureSrv(0u, m_resources.opaqueRadiance.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::OpaqueStorage], Core::GpuDescriptorClass::StorageImage, Core::DescriptorWriteItem::TextureUav(0u, m_resources.opaqueRadiance.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::GlassSampled], Core::GpuDescriptorClass::SampledImage, Core::DescriptorWriteItem::TextureSrv(0u, m_resources.glassRadiance.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::GlassStorage], Core::GpuDescriptorClass::StorageImage, Core::DescriptorWriteItem::TextureUav(0u, m_resources.glassRadiance.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::Queue], Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, m_resources.queue.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::Counters], Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, m_resources.counters.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::Args], Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, m_resources.indirectArgs.get()))
        || !registerDescriptor(m_descriptors[DescriptorIndex::Parameters], Core::GpuDescriptorClass::UniformBuffer, Core::DescriptorWriteItem::ConstantBuffer(0u, m_resources.frameParameters.get()))
    ){
        releaseTargets();
        return false;
    }
    if(!registerDescriptor(m_depthSampledDescriptor, Core::GpuDescriptorClass::SampledImage, Core::DescriptorWriteItem::TextureSrv(0u, depthPyramid.texture.get()))){
        releaseTargets();
        return false;
    }
    depthPyramid.sampledSlot = m_depthSampledDescriptor.slot();
    for(u32 mipIndex = 0u; mipIndex < depthPyramid.mipCount; ++mipIndex){
        const Core::TextureSubresourceSet subresources(mipIndex, 1u, 0u, 1u);
        if(
            !registerDescriptor(m_depthMipSampledDescriptors[mipIndex], Core::GpuDescriptorClass::SampledImage, Core::DescriptorWriteItem::TextureSrv(0u, depthPyramid.texture.get(), depthFormat, subresources))
            || !registerDescriptor(m_depthMipStorageDescriptors[mipIndex], Core::GpuDescriptorClass::StorageImage, Core::DescriptorWriteItem::TextureUav(0u, depthPyramid.texture.get(), depthFormat, subresources))
        ){
            releaseTargets();
            return false;
        }
        depthPyramid.mips[mipIndex].sampledSlot = m_depthMipSampledDescriptors[mipIndex].slot();
        depthPyramid.mips[mipIndex].storageSlot = m_depthMipStorageDescriptors[mipIndex].slot();
    }
    PopulateReflectionPreparedFrameParameters(
        width,
        height,
        capacity,
        m_descriptors[DescriptorIndex::OpaqueStorage].slot(),
        m_descriptors[DescriptorIndex::GlassStorage].slot(),
        m_descriptors[DescriptorIndex::OpaqueSampled].slot(),
        m_descriptors[DescriptorIndex::GlassSampled].slot(),
        m_descriptors[DescriptorIndex::Queue].slot(),
        m_descriptors[DescriptorIndex::Counters].slot(),
        m_descriptors[DescriptorIndex::Args].slot(),
        depthPyramid.sampledSlot,
        depthPyramid.mipCount,
        m_resources.parameters
    );
    m_resources.frameParametersSlot = m_descriptors[DescriptorIndex::Parameters].slot();
    return m_statistics.prepareResources() && m_postprocess.prepareResources(width, height, settings) && m_feedback.prepareResources(width, height, settings, settings.screenFeedbackEnabled);
}

Expected<ReflectionCsgDispatchHandle> RendererReflectionSystem::acquireCsgDispatchState(const ReflectionFrameSnapshot& resources)noexcept{
    if(!resources.hasHardwareWork() || !resources.scene.csgTraceContextBuffer)
        return ReflectionCsgDispatchHandle{};
    ReflectionCsgDispatchControl* const control = m_preparedCsgDispatch;
    if(!control || control->getReferenceCount() != 1u)
        return MakeUnexpected(Failure{});
    AtomicThreadFence(MemoryOrder::acquire);
    control->resources = resources;
    return ReflectionCsgDispatchHandle(control, ArenaRefDeleter<ReflectionCsgDispatchControl, Core::Alloc::GlobalArena>(&m_arena));
}

ReflectionFrameSnapshot RendererReflectionSystem::snapshotFrameResources(
    const DeferredFrameTargets& targets,
    const ECSRenderDetail::MeshViewBufferSnapshot& view,
    const RayTracingSceneGraphResources& scene,
    const ReflectionSettings& settings,
    const u32 frameIndex,
    const ReflectionSceneContentStamp& stamp
)const noexcept{
    if(
        !m_resources.valid() || !view.bindingValid() || !ValidateReflectionSettings(settings)
        || targets.width != m_resources.parameters.width || targets.height != m_resources.parameters.height
        || !targets.bindless.slotsBufferDescriptor.valid() || !targets.bindless.gbufferSpecularRoughness.valid()
        || !targets.bindless.refractionSpecularRoughness.valid()
        || !targets.bindless.gbufferDepth.valid()
    )
        return {};
    ReflectionFrameSnapshot snapshot = m_resources;
    const b8 opticalTransport = !scene.opticalScene.boundsComplete || scene.opticalScene.transparentCount != 0u;
    if(scene.valid()){
        const b8 csg = static_cast<b8>(scene.csgTraceContextBuffer);
        if(!opticalTransport)
            snapshot.hardwarePipeline = csg ? m_csgPlainHardwarePipeline : m_plainHardwarePipeline;
        else if(scene.opticalScene.unspecifiedBoundariesOnly)
            snapshot.hardwarePipeline = csg ? m_csgUnspecifiedHardwarePipeline : m_unspecifiedHardwarePipeline;
        else if(csg)
            snapshot.hardwarePipeline = m_csgHardwarePipeline;
    }
    const ReflectionRadianceBinding base{ m_resources.opaqueRadiance, m_resources.parameters.opaqueRadianceSlot, m_resources.parameters.opaqueOutputSlot, };
    snapshot.postprocess = m_postprocess.snapshot(base, stamp, settings, m_graphics.getFrameIndex());
    if(!snapshot.postprocess.control || !snapshot.postprocess.current.texture)
        return {};
    snapshot.feedback = m_feedback.snapshot(stamp, settings, settings.screenFeedbackEnabled, m_graphics.getFrameIndex());
    if(!snapshot.feedback.valid())
        return {};
    snapshot.opaqueRadiance = snapshot.postprocess.current.texture;
    ReflectionFrameParameters& parameters = snapshot.parameters;
    const b8 hardwareRequested = settings.traceMode == ReflectionTraceMode::Hardware || settings.traceMode == ReflectionTraceMode::Hybrid;
    PopulateReflectionSnapshotFrameParameters(
        settings,
        scene.valid() && snapshot.hardwarePipeline != nullptr,
        targets.bindless.gbufferSpecularRoughness.slot(),
        targets.bindless.refractionSpecularRoughness.slot(),
        snapshot.postprocess.history.sampleIndex,
        snapshot.postprocess.current.storageSlot,
        snapshot.postprocess.current.sampledSlot,
        snapshot.postprocess.spatial.sampledSlot,
        snapshot.postprocess.spatialEnabled,
        targets.bindless.slotsBufferDescriptor.slot(),
        view.heapHandle.slot(),
        scene.materialContextSlotsHeapSlot,
        snapshot.feedback.plan.probeIndex,
        snapshot.feedback.previous.valid() ? snapshot.feedback.previous.descriptor.slot() : 0u,
        snapshot.feedback.current.valid() ? snapshot.feedback.current.descriptor.slot() : 0u,
        parameters
    );
    if(parameters.hardwareEnabled != 0u && parameters.maxHardwareRays > 0u && scene.csgTraceContextBuffer){
        const u32 argumentCount = ResolveReflectionIndirectArgumentCount(parameters.maxHardwareRays, true);
        if(!m_csgBuildArgsPipeline || snapshot.indirectArgs->getCreationDescription().byteSize < static_cast<u64>(argumentCount) * s_ReflectionIndirectArgumentBytes)
            return {};
        snapshot.buildArgsPipeline = m_csgBuildArgsPipeline;
    }
    snapshot.postprocess.deferredResourcesSlot = parameters.deferredResourcesSlot;
    snapshot.postprocess.opaqueSpecularSlot = parameters.opaqueSpecularSlot;
    snapshot.postprocess.viewSlot = parameters.viewSlot;
    snapshot.depthPyramid.sourceDepthSlot = targets.bindless.gbufferDepth.slot();
    if(parameters.hardwareEnabled != 0u)
        snapshot.scene = scene;
    if(settings.diagnosticsEnabled){
        snapshot.statistics = m_statistics.snapshot(MakeReflectionStatisticsMetadata(
            settings,
            parameters,
            frameIndex,
            m_graphics.getFrameIndex(),
            static_cast<b8>(scene.csgTraceContextBuffer),
            opticalTransport,
            hardwareRequested,
            scene.valid() && snapshot.hardwarePipeline
        ));
    }
    return snapshot;
}


void RendererReflectionSystem::releaseTargets(){
    m_postprocess.invalidateResources();
    m_feedback.invalidateResources();
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    for(Core::GpuDescriptorHandle& descriptor : m_descriptors)
        RendererDescriptorRegisterDetail::RetireDescriptor(heap, descriptor);
    RendererDescriptorRegisterDetail::RetireDescriptor(heap, m_depthSampledDescriptor);
    for(Core::GpuDescriptorHandle& descriptor : m_depthMipSampledDescriptors)
        RendererDescriptorRegisterDetail::RetireDescriptor(heap, descriptor);
    for(Core::GpuDescriptorHandle& descriptor : m_depthMipStorageDescriptors)
        RendererDescriptorRegisterDetail::RetireDescriptor(heap, descriptor);
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
    m_resources.feedback = {};
    m_resources.depthPyramid.texture = nullptr;
    m_resources.depthPyramid.mipCount = 0u;
    m_resources.depthPyramid.sampledSlot = 0u;
    m_resources.depthPyramid.sourceDepthSlot = 0u;
    for(ReflectionDepthPyramidMip& mip : m_resources.depthPyramid.mips)
        mip = {};
}

b8 RendererReflectionSystem::prepareCsgDispatchResources(){
    m_preparedCsgDispatch = nullptr;
    for(const ReflectionCsgDispatchHandle& control : m_csgDispatchStates){
        if(control->getReferenceCount() != 1u)
            continue;
        AtomicThreadFence(MemoryOrder::acquire);
        control->timing->discard();
        control->timing.emplace(m_graphics.gpuTiming());
        control->resources = {};
        m_preparedCsgDispatch = control.get();
        return true;
    }
    // Retained graph callbacks keep their owner immutable until they release it.
    auto* const control = NewArenaObject<ReflectionCsgDispatchControl>(m_arena, ReflectionFrameSnapshot{}, m_graphics.gpuTiming());
    if(!control)
        return false;
    ReflectionCsgDispatchHandle owner(control, ArenaRefDeleter<ReflectionCsgDispatchControl, Core::Alloc::GlobalArena>(&m_arena), s_AdoptRef);
    m_csgDispatchStates.push_back(Move(owner));
    m_preparedCsgDispatch = control;
    return true;
}

b8 RendererReflectionSystem::prepareQueue(const u32 capacity){
    using namespace __hidden_reflection_system;
    if(m_resources.queue && m_resources.parameters.queueCapacity >= capacity)
        return true;
    Core::BufferDesc desc;
    desc
        .setByteSize(static_cast<u64>(capacity) * sizeof(u32))
        .setCanHaveUAVs(true)
        .setCanHaveRawViews(true)
        .setDebugName(s_ReflectionRayQueueBufferName)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto queueResult = m_graphics.createBuffer(desc);
    if(!queueResult)
        return false;
    Core::BufferHandle queue = Move(*queueResult);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!RendererDescriptorRegisterDetail::ReplaceDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, queue.get()), m_descriptors[DescriptorIndex::Queue]))
        return false;
    m_resources.queue = Move(queue);
    m_resources.parameters.queueCapacity = capacity;
    m_resources.parameters.queueSlot = m_descriptors[DescriptorIndex::Queue].slot();
    return true;
}

b8 RendererReflectionSystem::prepareArguments(const u32 capacity, const b8 sliced){
    using namespace __hidden_reflection_system;
    const u32 argumentCount = ResolveReflectionIndirectArgumentCount(capacity, sliced);
    const u64 byteSize = static_cast<u64>(argumentCount) * s_ReflectionIndirectArgumentBytes;
    if(m_resources.indirectArgs && m_resources.indirectArgs->getCreationDescription().byteSize >= byteSize)
        return true;
    Core::BufferDesc desc;
    desc
        .setByteSize(byteSize)
        .setCanHaveUAVs(true)
        .setCanHaveRawViews(true)
        .setIsDrawIndirectArgs(true)
        .setDebugName(s_ReflectionIndirectArgsBufferName)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    auto argumentsResult = m_graphics.createBuffer(desc);
    if(!argumentsResult)
        return false;
    Core::BufferHandle arguments = Move(*argumentsResult);
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!RendererDescriptorRegisterDetail::ReplaceDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, arguments.get()), m_descriptors[DescriptorIndex::Args]))
        return false;
    m_resources.indirectArgs = Move(arguments);
    m_resources.parameters.argsSlot = m_descriptors[DescriptorIndex::Args].slot();
    return true;
}

b8 RendererReflectionSystem::preparePipelines(const b8 prepareHardware, const b8 prepareCsgHardware){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(u32), Core::ShaderType::Compute, m_bindingLayout)
        || !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES, Core::ShaderType::Compute, m_depthBindingLayout)
    )
        return false;
    const auto preparePipeline = [&](
        Core::ComputePipelineHandle& pipeline,
        Core::ShaderHandle& shader,
        const Core::Assets::AssetRef<ComputeShader> ReflectionSubsystem::* member,
        const Name debugName,
        const b8 hardware,
        const Core::BindingLayoutHandle& bindingLayout,
        const AStringView variant = Core::ShaderArchive::s_DefaultVariant
    ){
        if(pipeline)
            return true;
        if(!m_shaders.loadShader<ComputeShader>(member, variant, debugName, shader))
            return false;
        return RendererComputePipelineDetail::CreateHeapComputePipeline(device, shader, bindingLayout, hardware, pipeline);
    };
    if(
        !preparePipeline(m_resources.classifyPipeline, m_classifyShader, &ReflectionSubsystem::classifyCs, Name("ECSRender_ReflectionClassify"), false, m_bindingLayout)
        || !preparePipeline(m_resources.buildArgsPipeline, m_buildArgsShader, &ReflectionSubsystem::buildArgsCs, Name("ECSRender_ReflectionBuildArgs"), false, m_bindingLayout, s_ReflectionBuildArgsVariant)
        || !preparePipeline(m_resources.depthPyramid.pipeline, m_depthShader, &ReflectionSubsystem::depthReduceCs, Name("ECSRender_ReflectionDepthReduce"), false, m_depthBindingLayout)
    )
        return false;
    if(
        prepareHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout()
        && (!preparePipeline(m_resources.hardwarePipeline, m_hardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionOpticalHardware"), true, m_bindingLayout, s_ReflectionHwOpticalVariant) || !preparePipeline(
            m_plainHardwarePipeline, m_plainHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionHardware"), true, m_bindingLayout,
            s_ReflectionHwPlainVariant
        ) || !preparePipeline(m_unspecifiedHardwarePipeline, m_unspecifiedHardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionUnspecifiedOpticalHardware"), true, m_bindingLayout, s_ReflectionHwUnspecifiedVariant))
    )
        return false;
    const b8 csgHardware = prepareCsgHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout();
    if(csgHardware && !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES, Core::ShaderType::Compute, m_csgBindingLayout))
        return false;
    if(
        csgHardware
        && (!preparePipeline(m_csgBuildArgsPipeline, m_csgBuildArgsShader, &ReflectionSubsystem::buildArgsCs, Name("ECSRender_ReflectionCsgBuildArgs"), false, m_csgBindingLayout, s_ReflectionCsgBuildArgsVariant) || !preparePipeline(
            m_csgHardwarePipeline, m_csgHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgOpticalHardware"), true, m_csgBindingLayout,
            s_ReflectionHwCsgOpticalVariant
        ) || !preparePipeline(m_csgPlainHardwarePipeline, m_csgPlainHardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgHardware"), true, m_csgBindingLayout, s_ReflectionHwCsgPlainVariant) || !preparePipeline(
            m_csgUnspecifiedHardwarePipeline, m_csgUnspecifiedHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgUnspecifiedOpticalHardware"), true, m_csgBindingLayout,
            s_ReflectionHwCsgUnspecifiedVariant
        ))
    )
        return false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

