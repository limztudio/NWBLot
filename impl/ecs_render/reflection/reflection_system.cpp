// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_reflection.h"
#include "reflection_system.h"
#include <impl/ecs_render/kernel/descriptor_register_utils.h>
#include <impl/ecs_render/kernel/timing_names.h>
#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/frame_constants.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/reflection/types.adhd.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/backend_selection/backend.h>
#include <global/basic_string.h>
#include "feedback_resources.h"
#include <core/common/log.h>
#include "postprocess_resources.h"
#include <impl/assets_graphics/reflection_subsystem.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/assets/graphics/reflection/temporal_constants.h>
#include "reflection_pipelines.h"
#include "statistics_readback.h"
#include <impl/ecs_render/shared/statistics_readback_detail.h>
#include <core/alloc/general.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <core/task/gpu/capture/command_ir.h>
#include <global/algorithm.h>
#include <global/numeric_text.h>
#include "frame_graph_reflection_resolve.h"
#include <impl/assets/graphics/reflection/spatial_constants.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportReflectionTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    AStringView resourceLabel = "Reflection Output",
    b8 firstWrite = true
);
[[nodiscard]] Core::GpuTaskDesc ReflectionTaskDesc(Name identity, AStringView label, const Core::GpuTaskId& dependency)noexcept;

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionDepthPyramidTasks(
    const ReflectionDepthPyramidSnapshot& pyramid,
    Core::GpuGraphResourceId opaqueDepth,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::GpuGraphResourceId& outDepthPyramid
);

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionPostprocessTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    ReflectionGraphResult& result
);

[[nodiscard]] Expected<Core::GpuTaskId> DeclareReflectionStatisticsAndFinalizeTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    b8 feedbackReserved,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    ReflectionGraphResult& result
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    m_pipelines.invalidate(m_resources);
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
        && (!prepareHardware || (m_resources.hardwarePipeline && m_pipelines.plainHardwarePipeline() && m_pipelines.unspecifiedHardwarePipeline()))
        && (!csgHardwareWork || (m_pipelines.csgBuildArgsPipeline() && m_pipelines.csgHardwarePipeline() && m_pipelines.csgPlainHardwarePipeline() && m_pipelines.csgUnspecifiedHardwarePipeline()))
    )
        return
            prepareQueue(capacity) && prepareArguments(capacity, csgHardwareWork && m_pipelines.csgBuildArgsPipeline())
            && m_statistics.prepareResources()
            && m_postprocess.prepareResources(width, height, settings)
            && m_feedback.prepareResources(width, height, settings, settings.screenFeedbackEnabled)
        ;
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !m_pipelines.prepare(prepareHardware, csgHardwareWork, m_resources))
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
            prepareQueue(capacity) && prepareArguments(capacity, csgHardwareWork && m_pipelines.csgBuildArgsPipeline())
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
    const u32 argumentCount = ResolveReflectionIndirectArgumentCount(capacity, csgHardwareWork && m_pipelines.csgBuildArgsPipeline());
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
            snapshot.hardwarePipeline = csg ? m_pipelines.csgPlainHardwarePipeline() : m_pipelines.plainHardwarePipeline();
        else if(scene.opticalScene.unspecifiedBoundariesOnly)
            snapshot.hardwarePipeline = csg ? m_pipelines.csgUnspecifiedHardwarePipeline() : m_pipelines.unspecifiedHardwarePipeline();
        else if(csg)
            snapshot.hardwarePipeline = m_pipelines.csgHardwarePipeline();
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
        if(!m_pipelines.csgBuildArgsPipeline() || snapshot.indirectArgs->getCreationDescription().byteSize < static_cast<u64>(argumentCount) * s_ReflectionIndirectArgumentBytes)
            return {};
        snapshot.buildArgsPipeline = m_pipelines.csgBuildArgsPipeline();
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererReflectionFeedback::invalidateResources(){
    if(m_control)
        m_control->reset(0u);
    releaseBuffers();
    m_extent = {};
    m_deviceGeneration = 0u;
}

b8 RendererReflectionFeedback::prepareResources(const u32 width, const u32 height, const ReflectionSettings& settings, const b8 enabled){
    const Expected<ReflectionFeedbackExtent> extentResult = TryComputeReflectionFeedbackExtent(width, height);
    if(!extentResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: invalid extent or feedback address range ({}x{})"), width, height);
        return false;
    }
    const ReflectionFeedbackExtent& extent = *extentResult;
    auto& device = m_graphics.getDevice();
    const u16 deviceGeneration = device.getDeviceGeneration();
    if(!m_control){
        auto controlResult = CreateStatisticsControl<ReflectionFeedbackControl>(m_arena, deviceGeneration);
        if(!controlResult){
            NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to allocate acceptance control"));
            return false;
        }
        m_control = Move(*controlResult);
    }
    const b8 changed = width != m_extent.width || height != m_extent.height || deviceGeneration != m_deviceGeneration;
    const b8 active = enabled && settings.traceMode == ReflectionTraceMode::Hybrid && settings.maxHardwareRaysPerFrame > 0u;
    if(active && (changed || !m_banks[0].valid() || !m_banks[1].valid())){
        auto firstBank = prepareBank(extent.byteCount, s_ReflectionScreenFeedback0Name);
        if(!firstBank)
            return false;
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        Core::GpuDescriptorRetirementGuard retireFirst(heap, firstBank->descriptor);
        auto secondBank = prepareBank(extent.byteCount, s_ReflectionScreenFeedback1Name);
        if(!secondBank)
            return false;
        Core::GpuDescriptorRetirementGuard retireSecond(heap, secondBank->descriptor);
        releaseBuffers();
        m_banks[0] = Move(*firstBank);
        m_banks[1] = Move(*secondBank);
        retireFirst.release();
        retireSecond.release();
    }
    else if(changed)
        releaseBuffers();
    if(changed)
        m_control->reset(deviceGeneration);
    m_extent = extent;
    m_deviceGeneration = deviceGeneration;
    return true;
}

ReflectionFeedbackSnapshot RendererReflectionFeedback::snapshot(const ReflectionSceneContentStamp& stamp, const ReflectionSettings& settings, const b8 enabled, const u64 graphicsFrameIndex)const noexcept{
    ReflectionFeedbackSnapshot result;
    if(!m_control)
        return result;
    result.control = m_control;
    result.plan = m_control->plan(stamp, settings, enabled, graphicsFrameIndex);
    result.extent = m_extent;
    result.current = m_banks[result.plan.currentBank];
    result.previous = m_banks[result.plan.previousBank];
    return result;
}

