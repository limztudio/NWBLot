// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Packet = ECSRenderDetail::FrameExecutionPacket;
namespace Work = ECSRenderDetail::FrameExecutionWork;
namespace ExternalWait = ECSRenderDetail::FrameExecutionExternalWait;

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


[[nodiscard]] static WorkMetadata WorkMetadataFor(
    const Work::Enum work,
    const bool hardwareCaustics
){
    switch(work){
    case Work::GraphicsPrefix:
        return WorkMetadata{ Name("render.task_graph.graphics_prefix"), "Graphics Prefix", GraphicsQueueRequest() };
    case Work::RayEffects:
        return WorkMetadata{ Name("render.task_graph.ray_effects"), "Ray Effects", ComputeQueueRequest() };
    case Work::Caustics:
        return WorkMetadata{
            Name("render.task_graph.caustics"),
            "Caustics",
            hardwareCaustics ? GraphicsQueueRequest() : ComputeQueueRequest(),
        };
    case Work::AvboitRaster:
        return WorkMetadata{ Name("render.task_graph.avboit_raster"), "AVBOIT Raster", GraphicsQueueRequest() };
    case Work::AsyncEffectsTiming:
        return WorkMetadata{ Name("render.task_graph.async_effects_timing"), "Async Effects Timing", GraphicsQueueRequest() };
    case Work::AvboitDepthWarp:
        return WorkMetadata{ Name("render.task_graph.avboit_depth_warp"), "AVBOIT Depth Warp", ComputeQueueRequest() };
    case Work::AvboitExtinction:
        return WorkMetadata{ Name("render.task_graph.avboit_extinction"), "AVBOIT Extinction", GraphicsQueueRequest() };
    case Work::AvboitIntegration:
        return WorkMetadata{ Name("render.task_graph.avboit_integration"), "AVBOIT Integration", ComputeQueueRequest() };
    case Work::AvboitAccumulation:
        return WorkMetadata{ Name("render.task_graph.avboit_accumulation"), "AVBOIT Accumulation", GraphicsQueueRequest() };
    case Work::GraphicsPresent:
        return WorkMetadata{ Name("render.task_graph.present"), "Present", GraphicsQueueRequest() };
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

[[nodiscard]] static bool PacketPathExists(
    const ECSRenderDetail::FrameExecutionPlan& frameExecutionPlan,
    const Packet::Enum producer,
    const Packet::Enum consumer
){
    if(producer == consumer)
        return true;

    bool visited[Packet::kCount] = {};
    const auto visit = [&](auto&& self, const Packet::Enum current) -> bool{
        if(current == producer)
            return true;

        const usize currentIndex = static_cast<usize>(current);
        if(visited[currentIndex])
            return false;
        visited[currentIndex] = true;

        const ECSRenderDetail::FrameExecutionPacketPlan& packetPlan = frameExecutionPlan.packet(current);
        for(u8 waitIndex = 0u; waitIndex < packetPlan.waitPacketCount; ++waitIndex){
            if(self(self, packetPlan.waitPackets[waitIndex]))
                return true;
        }
        return false;
    };
    return visit(visit, consumer);
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

    // This first live task is independent of the observational sidecar below.  A telemetry/parity failure in an
    // unmigrated pass must not restore the retired history-copy packet or its persistent command list.
    buildLaggedLightingHistoryTaskGraph(input, deferredTargets);

    m_gpuTaskGraphValid = false;
    m_gpuTaskGraphLiveRoutingValid = false;
    m_gpuTaskGraphWorkTasks.clear();
    m_gpuTaskGraphWorkQueues.clear();
    m_gpuTaskGraphLegacyMismatches.clear();
    m_gpuTaskGraphLegacyQueueMismatches.clear();
    m_gpuTaskGraph.reset();
    m_gpuTaskGraphAnalysis.reset();
    m_gpuTaskGraphQueueAssignments.reset();

    m_gpuTaskGraphWorkTasks.reserve(Work::kCount);
    m_gpuTaskGraphWorkQueues.reserve(Work::kCount);
    for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
        m_gpuTaskGraphWorkTasks.push_back(Core::GpuTaskId{});
        m_gpuTaskGraphWorkQueues.push_back(Core::CommandQueue::kCount);
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
        || dedicatedAsyncCompute != frameExecutionPlan.workRunsOnLane(
            Work::RayEffects,
            Core::RenderLane::AsyncCompute
        )
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
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        deferredTargets.shadowVisibility,
        Name("render.task_graph.shadow_visibility"),
        "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.task_graph.caustic_irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        deferredTargets.surfelIrradiance,
        Name("render.task_graph.surfel_irradiance"),
        "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.task_graph.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.task_graph.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId meshViewBuffer = m_gpuTaskGraph.importBuffer(
        m_drawState.m_meshViewBuffer,
        BufferResourceDesc(Name("render.task_graph.mesh_view"), "Mesh View")
    );
    const Core::GpuGraphResourceId backbuffer = m_gpuTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.task_graph.backbuffer"), "Back Buffer")
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
        || !compositeColor.valid()
        || !meshViewBuffer.valid()
        || !backbuffer.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not import required renderer resources"));
        return;
    }

    // AVBOIT raster work clears these targets even when no transparent draw needs the split compute stages, so the
    // semantic graph must import them on every valid deferred-target generation.
    const Core::GpuGraphResourceId avboitLowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.task_graph.avboit_low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.task_graph.avboit_accum_color"),
        "AVBOIT Accumulation Color"
    );
    const Core::GpuGraphResourceId avboitExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.task_graph.avboit_extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId avboitTransmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.task_graph.avboit_transmittance"),
        "AVBOIT Transmittance"
    );
    if(!avboitLowRaster.valid() || !avboitAccumColor.valid() || !avboitExtinction.valid() || !avboitTransmittance.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not import AVBOIT resources"));
        return;
    }

    Core::GpuExternalCompletionId laggedHistoryCompletion;
    if(graphSchedule.workWaitsForLaggedLightingHistory(Work::Caustics)){
        Core::GpuExternalCompletionDesc completion;
        completion
            .setIdentity(Name("render.task_graph.lagged_lighting_history_ready"))
            .setMarkerLabel("Lagged Lighting History Ready")
        ;
        laggedHistoryCompletion = m_gpuTaskGraph.importExternalCompletion(completion);
        if(!laggedHistoryCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not import lagged-lighting completion"));
            return;
        }
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
        Core::GpuExternalCompletionId externalDependencies[ExternalWait::kCount] = {};
        usize externalDependencyCount = 0u;
        if(graphSchedule.workWaitsForLaggedLightingHistory(work)){
            if(!laggedHistoryCompletion.valid())
                return false;
            externalDependencies[externalDependencyCount++] = laggedHistoryCompletion;
        }

        const WorkMetadata metadata = WorkMetadataFor(work, input.hardwareCaustics);
        Core::GpuTaskDesc desc;
        desc
            .setIdentity(metadata.identity)
            .setMarkerLabel(metadata.markerLabel)
            .setQueue(metadata.queue)
            .setScheduling(metadata.scheduling)
            .setDependencies(dependencies, dependencyCount)
            .setExternalDependencies(externalDependencies, externalDependencyCount)
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
    const Core::GpuTaskResourceUse rayEffectsUses[] = {
        ReadUse(meshViewBuffer),
        ReadUse(worldPosition),
        ReadUse(normal),
        ReadUse(depth, Core::ResourceStates::DepthRead),
        WriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse causticsUses[] = {
        ReadUse(worldPosition),
        ReadUse(normal),
        ReadUse(depth, Core::ResourceStates::DepthRead),
        WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse avboitRasterUses[] = {
        ReadUse(depth, Core::ResourceStates::DepthRead),
        WriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget),
        WriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget),
    };
    const Core::GpuTaskResourceUse avboitDepthWarpUses[] = {
        ReadUse(avboitLowRaster),
        WriteUse(avboitAccumColor, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse avboitExtinctionUses[] = {
        ReadUse(avboitAccumColor),
        WriteUse(avboitExtinction, Core::ResourceStates::RenderTarget),
    };
    const Core::GpuTaskResourceUse avboitIntegrationUses[] = {
        ReadUse(avboitExtinction),
        WriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse avboitAccumulationUses[] = {
        ReadUse(avboitTransmittance),
        WriteUse(opaqueColor, Core::ResourceStates::RenderTarget),
    };
    const Core::GpuTaskResourceUse presentUses[] = {
        ReadUse(compositeColor),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    const bool graphBuilt =
        addWorkTask(Work::GraphicsPrefix, graphicsPrefixUses, LengthOf(graphicsPrefixUses))
        && addWorkTask(Work::RayEffects, rayEffectsUses, LengthOf(rayEffectsUses))
        && addWorkTask(Work::Caustics, causticsUses, LengthOf(causticsUses))
        && addWorkTask(Work::AvboitRaster, avboitRasterUses, LengthOf(avboitRasterUses))
        && addWorkTask(Work::AsyncEffectsTiming, nullptr, 0u)
        && addWorkTask(Work::AvboitDepthWarp, avboitDepthWarpUses, LengthOf(avboitDepthWarpUses))
        && addWorkTask(Work::AvboitExtinction, avboitExtinctionUses, LengthOf(avboitExtinctionUses))
        && addWorkTask(Work::AvboitIntegration, avboitIntegrationUses, LengthOf(avboitIntegrationUses))
        && addWorkTask(Work::AvboitAccumulation, avboitAccumulationUses, LengthOf(avboitAccumulationUses))
        && addWorkTask(Work::GraphicsPresent, presentUses, LengthOf(presentUses))
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph work set differs from FrameExecutionPlan; retaining legacy command-list routing for this frame ({})")
            , workSetMismatchCount
        );
    }

    bool queueRoutingMatchesLegacyPlan = workSetMatchesLegacyPlan;
    for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
        const Work::Enum work = static_cast<Work::Enum>(workIndex);
        if(!graphSchedule.hasWork(work))
            continue;

        if(!frameExecutionPlan.hasWork(work))
            continue;

        const Core::GpuTaskQueueAssignment* const assignment = m_gpuTaskGraphQueueAssignments.find(workTasks[workIndex]);
        if(!assignment){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue assignment is missing declared work"));
            queueRoutingMatchesLegacyPlan = false;
            continue;
        }
        m_gpuTaskGraphWorkQueues[workIndex] = assignment->queueClass;
        const Core::CommandQueue::Enum legacyQueue = device.resolveRenderLane(frameExecutionPlan.laneForWork(work));
        if(
            legacyQueue != frameExecutionPlan.expectedQueueForWork(work)
            || !frameExecutionPlan.workMatchesExpectedQueue(work, assignment->queueClass)
        ){
            queueRoutingMatchesLegacyPlan = false;
            m_gpuTaskGraphLegacyQueueMismatches.push_back(workTasks[workIndex]);
        }
    }
    if(!m_gpuTaskGraphLegacyQueueMismatches.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph queue assignment differs from FrameExecutionPlan; retaining legacy command-list routing for this frame ({})")
            , m_gpuTaskGraphLegacyQueueMismatches.size()
        );
    }

    const auto workForTask = [&](const Core::GpuTaskId task, Work::Enum& outWork){
        for(usize workIndex = 0u; workIndex < Work::kCount; ++workIndex){
            if(workTasks[workIndex] != task)
                continue;
            outWork = static_cast<Work::Enum>(workIndex);
            return true;
        }
        return false;
    };
    const auto packetForTask = [&](const Core::GpuTaskId task, Packet::Enum& outPacket){
        Work::Enum work = Work::kCount;
        if(!workForTask(task, work) || !frameExecutionPlan.hasWork(work))
            return false;
        outPacket = frameExecutionPlan.packetForWork(work);
        return true;
    };
    const auto recordLegacyScheduleMismatch = [&](const Core::GpuTaskDependencyEdge& graphEdge){
        for(const Core::GpuTaskDependencyEdge& mismatch : m_gpuTaskGraphLegacyMismatches){
            if(mismatch.producer == graphEdge.producer && mismatch.consumer == graphEdge.consumer)
                return;
        }
        m_gpuTaskGraphLegacyMismatches.push_back(graphEdge);
    };
    bool schedulingMatchesLegacyPlan = workSetMatchesLegacyPlan;
    for(const Core::GpuTaskDependencyEdge& graphEdge : m_gpuTaskGraphAnalysis.edges()){
        Packet::Enum producerPacket = Packet::kCount;
        Packet::Enum consumerPacket = Packet::kCount;
        if(
            !packetForTask(graphEdge.producer, producerPacket)
            || !packetForTask(graphEdge.consumer, consumerPacket)
        ){
            schedulingMatchesLegacyPlan = false;
            continue;
        }
        if(PacketPathExists(frameExecutionPlan, producerPacket, consumerPacket))
            continue;

        schedulingMatchesLegacyPlan = false;
        recordLegacyScheduleMismatch(graphEdge);
    }
    if(!m_gpuTaskGraphLegacyMismatches.empty()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph found {} semantic dependencies absent from FrameExecutionPlan")
            , m_gpuTaskGraphLegacyMismatches.size()
        );
    }

    bool externalDependenciesMatchLegacyPlan = true;
    usize externalDependencyMismatchCount = 0u;
    for(const Core::GpuTaskExternalDependencyEdge& graphEdge : m_gpuTaskGraphAnalysis.externalDependencies()){
        Work::Enum consumerWork = Work::kCount;
        if(
            graphEdge.completion != laggedHistoryCompletion
            || !workForTask(graphEdge.consumer, consumerWork)
            || !frameExecutionPlan.workWaitsForExternalToken(
                consumerWork,
                ExternalWait::LaggedLightingHistory
            )
        ){
            externalDependenciesMatchLegacyPlan = false;
            ++externalDependencyMismatchCount;
        }
    }
    if(!externalDependenciesMatchLegacyPlan){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph found {} external dependencies absent from FrameExecutionPlan; retaining legacy command-list routing for this frame")
            , externalDependencyMismatchCount
        );
    }

    m_gpuTaskGraphValid = true;
    m_gpuTaskGraphLiveRoutingValid =
        queueRoutingMatchesLegacyPlan
        && schedulingMatchesLegacyPlan
        && externalDependenciesMatchLegacyPlan
    ;
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

    Core::GpuExternalCompletionDesc effectsCompletionDesc;
    effectsCompletionDesc
        .setIdentity(Name("render.surfel_gi.ray_effects_complete"))
        .setMarkerLabel("Ray Effects Complete")
    ;
    m_surfelGiEffectsCompletion = m_surfelGiTaskGraph.importExternalCompletion(effectsCompletionDesc);
    if(!m_surfelGiEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import ray-effects completion for surfel GI"));
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


void RendererSystem::buildDeferredLightingTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredLightingGraphicsEffectsCompletion = {};
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

    Core::GpuExternalCompletionDesc graphicsEffectsCompletionDesc;
    graphicsEffectsCompletionDesc
        .setIdentity(Name("render.deferred_lighting.graphics_effects_complete"))
        .setMarkerLabel("Graphics Effects Complete")
    ;
    m_deferredLightingGraphicsEffectsCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        graphicsEffectsCompletionDesc
    );
    if(!m_deferredLightingGraphicsEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-effects completion for deferred lighting"));
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
        m_deferredLightingGraphicsEffectsCompletion,
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

