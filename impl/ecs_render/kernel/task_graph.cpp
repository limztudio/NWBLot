// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/gpu_timing.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Work = ECSRenderDetail::FrameExecutionWork;

struct WorkMetadata{
    Name identity = NAME_NONE;
    AStringView markerLabel;
    Core::GpuQueueRequest queue;
    Core::GpuTaskSchedulingHint scheduling;

    WorkMetadata() = default;
    WorkMetadata(const Name& value, const AStringView label, const Core::GpuQueueRequest& request)
        : identity(value)
        , markerLabel(label)
        , queue(request)
    {}
};


[[nodiscard]] static Core::GpuQueueRequest GraphicsQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Graphics,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] static Core::GpuQueueRequest ComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        true,
    };
}

[[nodiscard]] static Core::GpuQueueRequest TransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}


// This is the first late-native-recording task.  It is intentionally self-contained so the compiler may route it
// through the existing Graphics or Compute transport today and a distinct Transfer queue in a later phase.
struct LaggedLightingHistoryCopyTask{
    struct Payload{
        Core::TextureHandle sourceShadowVisibility;
        Core::TextureHandle sourceCausticIrradiance;
        Core::TextureHandle sourceSurfelIrradiance;
        Core::TextureHandle destinationShadowVisibility;
        Core::TextureHandle destinationCausticIrradiance;
        Core::TextureHandle destinationSurfelIrradiance;
        Core::QueueSubmissionToken* acceptedHistoryToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.sourceShadowVisibility
            || !payload.sourceCausticIrradiance
            || !payload.sourceSurfelIrradiance
            || !payload.destinationShadowVisibility
            || !payload.destinationCausticIrradiance
            || !payload.destinationSurfelIrradiance
        )
            return false;

        commandList.setTextureState(
            payload.sourceShadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::CopySource
        );
        commandList.setTextureState(
            payload.destinationShadowVisibility.get(),
            ECSRenderDetail::s_ShadowVisibilitySubresources,
            Core::ResourceStates::CopyDest
        );
        commandList.setTextureState(
            payload.sourceCausticIrradiance.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopySource
        );
        commandList.setTextureState(
            payload.destinationCausticIrradiance.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        );
        commandList.setTextureState(
            payload.sourceSurfelIrradiance.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopySource
        );
        commandList.setTextureState(
            payload.destinationSurfelIrradiance.get(),
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        );
        commandList.commitBarriers();

        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::TextureSlice shadowSlice;
            shadowSlice.setArraySlice(shadowSlot);
            commandList.copyTexture(
                payload.destinationShadowVisibility.get(),
                shadowSlice,
                payload.sourceShadowVisibility.get(),
                shadowSlice
            );
        }
        const Core::TextureSlice irradianceSlice;
        commandList.copyTexture(
            payload.destinationCausticIrradiance.get(),
            irradianceSlice,
            payload.sourceCausticIrradiance.get(),
            irradianceSlice
        );
        commandList.copyTexture(
            payload.destinationSurfelIrradiance.get(),
            irradianceSlice,
            payload.sourceSurfelIrradiance.get(),
            irradianceSlice
        );
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.acceptedHistoryToken)
            *payload.acceptedHistoryToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedHistoryToken)
            *payload.acceptedHistoryToken = {};
    }
};


// AVBOIT remains explicitly staged because its raster/compute alternation is a real dependency chain.  The graph
// owns those packets now; manual state handoffs only seed native recording until automatic graph barriers arrive.
static void RestoreAvboitGbufferInputs(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setTextureState(
        targets.albedo.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.normal.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.worldPosition.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
}


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool clearTargets = false;
        bool hasTransparentRenderers = false;
        bool splitStages = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.clearTargets)
            payload.avboitSystem->clearAvboitTargets(commandList, payload.targets->avboit);
        if(payload.hasTransparentRenderers){
            if(payload.splitStages)
                payload.avboitSystem->renderAvboitPreDepthWarpPasses(
                    commandList,
                    *payload.targets,
                    *payload.csgFrameState
                );
            else
                payload.avboitSystem->renderAvboitPasses(commandList, *payload.targets, *payload.csgFrameState);
        }
        RestoreAvboitGbufferInputs(commandList, *payload.targets);
        return true;
    }
};


struct AvboitDepthWarpGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitDepthWarp(commandList, *payload.targets);
        return true;
    }
};


struct AvboitExtinctionGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->renderAvboitExtinctionPass(commandList, *payload.targets, *payload.csgFrameState);
        return true;
    }
};


struct AvboitIntegrationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitIntegration(commandList, *payload.targets);
        return true;
    }
};


struct AvboitAccumulationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->renderAvboitAccumulatePass(commandList, *payload.targets, *payload.csgFrameState);
        return true;
    }
};


struct DeferredPresentGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::Framebuffer* presentationFramebuffer = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncFinalTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool shadowVisibilityRunsOnCompute = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.deferredSystem
            || !payload.graphics
            || !payload.targets
            || !payload.presentationFramebuffer
            || !payload.frameTimingTransaction
            || !payload.timingTicket
            || (payload.shadowVisibilityRunsOnCompute && !payload.asyncFinalTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.shadowVisibilityRunsOnCompute){
            payload.asyncFinalTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncFinal,
                payload.graphics->getDevice(),
                commandList
            );
            payload.asyncFinalTiming->value().finishMarker();
        }

        const bool presentRecorded = payload.deferredSystem->renderDeferredPresent(
            commandList,
            *payload.targets,
            payload.presentationFramebuffer
        );
        const bool frameTimingEnded = presentRecorded
            && payload.frameTimingTransaction->recordEnd(commandList)
        ;
        if(payload.shadowVisibilityRunsOnCompute && presentRecorded && payload.asyncFinalTiming->has_value()){
            payload.asyncFinalTiming->value().finishTiming(commandList);
            payload.asyncFinalTiming->reset();
        }
        return presentRecorded && frameTimingEnded;
    }
};