void RendererReflectionFeedback::releaseBuffers(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    for(ReflectionFeedbackBinding& bank : m_banks){
        if(bank.descriptor.valid() && heap.isInitialized())
            heap.free(bank.descriptor);
        bank = {};
    }
}

Expected<ReflectionFeedbackBinding> RendererReflectionFeedback::prepareBank(const u32 byteCount, const Name name){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: descriptor heap is unavailable during preflight"));
        return MakeUnexpected(Failure{});
    }
    Core::BufferDesc desc;
    desc.setByteSize(byteCount).setCanHaveRawViews(true).setCanHaveUAVs(true).setDebugName(name).enableAutomaticStateTracking(Core::ResourceStates::Common);
    auto bufferResult = m_graphics.createBuffer(desc);
    if(!bufferResult){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to create {}-byte buffer"), byteCount);
        return MakeUnexpected(Failure{});
    }
    Core::BufferHandle buffer = Move(*bufferResult);
    const Expected<Core::GpuDescriptorHandle> descriptor = RendererDescriptorRegisterDetail::RegisterDescriptor(heap, Core::GpuDescriptorClass::StorageBuffer, Core::DescriptorWriteItem::RawBufferUav(0u, buffer.get()));
    if(!descriptor){
        NWB_LOGGER_ERROR(NWB_TEXT("Reflection feedback: failed to register storage buffer"));
        return MakeUnexpected(Failure{});
    }
    return ReflectionFeedbackBinding{Move(buffer), *descriptor};
}


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
    for(Core::GpuDescriptorHandle& descriptor : m_descriptors)
        RendererDescriptorRegisterDetail::RetireDescriptor(heap, descriptor);
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


b8 RendererReflectionPipelines::prepare(const b8 prepareHardware, const b8 prepareCsgHardware, ReflectionFrameSnapshot& resources){
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
        !preparePipeline(resources.classifyPipeline, m_classifyShader, &ReflectionSubsystem::classifyCs, Name(s_RenderNameECSRenderReflectionClassify), false, m_bindingLayout)
        || !preparePipeline(resources.buildArgsPipeline, m_buildArgsShader, &ReflectionSubsystem::buildArgsCs, Name(s_RenderNameECSRenderReflectionBuildArgs), false, m_bindingLayout, s_ReflectionBuildArgsVariant)
        || !preparePipeline(resources.depthPyramid.pipeline, m_depthShader, &ReflectionSubsystem::depthReduceCs, Name(s_RenderNameECSRenderReflectionDepthReduce), false, m_depthBindingLayout)
    )
        return false;
    if(
        prepareHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout()
        && (!preparePipeline(resources.hardwarePipeline, m_hardwareShader, &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionOpticalHardware), true, m_bindingLayout, s_ReflectionHwOpticalVariant) || !preparePipeline(
            m_plainHardwarePipeline, m_plainHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionHardware), true, m_bindingLayout,
            s_ReflectionHwPlainVariant
        ) || !preparePipeline(m_unspecifiedHardwarePipeline, m_unspecifiedHardwareShader, &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionUnspecifiedOpticalHardware), true, m_bindingLayout, s_ReflectionHwUnspecifiedVariant))
    )
        return false;
    const b8 csgHardware = prepareCsgHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout();
    if(csgHardware && !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES, Core::ShaderType::Compute, m_csgBindingLayout))
        return false;
    if(
        csgHardware
        && (!preparePipeline(m_csgBuildArgsPipeline, m_csgBuildArgsShader, &ReflectionSubsystem::buildArgsCs, Name(s_RenderNameECSRenderReflectionCsgBuildArgs), false, m_csgBindingLayout, s_ReflectionCsgBuildArgsVariant) || !preparePipeline(
            m_csgHardwarePipeline, m_csgHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionCsgOpticalHardware), true, m_csgBindingLayout,
            s_ReflectionHwCsgOpticalVariant
        ) || !preparePipeline(m_csgPlainHardwarePipeline, m_csgPlainHardwareShader, &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionCsgHardware), true, m_csgBindingLayout, s_ReflectionHwCsgPlainVariant) || !preparePipeline(
            m_csgUnspecifiedHardwarePipeline, m_csgUnspecifiedHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name(s_RenderNameECSRenderReflectionCsgUnspecifiedOpticalHardware), true, m_csgBindingLayout,
            s_ReflectionHwCsgUnspecifiedVariant
        ))
    )
        return false;
    return true;
}