[[nodiscard]] static WorkMetadata WorkMetadataFor(const Work::Enum work){
    switch(work){
    case Work::GraphicsPrefix:
        return WorkMetadata{ Name("render.task_graph.graphics_prefix"), "Graphics Prefix", GraphicsQueueRequest() };
    default:
        NWB_ASSERT(false);
        return {};
    }
}

[[nodiscard]] static Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Texture)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Buffer)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::HazardDomain)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse WriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Write,
    };
}

[[nodiscard]] static Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::AccelStruct)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadWriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::buildGpuTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    const DeferredFrameTargets& deferredTargets,
    const ECSRenderDetail::FrameExecutionPlan& frameExecutionPlan
){
    using namespace __hidden_renderer_task_graph;

    // The history-copy task is independent of the observational sidecar below. A parity mismatch in remaining
    // legacy work must not restore a retired graph-owned task or its submission path.
    buildLaggedLightingHistoryTaskGraph(input, deferredTargets);

    m_gpuTaskGraphValid = false;
    m_gpuTaskGraphWorkTasks.clear();
    m_gpuTaskGraphLegacyQueueMismatches.clear();
    m_gpuTaskGraph.reset();
    m_gpuTaskGraphAnalysis.reset();
    m_gpuTaskGraphQueueAssignments.reset();

    m_gpuTaskGraphWorkTasks.reserve(Work::kCount);
    for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
        m_gpuTaskGraphWorkTasks.push_back(Core::GpuTaskId{});
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    // The existing renderer reports dedicated AsyncCompute only for a distinct compute transport. Recheck the
    // physical family identity here so a future backend cannot accidentally make the graph compiler invent overlap.
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const ECSRenderDetail::GpuTaskGraphFrameSchedule graphSchedule(input);

    if(
        dedicatedAsyncCompute != input.dedicatedAsyncCompute
        || dedicatedAsyncCompute != graphSchedule.usesDedicatedAsyncCompute()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue topology disagrees with the legacy frame plan"));
        return;
    }
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_gpuTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(deferredTargets.albedo, Name("render.task_graph.albedo"), "Albedo");
    const Core::GpuGraphResourceId normal = importTexture(deferredTargets.normal, Name("render.task_graph.normal"), "Normal");
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.task_graph.world_position"),
        "World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(deferredTargets.depth, Name("render.task_graph.depth"), "Depth");
    const Core::GpuGraphResourceId meshViewBuffer = m_gpuTaskGraph.importBuffer(
        m_drawState.m_meshViewBuffer,
        BufferResourceDesc(Name("render.task_graph.mesh_view"), "Mesh View")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !meshViewBuffer.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not import required renderer resources"));
        return;
    }

    Core::GraphicsVector<Core::GpuTaskId>& workTasks = m_gpuTaskGraphWorkTasks;
    const auto addWorkTask = [&](const Work::Enum work, const Core::GpuTaskResourceUse* const resourceUses, const usize resourceUseCount){
        if(!graphSchedule.hasWork(work))
            return true;

        Core::GpuTaskId dependencies[Work::kCount] = {};
        usize dependencyCount = 0u;
        for(usize producerWorkIndex = 0u; producerWorkIndex < Work::kCount; ++producerWorkIndex){
            const Work::Enum producerWork = static_cast<Work::Enum>(producerWorkIndex);
            if(!graphSchedule.workDependsOn(work, producerWork))
                continue;

            const Core::GpuTaskId dependency = workTasks[producerWorkIndex];
            if(!dependency.valid() || dependencyCount >= LengthOf(dependencies))
                return false;
            dependencies[dependencyCount++] = dependency;
        }
        const WorkMetadata metadata = WorkMetadataFor(work);
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(metadata.identity)
            .setMarkerLabel(metadata.markerLabel)
            .setQueue(metadata.queue)
            .setScheduling(metadata.scheduling)
            .setDependencies(dependencies, dependencyCount)
            .setResourceUses(resourceUses, resourceUseCount)
        ;
        workTasks[static_cast<usize>(work)] = m_gpuTaskGraph.addTask(desc);
        return workTasks[static_cast<usize>(work)].valid();
    };

    const Core::GpuTaskResourceUse graphicsPrefixUses[] = {
        WriteUse(meshViewBuffer, Core::ResourceStates::UnorderedAccess),
        WriteUse(albedo, Core::ResourceStates::RenderTarget),
        WriteUse(normal, Core::ResourceStates::RenderTarget),
        WriteUse(worldPosition, Core::ResourceStates::RenderTarget),
        WriteUse(depth, Core::ResourceStates::DepthWrite),
    };
    const bool graphBuilt =
        addWorkTask(Work::GraphicsPrefix, graphicsPrefixUses, LengthOf(graphicsPrefixUses))
    ;
    if(!graphBuilt){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not declare semantic work dependencies"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.analyze(m_gpuTaskGraph, m_gpuTaskGraphAnalysis, scratchArena)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph validation failed (status {})")
            , static_cast<u32>(m_gpuTaskGraphAnalysis.diagnostic().status)
        );
        return;
    }

    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queueTopologyInfos[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology queueTopology{
        .queues = queueTopologyInfos,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queueTopologyInfos) : 1u,
    };
    if(!compiler.assignQueues(m_gpuTaskGraph, m_gpuTaskGraphAnalysis, queueTopology, m_gpuTaskGraphQueueAssignments)){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue assignment failed (status {})")
            , static_cast<u32>(m_gpuTaskGraphQueueAssignments.diagnostic().status)
        );
        return;
    }
    bool workSetMatchesLegacyPlan = true;
    usize workSetMismatchCount = 0u;
    for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
        const Work::Enum work = static_cast<Work::Enum>(workIndex);
        if(graphSchedule.hasWork(work) == frameExecutionPlan.hasWork(work))
            continue;
        workSetMatchesLegacyPlan = false;
        ++workSetMismatchCount;
    }
    if(!workSetMatchesLegacyPlan){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph work set differs from FrameExecutionPlan ({})")
            , workSetMismatchCount
        );
    }

    for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
        const Work::Enum work = static_cast<Work::Enum>(workIndex);
        if(!graphSchedule.hasWork(work))
            continue;

        if(!frameExecutionPlan.hasWork(work))
            continue;

        const Core::GpuTaskQueueAssignment* const assignment = m_gpuTaskGraphQueueAssignments.find(workTasks[workIndex]);
        if(!assignment){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue assignment is missing declared work"));
            m_gpuTaskGraphLegacyQueueMismatches.push_back(workTasks[workIndex]);
            continue;
        }
        const Core::CommandQueue::Enum legacyQueue = device.resolveRenderLane(frameExecutionPlan.laneForWork(work));
        if(
            legacyQueue != frameExecutionPlan.expectedQueueForWork(work)
            || !frameExecutionPlan.workMatchesExpectedQueue(work, assignment->queueClass)
        ){
            m_gpuTaskGraphLegacyQueueMismatches.push_back(workTasks[workIndex]);
        }
    }
    if(!m_gpuTaskGraphLegacyQueueMismatches.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue assignment differs from FrameExecutionPlan ({})")
            , m_gpuTaskGraphLegacyQueueMismatches.size()
        );
    }

    m_gpuTaskGraphValid = true;
}


void RendererSystem::buildLaggedLightingHistoryTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    const DeferredFrameTargets& deferredTargets
){
    using namespace __hidden_renderer_task_graph;

    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    if(!schedule.capturesLaggedLightingHistory())
        return;

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    if(!dedicatedAsyncCompute)
        return;

    const DeferredLaggedLightingHistoryResources& history = deferredTargets.laggedLightingHistory;
    if(!history.valid())
        return;
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_laggedLightingHistoryTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        deferredTargets.shadowVisibility,
        Name("render.lagged_history_copy.shadow_visibility"),
        "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.lagged_history_copy.caustic_irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        deferredTargets.surfelIrradiance,
        Name("render.lagged_history_copy.surfel_irradiance"),
        "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId historyShadowVisibility = importTexture(
        history.shadowVisibility,
        Name("render.lagged_history_copy.history_shadow_visibility"),
        "History Shadow Visibility"
    );
    const Core::GpuGraphResourceId historyCausticIrradiance = importTexture(
        history.causticIrradiance,
        Name("render.lagged_history_copy.history_caustic_irradiance"),
        "History Caustic Irradiance"
    );
    const Core::GpuGraphResourceId historySurfelIrradiance = importTexture(
        history.surfelIrradiance,
        Name("render.lagged_history_copy.history_surfel_irradiance"),
        "History Surfel Irradiance"
    );
    if(
        !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !historyShadowVisibility.valid()
        || !historyCausticIrradiance.valid()
        || !historySurfelIrradiance.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history-copy resources"));
        return;
    }

    Core::GpuExternalCompletionDesc presentationCompletionDesc;
    presentationCompletionDesc
        .setIdentity(Name("render.lagged_history_copy.presentation_complete"))
        .setMarkerLabel("Final Presentation Complete")
    ;
    m_laggedLightingPresentationCompletion = m_laggedLightingHistoryTaskGraph.importExternalCompletion(
        presentationCompletionDesc
    );
    if(!m_laggedLightingPresentationCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import final presentation completion for history copy"));
        return;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(shadowVisibility, Core::ResourceStates::CopySource),
        ReadUse(causticIrradiance, Core::ResourceStates::CopySource),
        ReadUse(surfelIrradiance, Core::ResourceStates::CopySource),
        WriteUse(historyShadowVisibility, Core::ResourceStates::CopyDest),
        WriteUse(historyCausticIrradiance, Core::ResourceStates::CopyDest),
        WriteUse(historySurfelIrradiance, Core::ResourceStates::CopyDest),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.lagged_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(TransferQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_laggedLightingPresentationCompletion, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_laggedLightingHistoryTask = m_laggedLightingHistoryTaskGraph.addTask<LaggedLightingHistoryCopyTask>(
        desc,
        LaggedLightingHistoryCopyTask::Payload{
            .sourceShadowVisibility = deferredTargets.shadowVisibility,
            .sourceCausticIrradiance = deferredTargets.causticIrradiance,
            .sourceSurfelIrradiance = deferredTargets.surfelIrradiance,
            .destinationShadowVisibility = history.shadowVisibility,
            .destinationCausticIrradiance = history.causticIrradiance,
            .destinationSurfelIrradiance = history.surfelIrradiance,
            .acceptedHistoryToken = &m_laggedLightingHistorySubmissionToken,
        }
    );
    if(!m_laggedLightingHistoryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare lagged-lighting history-copy task"));
        return;
    }

    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_laggedLightingHistoryTaskGraph,
        m_laggedLightingHistoryTaskGraphAnalysis,
        topology,
        m_laggedLightingHistoryTaskGraphQueueAssignments,
        m_laggedLightingHistoryCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile lagged-lighting history-copy graph"));
        return;
    }
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = true;
}