void RendererReflectionPipelines::invalidate(ReflectionFrameSnapshot& resources){
    resources.classifyPipeline = nullptr;
    resources.buildArgsPipeline = nullptr;
    resources.hardwarePipeline = nullptr;
    resources.depthPyramid.pipeline = nullptr;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReflectionStatisticsState::accept(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const b8 hardwareReady, const ReflectionHistoryOutcome* history, const ReflectionFeedbackOutcome* feedback)noexcept{
    NothrowScopedLock lock(m_mutex);

    Slot* slot = beginAcceptWithinLock(key, token, hardwareReady);
    if(!slot)
        return;
    slot->metadata.opticalTransportEnabled = slot->metadata.opticalTransportEnabled && hardwareReady;
    if(history)
        ApplyReflectionStatisticsHistoryOutcome(*history, slot->metadata);
    if(feedback)
        ApplyReflectionStatisticsFeedbackOutcome(*feedback, slot->metadata);
}

void ReflectionStatisticsState::complete(const StatisticsReservationKey& key, const Core::QueueSubmissionToken& token, const Span<const u32> counters)noexcept{
    constexpr usize s_RequiredCounterCount = NWB_REFLECTION_COUNTER_SIZE / sizeof(u32);
    completeSlot<s_RequiredCounterCount>(
        key,
        token,
        counters,
        [](ReflectionStatistics& latest, const ReflectionStatistics& metadata, const Span<const u32> completedCounters)noexcept{
            CopyReflectionDiagnosticCounters(completedCounters, metadata, latest);
        });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 ReflectionStatisticsReadback::prepareResources(){
    if(m_buffers[0])
        return true;
    static constexpr Array<Name, s_StatisticsReadbackSlotCount> s_Names = { s_ReflectionStatisticsReadback0Name, s_ReflectionStatisticsReadback1Name, s_ReflectionStatisticsReadback2Name, };
    return ECSRenderDetail::PrepareStatisticsReadbackControlAndBuffers<NWB_REFLECTION_COUNTER_SIZE>(
        s_Names,
        NWB_TEXT("Reflection statistics: failed to allocate readback control"),
        m_arena,
        m_graphics,
        m_control,
        m_buffers
    );
}

void ReflectionStatisticsReadback::pollCompleted(){
    ECSRenderDetail::PollCompletedStatisticsReadback<NWB_REFLECTION_COUNTER_SIZE>(m_buffers, NWB_TEXT("Reflection statistics: failed to map a completed readback"), m_control, m_graphics);
}

ReflectionStatisticsReadbackSnapshot ReflectionStatisticsReadback::snapshot(const ReflectionStatistics& metadata)const noexcept{
    ReflectionStatisticsReadbackSnapshot result;
    if(!m_control || !m_buffers[0])
        return result;
    populateSnapshot(metadata, result);
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_reflection{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct UploadParametersTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };


public:
    struct Payload{
        Core::BufferHandle buffer;
        ReflectionFrameParameters parameters;
        ReflectionHistoryPlan history;
        ReflectionFeedbackPlan feedback;
        b8 feedbackReserved = false;
        const b8* hardwarePreparationReady = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        const b8 hardwareReady = payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionFrameParameters parameters = ResolveReflectionUploadFrameParameters(payload.parameters, payload.history, payload.feedback, payload.feedbackReserved, hardwareReady);
        // Stage the frame value after scene prep; a missing scene enqueues nothing.
        commandList.writeBuffer(*payload.buffer, &parameters, sizeof(parameters));
        return true;
    }
};

namespace DispatchStage = ReflectionDispatchStage;
static_assert(sizeof(ReflectionCsgDispatchPushConstants) == NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES);

struct DispatchTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionFrameSnapshot resources;
        DispatchStage::Enum stage;
        ReflectionFeedbackReservation feedbackReservation;
        const b8* hardwarePreparationReady = nullptr;
        b8* hardwareDispatchLogged = nullptr;
        b8* fallbackDispatchLogged = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        const ReflectionFrameSnapshot& resources = payload.resources;
        const b8 hardware = payload.stage == DispatchStage::Hardware;
        if(hardware && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady))
            return true;
        Core::ComputePipeline* pipeline = resources.buildArgsPipeline.get();
        if(hardware)
            pipeline = resources.hardwarePipeline.get();
        else if(payload.stage == DispatchStage::Classify)
            pipeline = resources.classifyPipeline.get();
        if(!pipeline || (hardware && !resources.scene.valid()))
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(pipeline);
        if(hardware)
            state.setIndirectParams(resources.indirectArgs.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline, hardware ? resources.scene.tlasHeapHandle : Core::GpuDescriptorHandle::Invalid());
        const b8 sliced = resources.scene.csgTraceContextBuffer && (hardware || payload.stage == DispatchStage::BuildArgs);
        const ReflectionCsgDispatchPushConstants csgParameters = MakeReflectionCsgDispatchPushConstants(resources.frameParametersSlot, 0u);
        if(sliced)
            commandList.setPushConstants(&csgParameters, sizeof(csgParameters));
        else
            commandList.setPushConstants(&resources.frameParametersSlot, sizeof(resources.frameParametersSlot));

        NotNull<const Core::GpuTimingScopeDefinition*> scope(&ReflectionGpuTimingScope::s_BuildArgs);
        if(hardware)
            scope = &ReflectionGpuTimingScope::s_Hardware;
        else if(payload.stage == DispatchStage::Classify)
            scope = &ReflectionGpuTimingScope::s_Classify;
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), *scope, payload.graphics.getDevice(), commandList);

        if(hardware)
            commandList.dispatchIndirect(0u);
        else if(payload.stage == DispatchStage::BuildArgs)
            commandList.dispatch(1u, 1u, 1u);
        else
            commandList.dispatch(DivideUpWrapping(resources.parameters.width, NWB_REFLECTION_CLASSIFY_GROUP_SIZE), DivideUpWrapping(resources.parameters.height, NWB_REFLECTION_CLASSIFY_GROUP_SIZE), 1u);
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        const b8 hardware = payload.resources.parameters.hardwareEnabled != 0u && payload.resources.parameters.maxHardwareRays > 0u && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const b8 sceneReady = payload.resources.parameters.hardwareEnabled != 0u && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        payload.feedbackReservation.accept(token, sceneReady);
        if(payload.stage == DispatchStage::Hardware && hardware && payload.resources.scene.csgTraceContextBuffer)
            payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
        if(payload.stage == DispatchStage::Hardware && hardware){
            if(payload.hardwareDispatchLogged)
                LogHardwareReflectionResolveIfNeeded(*payload.hardwareDispatchLogged);
        }
        else if(payload.stage == DispatchStage::Classify && !hardware){
            if(payload.fallbackDispatchLogged)
                LogFallbackReflectionResolveIfNeeded(payload.resources.parameters.traceMode, *payload.fallbackDispatchLogged);
        }
    }

    static void Discarded(Payload& payload)noexcept{
        payload.feedbackReservation.discard();
    }
};