void RendererSystem::buildShadowVisibilityTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_shadowVisibilityTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_shadowVisibilityTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.shadow_visibility.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.shadow_visibility.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.shadow_visibility.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        deferredTargets.shadowVisibility,
        Name("render.shadow_visibility.output"),
        "Shadow Visibility"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.shadow_visibility.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_shadowVisibilityTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-visibility graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.shadow_visibility.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_shadowVisibilityPrefixCompletion = m_shadowVisibilityTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_shadowVisibilityPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for shadow visibility"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(40u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Hybrid transparent shadows multiply onto the opaque result, so this remains a read/write declaration even
    // when the hardware-only path overwrites it.
    resourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        appendOptionalReadWriteTexture(
            deferredTargets.shadowCoarseTransmittance,
            Name("render.shadow_visibility.coarse_transmittance"),
            "Shadow Coarse Transmittance"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B"
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.shadow_visibility.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.shadow_visibility.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.shadow_visibility.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.shadow_visibility.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.shadow_visibility.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.shadow_visibility.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsReadback,
            Name("render.shadow_visibility.edge_stats_readback"),
            "Shadow Edge Statistics Readback",
            Core::ResourceStates::CopyDest
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeListBuffer,
            Name("render.shadow_visibility.edge_list"),
            "Shadow Edge List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowIndirectArgsBuffer,
            Name("render.shadow_visibility.indirect_args"),
            "Shadow Indirect Arguments",
            Core::ResourceStates::UnorderedAccess
        )
    ;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_shadowVisibilityTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.shadow_visibility.tlas"), "Scene TLAS")
        );
        optionalResourcesImported = optionalResourcesImported && tlas.valid();
        if(tlas.valid())
            resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a shadow-visibility dynamic resource"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_shadowVisibilityPrefixCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_shadowVisibilityTask = m_raytracingSystem.declareShadowVisibilityTask(
        m_shadowVisibilityTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        timingTicket
    );
    if(!m_shadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow-visibility graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_shadowVisibilityTaskGraph,
        m_shadowVisibilityTaskGraphAnalysis,
        topology,
        m_shadowVisibilityTaskGraphQueueAssignments,
        m_shadowVisibilityCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile shadow-visibility task graph"));
        return;
    }
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilityTaskGraphValid = true;
}


void RendererSystem::buildHardwareCausticsTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const bool waitsForLaggedLightingHistory,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);

    if(!input.hardwareCaustics || !deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_hardwareCausticsTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_hardwareCausticsTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.hardware_caustics.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.hardware_caustics.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId causticAccumulator = importTexture(
        deferredTargets.causticAccumulator,
        Name("render.hardware_caustics.accumulator"),
        "Caustic Accumulator"
    );
    const Core::GpuGraphResourceId causticHistory = importTexture(
        deferredTargets.causticHistory,
        Name("render.hardware_caustics.history"),
        "Caustic History"
    );
    const Core::GpuGraphResourceId causticResolveHalf = importTexture(
        deferredTargets.causticResolveHalf,
        Name("render.hardware_caustics.resolve_half"),
        "Caustic Resolve Half"
    );
    const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
        deferredTargets.causticResolveGeometry,
        Name("render.hardware_caustics.resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.hardware_caustics.irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.hardware_caustics.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_hardwareCausticsTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.hardware_caustics.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !depth.valid()
        || !causticAccumulator.valid()
        || !causticHistory.valid()
        || !causticResolveHalf.valid()
        || !causticResolveGeometry.valid()
        || !causticIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.hardware_caustics.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_hardwareCausticsPrefixCompletion = m_hardwareCausticsTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_hardwareCausticsPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for hardware caustics"));
        return;
    }
    if(waitsForLaggedLightingHistory){
        Core::GpuExternalCompletionDesc laggedHistoryCompletionDesc;
        laggedHistoryCompletionDesc
            .setIdentity(Name("render.hardware_caustics.lagged_history_complete"))
            .setMarkerLabel("Lagged Lighting History Complete")
        ;
        m_hardwareCausticsLaggedHistoryCompletion = m_hardwareCausticsTaskGraph.importExternalCompletion(
            laggedHistoryCompletionDesc
        );
        if(!m_hardwareCausticsLaggedHistoryCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-history completion for hardware caustics"));
            return;
        }
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    resourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.hardware_caustics.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.hardware_caustics.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_drawState.m_meshViewBuffer,
            Name("render.hardware_caustics.mesh_view"),
            "Mesh View",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.hardware_caustics.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.hardware_caustics.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.hardware_caustics.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.hardware_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.hardware_caustics.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
    ;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_hardwareCausticsTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.hardware_caustics.tlas"), "Scene TLAS")
        );
        optionalResourcesImported = optionalResourcesImported && tlas.valid();
        if(tlas.valid())
            resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
        return;
    }

    Core::GpuExternalCompletionId externalDependencies[2] = {
        m_hardwareCausticsPrefixCompletion,
        m_hardwareCausticsLaggedHistoryCompletion,
    };
    const usize externalDependencyCount = waitsForLaggedLightingHistory ? LengthOf(externalDependencies) : 1u;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, externalDependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_hardwareCausticsTask = m_raytracingSystem.declareHardwareCausticsTask(
        m_hardwareCausticsTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        timingTicket
    );
    if(!m_hardwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_hardwareCausticsTaskGraph,
        m_hardwareCausticsTaskGraphAnalysis,
        topology,
        m_hardwareCausticsTaskGraphQueueAssignments,
        m_hardwareCausticsCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile hardware-caustics task graph"));
        return;
    }
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsTaskGraphValid = true;
}


void RendererSystem::buildSoftwareCausticsTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_softwareCausticsTaskGraphValid = false;
    m_softwareCausticsTask = {};
    m_softwareCausticsShadowVisibilityCompletion = {};
    m_softwareCausticsTaskGraph.reset();
    m_softwareCausticsTaskGraphAnalysis.reset();
    m_softwareCausticsTaskGraphQueueAssignments.reset();
    m_softwareCausticsCompiledGraph.reset();
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);

    // The software producer owns the complete non-hardware path on both a dedicated Compute queue and its
    // compiler-selected Graphics fallback.
    if(input.hardwareCaustics || !deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_softwareCausticsTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_softwareCausticsTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.software_caustics.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.software_caustics.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId causticAccumulator = importTexture(
        deferredTargets.causticAccumulator,
        Name("render.software_caustics.accumulator"),
        "Caustic Accumulator"
    );
    const Core::GpuGraphResourceId causticHistory = importTexture(
        deferredTargets.causticHistory,
        Name("render.software_caustics.history"),
        "Caustic History"
    );
    const Core::GpuGraphResourceId causticResolveHalf = importTexture(
        deferredTargets.causticResolveHalf,
        Name("render.software_caustics.resolve_half"),
        "Caustic Resolve Half"
    );
    const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
        deferredTargets.causticResolveGeometry,
        Name("render.software_caustics.resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.software_caustics.irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.software_caustics.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_softwareCausticsTaskGraph.importHazardDomain(
        HazardDomainDesc(
            Name("render.software_caustics.scene_geometry"),
            "Software BVH Scene Geometry"
        )
    );
    if(
        !worldPosition.valid()
        || !depth.valid()
        || !causticAccumulator.valid()
        || !causticHistory.valid()
        || !causticResolveHalf.valid()
        || !causticResolveGeometry.valid()
        || !causticIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software-caustics graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc shadowVisibilityCompletionDesc;
    shadowVisibilityCompletionDesc
        .setIdentity(Name("render.software_caustics.shadow_visibility_complete"))
        .setMarkerLabel("Shadow Visibility Complete")
    ;
    m_softwareCausticsShadowVisibilityCompletion = m_softwareCausticsTaskGraph.importExternalCompletion(
        shadowVisibilityCompletionDesc
    );
    if(!m_softwareCausticsShadowVisibilityCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-visibility completion for software caustics"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.software_caustics.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.software_caustics.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.software_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_drawState.m_meshViewBuffer,
            Name("render.software_caustics.mesh_view"),
            "Mesh View",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.software_caustics.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a software-caustics dynamic resource"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_softwareCausticsShadowVisibilityCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_softwareCausticsTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_softwareCausticsTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        timingTicket
    );
    if(!m_softwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software-caustics graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_softwareCausticsTaskGraph,
        m_softwareCausticsTaskGraphAnalysis,
        topology,
        m_softwareCausticsTaskGraphQueueAssignments,
        m_softwareCausticsCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile software-caustics task graph"));
        return;
    }
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsTaskGraphValid = true;
}


void RendererSystem::buildSurfelGiTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_surfelGiTaskGraphValid = false;
    m_surfelGiTask = {};
    m_surfelGiEffectsCompletion = {};
    m_surfelGiTaskGraph.reset();
    m_surfelGiTaskGraphAnalysis.reset();
    m_surfelGiTaskGraphQueueAssignments.reset();
    m_surfelGiCompiledGraph.reset();
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_surfelGiTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_surfelGiTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.surfel_gi.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.surfel_gi.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId surfelIrradianceHalf = importTexture(
        deferredTargets.surfelIrradianceHalf,
        Name("render.surfel_gi.irradiance_half"),
        "Surfel Irradiance Half"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        deferredTargets.surfelIrradiance,
        Name("render.surfel_gi.irradiance"),
        "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.surfel_gi.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_surfelGiTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !normal.valid()
        || !surfelIrradianceHalf.valid()
        || !surfelIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel-GI graph resources"));
        return;
    }

    const bool waitsForSoftwareCaustics = !input.hardwareCaustics;
    Core::GpuExternalCompletionDesc effectsCompletionDesc;
    effectsCompletionDesc
        .setIdentity(
            waitsForSoftwareCaustics
                ? Name("render.surfel_gi.software_caustics_complete")
                : Name("render.surfel_gi.shadow_visibility_complete")
        )
        .setMarkerLabel(waitsForSoftwareCaustics ? "Software Caustics Complete" : "Shadow Visibility Complete")
    ;
    m_surfelGiEffectsCompletion = m_surfelGiTaskGraph.importExternalCompletion(effectsCompletionDesc);
    if(!m_surfelGiEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import effects completion for surfel GI"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.surfel_gi.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.surfel_gi.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.surfel_gi.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolBuffer,
            Name("render.surfel_gi.pool"),
            "Surfel Pool",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadBuffer,
            Name("render.surfel_gi.cell_heads"),
            "Surfel Cell Heads",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterBuffer,
            Name("render.surfel_gi.counter"),
            "Surfel Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
            Name("render.surfel_gi.trace_args"),
            "Surfel Trace Arguments",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelFreeListBuffer,
            Name("render.surfel_gi.free_list"),
            "Surfel Free List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolSnapshotBuffer,
            Name("render.surfel_gi.pool_snapshot"),
            "Surfel Pool Snapshot",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
            Name("render.surfel_gi.cell_head_snapshot"),
            "Surfel Cell Head Snapshot",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterReadback,
            Name("render.surfel_gi.counter_readback"),
            "Surfel Counter Readback",
            Core::ResourceStates::CopyDest
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a surfel-GI dynamic resource domain"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_surfelGiEffectsCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_surfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_surfelGiTaskGraph,
        desc,
        deferredTargets,
        timingTicket
    );
    if(!m_surfelGiTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-GI graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_surfelGiTaskGraph,
        m_surfelGiTaskGraphAnalysis,
        topology,
        m_surfelGiTaskGraphQueueAssignments,
        m_surfelGiCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile surfel-GI task graph"));
        return;
    }
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);
    m_surfelGiTaskGraphValid = true;
}