struct CsgHardwareSliceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { .requiredCapabilities = Core::GpuQueueCapability::Compute, .requiresPrimaryGraphicsQueue = true, };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        RendererRayTracingSystem& raytracingSystem;
        ReflectionCsgDispatchHandle dispatch;
        u32 slice = 0u;
        u32 sliceCount = 0u;
        const b8* hardwarePreparationReady = nullptr;
        b8* hardwareDispatchLogged = nullptr;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            return true;
        if(!payload.dispatch || payload.slice >= payload.sliceCount)
            return false;
        const ReflectionFrameSnapshot& resources = payload.dispatch->resources;
        Core::ComputePipeline* const pipeline = resources.hardwarePipeline.get();
        if(!pipeline || !resources.scene.valid())
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(pipeline).setIndirectParams(resources.indirectArgs.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *pipeline, resources.scene.tlasHeapHandle);
        const ReflectionCsgDispatchPushConstants parameters = MakeReflectionCsgDispatchPushConstants(resources.frameParametersSlot, payload.slice * s_ReflectionTraceSliceRays);
        commandList.setPushConstants(&parameters, sizeof(parameters));
        if(payload.slice == 0u && !payload.dispatch->timing->begin(ReflectionGpuTimingScope::s_Hardware, payload.graphics.getDevice(), commandList))
            return false;

        commandList.dispatchIndirect(payload.slice * s_ReflectionIndirectArgumentBytes);
        if(payload.slice + 1u == payload.sliceCount)
            return payload.dispatch->timing->recordEnd(commandList);
        return true;
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)
            return;
        // An accepted prefix already reads the frozen context even when a later packet fails.
        payload.raytracingSystem.confirmRayTraceContextReadSubmission(token);
        if(payload.slice == 0u && !payload.dispatch->timing->confirmBeginSubmission(token)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing begin"));
            payload.dispatch->timing->discard();
        }
        if(payload.slice + 1u == payload.sliceCount && !payload.dispatch->timing->confirmEndSubmission(token, true)){
            NWB_LOGGER_WARNING(NWB_TEXT("Reflection: failed to confirm accepted hardware timing end"));
            payload.dispatch->timing->discard();
        }
        if(payload.hardwareDispatchLogged)
            LogHardwareReflectionResolveIfNeeded(*payload.hardwareDispatchLogged);
    }

    static void Discarded(Payload& payload)noexcept{
        if(payload.dispatch)
            payload.dispatch->timing->discard();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Name, ErrorCode> ReflectionCsgHardwareSliceIdentity(const u32 slice){
    Array<char, s_ReflectionSliceIndexTextBufferSize> suffix{};
    const auto index = FormatI64(static_cast<i64>(slice), suffix);
    if(!index)
        return MakeUnexpected(index.error());
    const auto identity = DeriveName(s_ReflectionCsgHardwareSliceFamily, *index);
    if(!identity)
        return MakeUnexpected(MakeErrorCode(ErrorCondition::invalid_argument));
    return *identity;
}

Expected<ReflectionGraphResult> DeclareReflectionTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererReflectionSystem& reflectionSystem,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_task_graph_reflection;
    if(!resources.valid() || inputs.surfaceReads.empty())
        return MakeUnexpected(Failure{});
    const b8 screen = resources.parameters.traceMode == NWB_REFLECTION_MODE_SCREEN || resources.parameters.traceMode == NWB_REFLECTION_MODE_HYBRID;
    if(screen && (!inputs.opaqueDepth.valid() || !inputs.opaqueColor.valid()))
        return MakeUnexpected(Failure{});
    ReflectionFeedbackReservation feedbackReservation(resources.feedback.control, resources.feedback.plan);
    const b8 feedbackReserved = feedbackReservation.valid();
    const b8 feedbackWrites = feedbackReserved && resources.feedback.plan.eligible && resources.parameters.hardwareEnabled != 0u;
    const b8 buildArguments = resources.hasHardwareWork();
    const DispatchStage::Enum feedbackPublicationStage = buildArguments ? DispatchStage::BuildArgs : DispatchStage::Classify;
    // A stale feedback lease disables this optimization; rendering remains available.
    Core::GpuGraphResourceId feedbackCurrent;
    Core::GpuGraphResourceId feedbackPrevious;
    if(feedbackWrites){
        const auto feedbackCurrentResult = FindOrImportBuffer(graph, resources.feedback.current.buffer, s_ReflectionBufferLabel);
        Expected<Core::GpuGraphResourceId> feedbackPreviousResult = MakeUnexpected(Failure{});
        if(resources.feedback.plan.reused)
            feedbackPreviousResult = FindOrImportBuffer(graph, resources.feedback.previous.buffer, s_ReflectionBufferLabel);
        if(!feedbackCurrentResult || (resources.feedback.plan.reused && !feedbackPreviousResult))
            return MakeUnexpected(Failure{});
        feedbackCurrent = *feedbackCurrentResult;
        if(resources.feedback.plan.reused)
            feedbackPrevious = *feedbackPreviousResult;
    }
    ReflectionGraphResult result;
    const auto opaqueRadianceResult = ImportReflectionTexture(graph, resources.opaqueRadiance);
    const auto glassRadianceResult = ImportReflectionTexture(graph, resources.glassRadiance);
    const auto frameParametersResult = FindOrImportBuffer(graph, resources.frameParameters, s_ReflectionBufferLabel);
    const auto countersResult = FindOrImportBuffer(graph, resources.counters, s_ReflectionBufferLabel);
    const auto queueResult = FindOrImportBuffer(graph, resources.queue, s_ReflectionBufferLabel);
    const auto argsResult = FindOrImportBuffer(graph, resources.indirectArgs, s_ReflectionBufferLabel);
    if(!opaqueRadianceResult || !glassRadianceResult || !frameParametersResult || !countersResult || !queueResult || !argsResult)
        return MakeUnexpected(Failure{});
    result.opaqueRadiance = *opaqueRadianceResult;
    result.glassRadiance = *glassRadianceResult;
    result.frameParameters = *frameParametersResult;
    result.counters = *countersResult;
    const Core::GpuGraphResourceId queue = *queueResult;
    const Core::GpuGraphResourceId args = *argsResult;

    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionParametersUploadTaskName, s_ReflectionParametersUploadTaskLabel, dependency);
    const Core::GpuTaskResourceUse parameterWrite = WriteUse(result.frameParameters, Core::ResourceStates::CopyDest);
    desc.setResourceUses({ &parameterWrite, 1u });
    const auto dependencyResult = graph.addTask<UploadParametersTask>(
        desc,
        UploadParametersTask::Payload{ resources.frameParameters, resources.parameters, resources.postprocess.history, resources.feedback.plan, feedbackReserved, inputs.hardwarePreparationReady, });
    if(!dependencyResult)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult;

    desc = ReflectionTaskDesc(s_ReflectionClearCountersTaskName, s_ReflectionClearCountersTaskLabel, dependency);
    const auto dependencyResult2 = graph.addClearBufferTask(desc, Core::GpuClearBufferTaskDesc{.destination = result.counters, .clearValue = 0u});
    if(!dependencyResult2)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult2;

    Core::GpuGraphResourceId depthPyramid;
    if(screen){
        const auto depthPyramidTaskResult = DeclareReflectionDepthPyramidTasks(resources.depthPyramid, inputs.opaqueDepth, dependency, graph, graphics, depthPyramid);
        if(!depthPyramidTaskResult)
            return MakeUnexpected(Failure{});
        dependency = *depthPyramidTaskResult;
    }

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReads.size() + inputs.hardwareReads.size() + s_ReflectionUseCapacity);
    const auto appendSurfaceReads = [&]()noexcept{
        uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
    };
    const auto appendDispatch = [&](const Name identity, const AStringView label, const DispatchStage::Enum stage){
        Core::GpuTaskDesc dispatchDesc = ReflectionTaskDesc(identity, label, dependency);
        dispatchDesc.setResourceUses(uses);
        dispatchDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        if(stage == DispatchStage::Hardware)
            dispatchDesc.setResourceSetUses(inputs.hardwareSetReads);
        ReflectionFeedbackReservation dispatchFeedback(nullptr, {});
        if(stage == feedbackPublicationStage)
            dispatchFeedback = Move(feedbackReservation);
        const auto dispatchTask = graph.addTask<DispatchTask>(
            dispatchDesc,
            DispatchTask::Payload{ graphics, inputs.raytracingSystem, resources, stage, Move(dispatchFeedback), inputs.hardwarePreparationReady, inputs.hardwareDispatchLogged, inputs.fallbackDispatchLogged, });
        if(!dispatchTask){
            dependency = {};
            return false;
        }
        dependency = *dispatchTask;
        return true;
    };
    appendSurfaceReads();
    if(screen){
        uses.push_back(ReadTextureUse(depthPyramid, Core::TextureSubresourceSet(0u, resources.depthPyramid.mipCount, 0u, 1u)));
        uses.push_back(ReadUse(inputs.opaqueColor));
    }
    if(feedbackWrites){
        Core::GpuTaskResourceUse entryWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
        entryWrite.range.bufferRange = Core::BufferRange(s_ReflectionFeedbackHeaderBytes, resources.feedback.extent.byteCount - s_ReflectionFeedbackHeaderBytes);
        uses.push_back(entryWrite);
        if(feedbackPrevious.valid())
            uses.push_back(ReadUse(feedbackPrevious));
    }
    uses.push_back(WriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
    uses.push_back(WriteUse(queue, Core::ResourceStates::UnorderedAccess));
    uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
    if(!appendDispatch(s_ReflectionClassifyTaskName, s_ReflectionClassifyTaskLabel, DispatchStage::Classify))
        return MakeUnexpected(Failure{});

    if(buildArguments){
        uses.clear();
        uses.push_back(ReadUse(result.frameParameters, Core::ResourceStates::ConstantBuffer));
        uses.push_back(ReadUse(result.counters));
        uses.push_back(WriteUse(args, Core::ResourceStates::UnorderedAccess));
        if(feedbackWrites){
            Core::GpuTaskResourceUse headerWrite = WriteUse(feedbackCurrent, Core::ResourceStates::UnorderedAccess);
            headerWrite.range.bufferRange = Core::BufferRange(0u, s_ReflectionFeedbackHeaderBytes);
            uses.push_back(headerWrite);
        }
        if(!appendDispatch(s_ReflectionBuildArgsTaskName, s_ReflectionBuildArgsTaskLabel, DispatchStage::BuildArgs))
            return MakeUnexpected(Failure{});
        if(!resources.hardwarePipeline || !resources.scene.valid() || inputs.hardwareReads.empty())
            return MakeUnexpected(Failure{});
        appendSurfaceReads();
        for(const Core::GpuTaskResourceUse& use : inputs.hardwareReads)
            uses.push_back(use);
        uses.push_back(ReadUse(queue));
        uses.push_back(ReadUse(args, Core::ResourceStates::IndirectArgument));
        uses.push_back(ReadWriteUse(result.counters, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        uses.push_back(ReadWriteUse(result.glassRadiance, Core::ResourceStates::UnorderedAccess));
        if(resources.scene.csgTraceContextBuffer){
            const auto dispatchResult = reflectionSystem.acquireCsgDispatchState(resources);
            if(!dispatchResult)
                return MakeUnexpected(dispatchResult.error());
            const ReflectionCsgDispatchHandle& dispatch = *dispatchResult;
            if(!dispatch)
                return MakeUnexpected(Failure{});
            const u32 rayCapacity = Min(resources.parameters.maxHardwareRays, resources.parameters.queueCapacity);
            const u32 sliceCount = ResolveReflectionIndirectArgumentCount(rayCapacity, true);
            // Separate native packets let the driver retire bounded work; shared UAV state also orders CPU recording.
            for(u32 slice = 0u; slice < sliceCount; ++slice){
                const auto sliceIdentity = ReflectionCsgHardwareSliceIdentity(slice);
                if(!sliceIdentity){
                    NWB_LOGGER_ERROR(NWB_TEXT("RendererReflectionSystem: failed to format CSG hardware slice identity: {}"), ConvertString(sliceIdentity.error().message()));
                    return MakeUnexpected(Failure{});
                }
                Core::GpuTaskDesc sliceDesc = ReflectionTaskDesc(*sliceIdentity, s_ReflectionHardwareResolveTaskLabel, dependency);
                Core::GpuTaskSchedulingHint scheduling;
                scheduling.forceSubmissionBoundary = true;
                scheduling.allowPacketMerge = false;
                sliceDesc.setScheduling(scheduling).setResourceUses(uses);
                sliceDesc.setResourceSetUses(inputs.hardwareSetReads);
                sliceDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
                const auto dependencyResult4 = graph.addTask<CsgHardwareSliceTask>(sliceDesc, CsgHardwareSliceTask::Payload{ graphics, inputs.raytracingSystem, dispatch, slice, sliceCount, inputs.hardwarePreparationReady, inputs.hardwareDispatchLogged, });
                if(!dependencyResult4)
                    return MakeUnexpected(Failure{});
                dependency = *dependencyResult4;
            }
        }
        else if(!appendDispatch(s_ReflectionHardwareTaskName, s_ReflectionHardwareResolveTaskLabel, DispatchStage::Hardware))
            return MakeUnexpected(Failure{});
    }

    const auto dependencyResult5 = DeclareReflectionPostprocessTasks(resources, inputs, dependency, graph, graphics, scratchArena, result);
    if(!dependencyResult5)
        return MakeUnexpected(Failure{});
    dependency = *dependencyResult5;

    const auto completionResult = DeclareReflectionStatisticsAndFinalizeTasks(resources, inputs, feedbackReserved, dependency, graph, result);
    if(!completionResult)
        return MakeUnexpected(completionResult.error());
    result.completion = *completionResult;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<FrameGraphReflectionResolveResult> FrameGraphReflectionResolve::declare(const FrameGraphReflectionResolveInputs& inputs, Core::Alloc::ScratchArena& scratchArena, RayTracingSceneGraphReads& sceneReads){
    FrameGraphReflectionResolveResult result{};
    using namespace RendererTaskGraphDetail;
    DeferredFrameTargets& deferredTargets = inputs.targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewBufferSnapshot = inputs.meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources = inputs.sceneResources;
    const ReflectionSettings& reflectionSettings = inputs.reflectionSettings;
    const CsgFrameState& csgFrameState = inputs.csgFrameState;
    const b8 useLaggedLightingHistory = inputs.useLaggedLightingHistory;
    const Core::GpuGraphResourceId specularRoughness = inputs.specularRoughness;
    const Core::GpuGraphResourceId refractionSpecularRoughness = inputs.refractionSpecularRoughness;
    const Core::GpuGraphResourceId normal = inputs.normal;
    const Core::GpuGraphResourceId depth = inputs.depth;
    const Core::GpuGraphResourceId worldPosition = inputs.worldPosition;
    const Core::GpuGraphResourceId refractionDepth = inputs.refractionDepth;
    const Core::GpuGraphResourceId refractionNormalIor = inputs.refractionNormalIor;
    const Core::GpuGraphResourceId opaqueColor = inputs.opaqueColor;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;
    const Core::GpuGraphResourceId sceneShading = inputs.sceneShading;
    const Core::GpuGraphResourceId lights = inputs.lights;
    const Core::GpuGraphResourceSetId hardwareTraceGeometrySet = inputs.hardwareTraceGeometrySet;
    const Core::GpuGraphResourceSetId traceMaterialSampledTextureSet = inputs.traceMaterialSampledTextureSet;
    ReflectionSceneContentStamp reflectionContentStamp = inputs.contentStamp;
    reflectionContentStamp.geometry = sceneResources.contentStamp.geometry;
    reflectionContentStamp.material = sceneResources.contentStamp.material;
    // CSG evaluation and lagged screen lighting do not have a matching content stamp for this first history policy.
    reflectionContentStamp.trusted = sceneResources.contentStamp.trusted && csgFrameState.empty() && !useLaggedLightingHistory;
    const ReflectionFrameSnapshot reflectionResources = m_reflectionSystem.snapshotFrameResources(
        deferredTargets,
        meshViewBufferSnapshot,
        inputs.reflectionSceneAvailable ? sceneResources : RayTracingSceneGraphResources{},
        reflectionSettings,
        inputs.reflectionFrameIndex,
        reflectionContentStamp
    );
    if(!reflectionResources.valid())
        return MakeUnexpected(Failure{});
    if(!sceneReads.valid() && reflectionResources.hasHardwareWork()){
        auto sceneReadsResult = ImportRayTracingSceneGraphReads(sceneResources, m_raytracingSystem.sceneTlasBackingInitialState(), inputs.traceContextUploadTask, m_graph, scratchArena);
        if(!sceneReadsResult){
            sceneReads = RayTracingSceneGraphReads(scratchArena);
            return MakeUnexpected(Failure{});
        }
        sceneReads = Move(*sceneReadsResult);
    }
    const Array<Core::GpuTaskResourceUse, 9> reflectionSurfaceReads = {
        ReadUse(specularRoughness),
        ReadUse(refractionSpecularRoughness),
        ReadUse(normal),
        ReadUse(depth),
        ReadUse(worldPosition),
        ReadUse(refractionDepth),
        ReadUse(refractionNormalIor),
        ReadUse(meshView, Core::ResourceStates::ConstantBuffer),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
    };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> reflectionHardwareReads{scratchArena};
    // Scene shading and lights follow the scene reads.
    reflectionHardwareReads.reserve(sceneReads.uses.size() + sceneReads.csgUses.size() + s_ReflectionExtraHardwareReadCapacity);
    if(sceneReads.valid()){
        for(const Core::GpuTaskResourceUse& read : sceneReads.uses)
            reflectionHardwareReads.push_back(read);
        for(const Core::GpuTaskResourceUse& read : sceneReads.csgUses)
            reflectionHardwareReads.push_back(read);
        reflectionHardwareReads.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
        reflectionHardwareReads.push_back(ReadUse(lights));
    }
    const RendererFramePipelineDetail::TraceResourceSetUses reflectionSets = RendererFramePipelineDetail::MakeTraceResourceSetUses(hardwareTraceGeometrySet, traceMaterialSampledTextureSet);
    const ReflectionGraphInputs reflectionInputs{
        .raytracingSystem = m_raytracingSystem,
        .opaqueDepth = depth,
        .opaqueColor = opaqueColor,
        .surfaceReads = reflectionSurfaceReads,
        .hardwareReads = reflectionHardwareReads,
        .hardwareSetReads = Span<const Core::GpuTaskResourceSetUse>(reflectionSets.uses.data(), reflectionSets.useCount),
        .hardwarePreparationReady = &inputs.hardwarePreparationReady,
        .hardwareDispatchLogged = &inputs.hardwareDispatchLogged,
        .fallbackDispatchLogged = &inputs.fallbackDispatchLogged,
    };
    auto reflectionGraphResult = DeclareReflectionTasks(reflectionResources, reflectionInputs, inputs.lightingTask, m_graph, m_graphics, m_reflectionSystem, scratchArena);
    if(!reflectionGraphResult)
        return MakeUnexpected(Failure{});
    result.reflectionGraph = Move(*reflectionGraphResult);
    result.reflectionCompositeInputs = ReflectionCompositeInputs{
        .opaqueRadianceSlot = reflectionResources.parameters.opaqueRadianceSlot,
        .glassRadianceSlot = reflectionResources.parameters.glassRadianceSlot,
        .debugView = reflectionSettings.debugView,
    };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_postprocess{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(sizeof(ReflectionTemporalPushConstants) == NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES);
static_assert(sizeof(ReflectionSpatialPushConstants) == NWB_REFLECTION_SPATIAL_PUSH_CONSTANT_BYTES);
static_assert(sizeof(ReflectionDepthReducePushConstants) == NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES);

struct DepthReduceTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        ReflectionDepthReducePushConstants parameters;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.pipeline)
            return false;
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_DepthPyramid, payload.graphics.getDevice(), commandList);

        commandList.dispatch(DivideUpWrapping(payload.parameters.destinationWidth, NWB_REFLECTION_DEPTH_GROUP_SIZE), DivideUpWrapping(payload.parameters.destinationHeight, NWB_REFLECTION_DEPTH_GROUP_SIZE), 1u);
        return true;
    }
};

struct TemporalTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        ReflectionPostprocessSnapshot snapshot;
        ReflectionHistoryReservation reservation;
        u32 width = 0u;
        u32 height = 0u;
        const b8* hardwarePreparationReady = nullptr;
        b8 hardwareEnabled = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.snapshot.history.eligible)
            return true;
        const b8 ready = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome outcome = ResolveReflectionHistoryOutcome(payload.snapshot.history, ready);
        // Classification and tracing already produced this bank; the accepted task still publishes its history state.
        if(!outcome.reused || payload.snapshot.history.settings.temporalMaxSamples <= 1u)
            return true;
        commandList.endRenderPass();
        const ReflectionTemporalPushConstants parameters = MakeReflectionTemporalPushConstants(
            payload.width,
            payload.height,
            payload.snapshot.current.storageSlot,
            payload.snapshot.previous.sampledSlot,
            payload.snapshot.opaqueSpecularSlot,
            outcome.reused,
            payload.snapshot.history.previousSampleCount,
            payload.snapshot.history.settings.temporalMaxSamples,
            outcome.sampleIndex,
            payload.snapshot.history.settings.samplingSeed
        );
        Core::ComputeState state;
        state.setPipeline(payload.snapshot.temporalPipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.snapshot.temporalPipeline.get());
        commandList.setPushConstants(&parameters, sizeof(parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Temporal, payload.graphics.getDevice(), commandList);

        commandList.dispatch(DivideUpWrapping(payload.width, NWB_REFLECTION_TEMPORAL_GROUP_SIZE), DivideUpWrapping(payload.height, NWB_REFLECTION_TEMPORAL_GROUP_SIZE), 1u);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        payload.reservation.accept(token, payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};

struct SpatialTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };


public:
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::ComputePipelineHandle pipeline;
        ReflectionSpatialPushConstants parameters;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        Core::ComputeState state;
        state.setPipeline(payload.pipeline.get());
        commandList.setComputeState(state);
        payload.graphics.getDevice().getDescriptorHeap().bindCompute(commandList, *payload.pipeline.get());
        commandList.setPushConstants(&payload.parameters, sizeof(payload.parameters));
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), ReflectionGpuTimingScope::s_Spatial, payload.graphics.getDevice(), commandList);

        commandList.dispatch(DivideUpWrapping(payload.parameters.width, NWB_REFLECTION_SPATIAL_GROUP_SIZE), DivideUpWrapping(payload.parameters.height, NWB_REFLECTION_SPATIAL_GROUP_SIZE), 1u);
        return !commandList.commandRecordingFailed();
    }
};

struct StatisticsReadbackTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };


public:
    struct Payload{
        Core::BufferHandle source;
        Core::BufferHandle destination;
        Core::GpuGraphResourceId sourceResource;
        Core::GpuGraphResourceId destinationResource;
        ReflectionStatisticsReservation reservation;
        ReflectionHistoryPlan history;
        ReflectionFeedbackPlan feedback;
        const b8* hardwarePreparationReady = nullptr;
        b8 feedbackReserved = false;
        b8 hardwareEnabled = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.source || !payload.destination || !payload.reservation.valid())
            return false;
        if(context.commandIrCapture && !context.commandIrCapture->captureCopyBuffer(context.task, context.packet, context.queue, payload.sourceResource, 0u, payload.destinationResource, 0u, NWB_REFLECTION_COUNTER_SIZE))
            return false;
        commandList.endRenderPass();
        commandList.copyBuffer(*payload.destination, 0u, *payload.source, 0u, NWB_REFLECTION_COUNTER_SIZE);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        const b8 hardwareReady = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        const ReflectionHistoryOutcome history = ResolveReflectionHistoryOutcome(payload.history, hardwareReady);
        ReflectionFeedbackOutcome feedback = ResolveReflectionFeedbackOutcome(payload.feedback, hardwareReady);
        if(!payload.feedbackReserved){
            feedback.eligible = false;
            feedback.reused = false;
        }
        payload.reservation.acceptReservation(token, hardwareReady, &history, &feedback);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuGraphResourceId> ImportReflectionTexture(
    Core::GpuTaskGraph& graph,
    const Core::TextureHandle& texture,
    const AStringView resourceLabel,
    const b8 firstWrite
){
    // Producers overwrite everything; Unknown preserves fresh Undefined.
    return FindOrImportTexture(graph, texture, resourceLabel, firstWrite ? Core::ResourceStates::Unknown : Core::ResourceStates::Common);
}