void RendererSystem::buildAvboitTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool clearTargets,
    const bool hasTransparentRenderers,
    Core::GpuTimingSubmissionTicket& preTimingTicket,
    Core::GpuTimingSubmissionTicket& depthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& extinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& integrationTimingTicket,
    Core::GpuTimingSubmissionTicket& accumulationTimingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    const bool splitStages = schedule.usesAsyncAvboit() && dedicatedAsyncCompute;
    if(!splitStages){
        depthWarpTimingTicket.discard();
        extinctionTimingTicket.discard();
        integrationTimingTicket.discard();
        accumulationTimingTicket.discard();
    }
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || hasTransparentRenderers != input.hasTransparentRenderers
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_avboitTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_avboitTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.avboit.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.avboit.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.avboit.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.avboit.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId lowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId accumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId accumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId transmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    const Core::GpuGraphResourceId coverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    const Core::GpuGraphResourceId depthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Core::GpuGraphResourceId control = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    const Core::GpuGraphResourceId extinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId extinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.avboit.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId materialDomain = m_avboitTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    const Core::GpuGraphResourceId csgDomain = m_avboitTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !lowRaster.valid()
        || !accumColor.valid()
        || !accumExtinction.valid()
        || !transmittance.valid()
        || !coverage.valid()
        || !depthWarp.valid()
        || !control.valid()
        || !extinction.valid()
        || !extinctionOverflow.valid()
        || !bindlessSlots.valid()
        || !materialDomain.valid()
        || !csgDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import AVBOIT graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.avboit.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_avboitPrefixCompletion = m_avboitTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_avboitPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for AVBOIT"));
        return;
    }

    const Core::GpuTaskResourceUse preResourceUses[] = {
        ReadUse(albedo),
        ReadUse(normal),
        ReadUse(worldPosition),
        ReadUse(depth),
        ReadWriteUse(lowRaster, Core::ResourceStates::RenderTarget),
        ReadWriteUse(accumColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(accumExtinction, Core::ResourceStates::RenderTarget),
        ReadWriteUse(transmittance, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(coverage, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(depthWarp, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(control, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(extinction, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(extinctionOverflow, Core::ResourceStates::UnorderedAccess),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        ReadUse(materialDomain),
        // Transparent CSG interval construction mutates its backing domain before occupancy consumes it.
        ReadWriteUse(csgDomain, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Core::GpuTaskCostHint::Large;
    graphicsScheduling.forceSubmissionBoundary = true;
    graphicsScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("render.avboit.pre"))
        .setMarkerLabel(splitStages ? "AVBOIT Pre" : "AVBOIT")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(graphicsScheduling)
        .setExternalDependencies(&m_avboitPrefixCompletion, 1u)
        .setResourceUses(preResourceUses, LengthOf(preResourceUses))
    ;
    m_avboitPreTask = m_avboitTaskGraph.addTask<AvboitPreGraphTask>(
        preDesc,
        AvboitPreGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .timingTicket = &preTimingTicket,
            .clearTargets = clearTargets,
            .hasTransparentRenderers = hasTransparentRenderers,
            .splitStages = splitStages,
        }
    );
    if(!m_avboitPreTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT pre graph task"));
        return;
    }

    if(splitStages){
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(coverage),
            ReadWriteUse(depthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(control, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        Core::GpuTaskSchedulingHint computeScheduling;
        computeScheduling.cost = Core::GpuTaskCostHint::Medium;
        computeScheduling.forceSubmissionBoundary = true;
        computeScheduling.allowPacketMerge = false;
        const Core::GpuTaskId preDependency[] = { m_avboitPreTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(ComputeQueueRequest())
            .setScheduling(computeScheduling)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_avboitDepthWarpTask = m_avboitTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &depthWarpTimingTicket,
            }
        );
        if(!m_avboitDepthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT depth-warp graph task"));
            return;
        }

        const Core::GpuTaskResourceUse extinctionResourceUses[] = {
            // The low-resolution framebuffer is rebound for the no-color-write extinction raster stage.
            ReadUse(lowRaster, Core::ResourceStates::RenderTarget),
            ReadUse(depthWarp),
            ReadUse(control),
            ReadWriteUse(extinction, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(extinctionOverflow, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(materialDomain),
            ReadUse(csgDomain),
        };
        const Core::GpuTaskId depthWarpDependency[] = { m_avboitDepthWarpTask };
        Core::GpuTaskDesc extinctionDesc;
        extinctionDesc
            .setIdentity(Name("render.avboit.extinction"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(graphicsScheduling)
            .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
            .setResourceUses(extinctionResourceUses, LengthOf(extinctionResourceUses))
        ;
        m_avboitExtinctionTask = m_avboitTaskGraph.addTask<AvboitExtinctionGraphTask>(
            extinctionDesc,
            AvboitExtinctionGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .csgFrameState = &csgFrameState,
                .timingTicket = &extinctionTimingTicket,
            }
        );
        if(!m_avboitExtinctionTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction graph task"));
            return;
        }

        const Core::GpuTaskResourceUse integrationResourceUses[] = {
            ReadUse(extinction),
            ReadUse(control),
            ReadUse(extinctionOverflow),
            ReadWriteUse(transmittance, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId extinctionDependency[] = { m_avboitExtinctionTask };
        Core::GpuTaskDesc integrationDesc;
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(ComputeQueueRequest())
            .setScheduling(computeScheduling)
            .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
            .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
        ;
        m_avboitIntegrationTask = m_avboitTaskGraph.addTask<AvboitIntegrationGraphTask>(
            integrationDesc,
            AvboitIntegrationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &integrationTimingTicket,
            }
        );
        if(!m_avboitIntegrationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT integration graph task"));
            return;
        }

        const Core::GpuTaskResourceUse accumulationResourceUses[] = {
            ReadUse(depth),
            ReadUse(transmittance),
            ReadUse(depthWarp),
            ReadUse(control),
            ReadWriteUse(accumColor, Core::ResourceStates::RenderTarget),
            ReadWriteUse(accumExtinction, Core::ResourceStates::RenderTarget),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(materialDomain),
            ReadUse(csgDomain),
        };
        const Core::GpuTaskId integrationDependency[] = { m_avboitIntegrationTask };
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(graphicsScheduling)
            .setDependencies(integrationDependency, LengthOf(integrationDependency))
            .setResourceUses(accumulationResourceUses, LengthOf(accumulationResourceUses))
        ;
        m_avboitAccumulationTask = m_avboitTaskGraph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            AvboitAccumulationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets,
                .csgFrameState = &csgFrameState,
                .timingTicket = &accumulationTimingTicket,
            }
        );
        if(!m_avboitAccumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation graph task"));
            return;
        }
    }

    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_avboitTaskGraph,
        m_avboitTaskGraphAnalysis,
        topology,
        m_avboitTaskGraphQueueAssignments,
        m_avboitCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile AVBOIT task graph"));
        return;
    }
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_avboitTaskGraphValid = true;
}


void RendererSystem::buildDeferredLightingTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    const bool useLaggedLightingHistory = schedule.usesLaggedLightingHistory();
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
        || (useLaggedLightingHistory && (!history || !history->valid()))
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId sceneShading = importBuffer(
        m_deferredState.m_sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    const Core::GpuGraphResourceId lights = importBuffer(
        m_deferredState.m_lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        history ? history->slotsBuffer : deferredTargets.bindless.slotsBuffer,
        Name("render.deferred_lighting.bindless_slots"),
        history ? "Lagged Deferred Bindless Slots" : "Deferred Bindless Slots"
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !bindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc avboitCompletionDesc;
    avboitCompletionDesc
        .setIdentity(Name("render.deferred_lighting.avboit_complete"))
        .setMarkerLabel("AVBOIT Complete")
    ;
    m_deferredLightingAvboitCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        avboitCompletionDesc
    );
    if(!m_deferredLightingAvboitCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import AVBOIT completion for deferred lighting"));
        return;
    }

    Core::GpuExternalCompletionDesc dependentEffectsCompletionDesc;
    dependentEffectsCompletionDesc
        .setIdentity(
            useLaggedLightingHistory
                ? Name("render.deferred_lighting.lagged_history_complete")
                : Name("render.deferred_lighting.surfel_gi_complete")
        )
        .setMarkerLabel(useLaggedLightingHistory ? "Lagged Lighting History Complete" : "Surfel GI Complete")
    ;
    Core::GpuExternalCompletionId dependentEffectsCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        dependentEffectsCompletionDesc
    );
    if(!dependentEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting dependent completion"));
        return;
    }
    if(useLaggedLightingHistory)
        m_deferredLightingHistoryCompletion = dependentEffectsCompletion;
    else
        m_deferredLightingSurfelGiCompletion = dependentEffectsCompletion;

    const Core::GpuExternalCompletionId externalDependencies[] = {
        m_deferredLightingAvboitCompletion,
        dependentEffectsCompletion,
    };
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(albedo),
        ReadUse(normal),
        ReadUse(worldPosition),
        ReadUse(depth),
        ReadUse(shadowVisibility),
        ReadUse(causticIrradiance),
        ReadUse(surfelIrradiance),
        ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer),
        ReadUse(lights),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(opaqueColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.avoidQueueCrossing = useLaggedLightingHistory;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, LengthOf(externalDependencies))
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredLightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        timingTicket
    );
    if(!m_deferredLightingTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-lighting task graph"));
        return;
    }
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingTaskGraphValid = true;
}


void RendererSystem::buildDeferredCompositeTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredCompositeTaskGraphValid = false;
    m_deferredCompositeTask = {};
    m_deferredCompositeLightingCompletion = {};
    m_deferredCompositeTaskGraph.reset();
    m_deferredCompositeTaskGraphAnalysis.reset();
    m_deferredCompositeTaskGraphQueueAssignments.reset();
    m_deferredCompositeCompiledGraph.reset();
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.slotsBuffer)
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredCompositeTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_composite.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.deferred_composite.avboit_accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId avboitAccumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.deferred_composite.avboit_accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId bindlessSlots = m_deferredCompositeTaskGraph.importBuffer(
        deferredTargets.bindless.slotsBuffer,
        BufferResourceDesc(Name("render.deferred_composite.bindless_slots"), "Deferred Bindless Slots")
    );
    if(
        !opaqueColor.valid()
        || !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !compositeColor.valid()
        || !bindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc lightingCompletionDesc;
    lightingCompletionDesc
        .setIdentity(Name("render.deferred_composite.deferred_lighting_complete"))
        .setMarkerLabel("Deferred Lighting Complete")
    ;
    m_deferredCompositeLightingCompletion = m_deferredCompositeTaskGraph.importExternalCompletion(
        lightingCompletionDesc
    );
    if(!m_deferredCompositeLightingCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting completion for composite"));
        return;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(opaqueColor),
        ReadUse(avboitAccumColor),
        ReadUse(avboitAccumExtinction),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.avoidQueueCrossing = schedule.usesLaggedLightingHistory();
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_deferredCompositeLightingCompletion, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredCompositeTask = m_deferredSystem.declareDeferredCompositeTask(
        desc,
        deferredTargets,
        timingTicket
    );
    if(!m_deferredCompositeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredCompositeTaskGraph,
        m_deferredCompositeTaskGraphAnalysis,
        topology,
        m_deferredCompositeTaskGraphQueueAssignments,
        m_deferredCompositeCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-composite task graph"));
        return;
    }
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeTaskGraphValid = true;
}


void RendererSystem::buildDeferredPresentTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::Framebuffer* const presentationFramebuffer,
    const bool waitsForSurfelGi,
    const bool shadowVisibilityRunsOnCompute,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !presentationFramebuffer
        || waitsForSurfelGi != schedule.usesLaggedLightingHistory()
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredPresentTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_present.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId bindlessSlots = m_deferredPresentTaskGraph.importBuffer(
        deferredTargets.bindless.slotsBuffer,
        BufferResourceDesc(Name("render.deferred_present.bindless_slots"), "Deferred Bindless Slots")
    );
    const Core::GpuGraphResourceId backbuffer = m_deferredPresentTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.deferred_present.backbuffer"), "Presentation Back Buffer")
    );
    if(!compositeColor.valid() || !bindlessSlots.valid() || !backbuffer.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc compositeCompletionDesc;
    compositeCompletionDesc
        .setIdentity(Name("render.deferred_present.deferred_composite_complete"))
        .setMarkerLabel("Deferred Composite Complete")
    ;
    m_deferredPresentCompositeCompletion = m_deferredPresentTaskGraph.importExternalCompletion(
        compositeCompletionDesc
    );
    if(!m_deferredPresentCompositeCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite completion for present"));
        return;
    }

    Core::GpuExternalCompletionId externalDependencies[2] = { m_deferredPresentCompositeCompletion };
    usize externalDependencyCount = 1u;
    if(waitsForSurfelGi){
        Core::GpuExternalCompletionDesc surfelGiCompletionDesc;
        surfelGiCompletionDesc
            .setIdentity(Name("render.deferred_present.surfel_gi_complete"))
            .setMarkerLabel("Surfel GI Complete")
        ;
        m_deferredPresentSurfelGiCompletion = m_deferredPresentTaskGraph.importExternalCompletion(
            surfelGiCompletionDesc
        );
        if(!m_deferredPresentSurfelGiCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel-GI completion for present"));
            return;
        }
        externalDependencies[externalDependencyCount++] = m_deferredPresentSurfelGiCompletion;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.avoidQueueCrossing = waitsForSurfelGi;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, externalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredPresentTask = m_deferredPresentTaskGraph.addTask<DeferredPresentGraphTask>(
        desc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &deferredTargets,
            .presentationFramebuffer = presentationFramebuffer,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &timingTicket,
            .shadowVisibilityRunsOnCompute = shadowVisibilityRunsOnCompute,
        }
    );
    if(!m_deferredPresentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_gpuTaskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredPresentTaskGraph,
        m_deferredPresentTaskGraphAnalysis,
        topology,
        m_deferredPresentTaskGraphQueueAssignments,
        m_deferredPresentCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-present task graph"));
        return;
    }
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentTaskGraphValid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