Core::GpuTaskDesc ReflectionTaskDesc(const Name identity, const AStringView label, const Core::GpuTaskId& dependency)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies({ &dependency, 1u });
    return desc;
}

Expected<Core::GpuTaskId> DeclareReflectionDepthPyramidTasks(
    const ReflectionDepthPyramidSnapshot& pyramid,
    const Core::GpuGraphResourceId opaqueDepth,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::GpuGraphResourceId& outDepthPyramid
){
    using namespace __hidden_task_graph_postprocess;
    const auto depthPyramidResult = ImportReflectionTexture(graph, pyramid.texture);
    if(!depthPyramidResult){
        outDepthPyramid = {};
        return MakeUnexpected(Failure{});
    }
    outDepthPyramid = *depthPyramidResult;
    for(u32 mipIndex = 0u; mipIndex < pyramid.mipCount; ++mipIndex){
        const ReflectionDepthPyramidMip& destination = pyramid.mips[mipIndex];
        const ReflectionDepthPyramidMip& source = pyramid.mips[mipIndex == 0u ? 0u : mipIndex - 1u];
        const ReflectionDepthReducePushConstants parameters = MakeReflectionDepthReducePushConstants(
            mipIndex == 0u ? pyramid.sourceDepthSlot : source.sampledSlot,
            destination.storageSlot,
            source.width,
            source.height,
            destination.width,
            destination.height
        );
        const Array<Core::GpuTaskResourceUse, 2u> depthUses = {
            ReadTextureUse(mipIndex == 0u ? opaqueDepth : outDepthPyramid, Core::TextureSubresourceSet(mipIndex == 0u ? 0u : mipIndex - 1u, 1u, 0u, 1u)),
            WriteTextureUse(outDepthPyramid, Core::TextureSubresourceSet(mipIndex, 1u, 0u, 1u), Core::ResourceStates::UnorderedAccess),
        };
        Core::GpuTaskDesc depthDesc = ReflectionTaskDesc(destination.taskIdentity, s_ReflectionDepthReduceTaskLabel, dependency);
        depthDesc.setResourceUses(depthUses);
        depthDesc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
        const auto depthTask = graph.addTask<DepthReduceTask>(depthDesc, DepthReduceTask::Payload{graphics, pyramid.pipeline, parameters});
        if(!depthTask)
            return MakeUnexpected(Failure{});
        dependency = *depthTask;
    }
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionPostprocessTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    Core::Alloc::ScratchArena& scratchArena,
    ReflectionGraphResult& result
){
    using namespace __hidden_task_graph_postprocess;
    const ReflectionPostprocessSnapshot& snapshot = resources.postprocess;
    ReflectionHistoryReservation reservation(snapshot.control, snapshot.history);
    if(!reservation.valid() || (snapshot.history.eligible && !snapshot.temporalPipeline))
        return MakeUnexpected(Failure{});
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
    uses.reserve(inputs.surfaceReads.size() + s_ReflectionTemporalExtraResourceUseCount);
    if(snapshot.history.eligible){
        uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
        uses.push_back(ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess));
        if(snapshot.history.reused){
            const auto previousResult = ImportReflectionTexture(graph, snapshot.previous.texture, s_ReflectionHistoryImageLabel, false);
            if(!previousResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId previous = *previousResult;
            uses.push_back(ReadUse(previous));
        }
    }
    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionTemporalTaskName, s_ReflectionTemporalTaskLabel, dependency);
    desc.setResourceUses(uses);
    if(snapshot.history.eligible)
        desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const auto temporalTask = graph.addTask<TemporalTask>(
        desc,
        TemporalTask::Payload{ graphics, snapshot, Move(reservation), resources.parameters.width, resources.parameters.height, inputs.hardwarePreparationReady, resources.parameters.hardwareEnabled != 0u, });
    if(!temporalTask)
        return MakeUnexpected(temporalTask.error());
    dependency = *temporalTask;
    if(!snapshot.spatialEnabled)
        return dependency;
    if(!snapshot.spatialPipeline || !snapshot.spatial.texture)
        return MakeUnexpected(Failure{});
    const auto spatialResult = ImportReflectionTexture(graph, snapshot.spatial.texture, s_ReflectionHistoryImageLabel, true);
    if(!spatialResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId spatial = *spatialResult;
    uses.assign(inputs.surfaceReads.begin(), inputs.surfaceReads.end());
    uses.push_back(ReadUse(result.opaqueRadiance));
    uses.push_back(WriteUse(spatial, Core::ResourceStates::UnorderedAccess));
    desc = ReflectionTaskDesc(s_ReflectionSpatialTaskName, s_ReflectionSpatialTaskLabel, dependency);
    desc.setResourceUses(uses);
    desc.setTiming(Core::GpuTaskTimingMetadata{.policy = Core::GpuTaskTimingPolicy::PacketOnly});
    const ReflectionSpatialPushConstants spatialParameters = MakeReflectionSpatialPushConstants(
        resources.parameters.width,
        resources.parameters.height,
        snapshot.current.sampledSlot,
        snapshot.spatial.storageSlot,
        snapshot.deferredResourcesSlot,
        snapshot.opaqueSpecularSlot,
        snapshot.viewSlot,
        snapshot.history.settings.spatialRadius
    );
    const auto spatialTask = graph.addTask<SpatialTask>(
        desc,
        SpatialTask::Payload{ graphics, snapshot.spatialPipeline, spatialParameters });
    if(!spatialTask)
        return MakeUnexpected(spatialTask.error());
    dependency = *spatialTask;
    result.opaqueRadiance = spatial;
    return dependency;
}

Expected<Core::GpuTaskId> DeclareReflectionStatisticsAndFinalizeTasks(
    const ReflectionFrameSnapshot& resources,
    const ReflectionGraphInputs& inputs,
    const b8 feedbackReserved,
    Core::GpuTaskId dependency,
    Core::GpuTaskGraph& graph,
    ReflectionGraphResult& result
){
    using namespace __hidden_task_graph_postprocess;
    if(resources.parameters.diagnosticsEnabled != 0u && resources.statistics.control){
        ReflectionStatistics metadata = resources.statistics.metadata;
        metadata.feedbackSequence = feedbackReserved ? resources.feedback.plan.sequence : 0u;
        ReflectionStatisticsReservation reservation(resources.statistics.control, metadata);
        if(reservation.valid()){
            const Core::BufferHandle& readback = resources.statistics.buffers[reservation.slotIndex()];
            if(!readback)
                return MakeUnexpected(Failure{});
            const auto readbackResourceResult = FindOrImportBuffer(graph, readback, s_ReflectionBufferLabel);
            if(!readbackResourceResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId readbackResource = *readbackResourceResult;
            const Array<Core::GpuTaskResourceUse, 2u> copyUses = {
                Core::GpuTaskResourceUse{
                    .resource = result.counters,
                    .range = Core::GpuTaskResourceRange{.bufferRange = Core::BufferRange(0u, NWB_REFLECTION_COUNTER_SIZE)},
                    .requiredState = Core::ResourceStates::CopySource,
                    .access = Core::GpuTaskResourceAccess::Read,
                },
                Core::GpuTaskResourceUse{
                    .resource = readbackResource,
                    .range = Core::GpuTaskResourceRange{.bufferRange = Core::BufferRange(0u, NWB_REFLECTION_COUNTER_SIZE)},
                    .requiredState = Core::ResourceStates::CopyDest,
                    .access = Core::GpuTaskResourceAccess::Write,
                },
            };
            Core::GpuTaskDesc copyDesc = ReflectionTaskDesc(s_ReflectionStatisticsTaskName, s_ReflectionStatisticsTaskLabel, dependency);
            copyDesc.setResourceUses(copyUses);
            const auto copyTask = graph.addTask<StatisticsReadbackTask>(
                copyDesc,
                StatisticsReadbackTask::Payload{
                    resources.counters, readback, result.counters, readbackResource, Move(reservation), resources.postprocess.history,
                    resources.feedback.plan, inputs.hardwarePreparationReady, feedbackReserved, resources.parameters.hardwareEnabled != 0u,
                });
            if(!copyTask)
                return MakeUnexpected(Failure{});
            dependency = *copyTask;
        }
    }

    const Array<Core::GpuTaskResourceUse, 2u> finalUses = {ReadUse(result.opaqueRadiance), ReadUse(result.glassRadiance)};
    Core::GpuTaskDesc desc = ReflectionTaskDesc(s_ReflectionFinalizeTaskName, s_ReflectionFinalizeTaskLabel, dependency);
    desc.setResourceUses(finalUses);
    return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
