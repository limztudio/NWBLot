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

[[nodiscard]] static WorkMetadata WorkMetadataFor(
    const Work::Enum work,
    const bool hardwareCaustics,
    const bool usesLaggedLightingHistory
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
    case Work::SurfelGi:
        return WorkMetadata{ Name("render.task_graph.surfel_gi"), "Surfel GI", ComputeQueueRequest() };
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
    case Work::DeferredLighting:{
        WorkMetadata metadata{ Name("render.task_graph.deferred_lighting"), "Deferred Lighting", ComputeQueueRequest() };
        // The active lagged path keeps its final lighting chain on Graphics to avoid a no-overlap return crossing
        // before presentation. This is a semantic scheduling hint, not a live FrameExecutionPlan override.
        metadata.scheduling.avoidQueueCrossing = usesLaggedLightingHistory;
        return metadata;
    }
    case Work::DeferredComposite:{
        WorkMetadata metadata{ Name("render.task_graph.deferred_composite"), "Deferred Composite", ComputeQueueRequest() };
        metadata.scheduling.avoidQueueCrossing = usesLaggedLightingHistory;
        return metadata;
    }
    case Work::GraphicsPresent:
        return WorkMetadata{ Name("render.task_graph.present"), "Present", GraphicsQueueRequest() };
    case Work::LaggedLightingStash:
        return WorkMetadata{ Name("render.task_graph.lagged_lighting_stash"), "Lagged Lighting Stash", ComputeQueueRequest() };
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

    Core::GpuGraphResourceId historyShadowVisibility;
    Core::GpuGraphResourceId historyCausticIrradiance;
    Core::GpuGraphResourceId historySurfelIrradiance;
    const bool capturesLaggedLightingHistory = graphSchedule.capturesLaggedLightingHistory();
    if(capturesLaggedLightingHistory){
        const DeferredLaggedLightingHistoryResources& history = deferredTargets.laggedLightingHistory;
        historyShadowVisibility = importTexture(
            history.shadowVisibility,
            Name("render.task_graph.history_shadow_visibility"),
            "History Shadow Visibility"
        );
        historyCausticIrradiance = importTexture(
            history.causticIrradiance,
            Name("render.task_graph.history_caustic_irradiance"),
            "History Caustic Irradiance"
        );
        historySurfelIrradiance = importTexture(
            history.surfelIrradiance,
            Name("render.task_graph.history_surfel_irradiance"),
            "History Surfel Irradiance"
        );
        if(!historyShadowVisibility.valid() || !historyCausticIrradiance.valid() || !historySurfelIrradiance.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: GPU task graph could not import lagged-lighting history"));
            return;
        }
    }

    const bool consumesLaggedLightingHistory = graphSchedule.usesLaggedLightingHistory();
    Core::GpuExternalCompletionId laggedHistoryCompletion;
    if(consumesLaggedLightingHistory){
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

    const bool usesLaggedLightingHistory = graphSchedule.usesLaggedLightingHistory();

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

        const WorkMetadata metadata = WorkMetadataFor(work, input.hardwareCaustics, usesLaggedLightingHistory);
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
    const Core::GpuTaskResourceUse surfelGiUses[] = {
        ReadUse(worldPosition),
        ReadUse(normal),
        ReadUse(depth, Core::ResourceStates::DepthRead),
        WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess),
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
    const Core::GpuGraphResourceId lightingShadowVisibility = usesLaggedLightingHistory
        ? historyShadowVisibility
        : shadowVisibility
    ;
    const Core::GpuGraphResourceId lightingCausticIrradiance = usesLaggedLightingHistory
        ? historyCausticIrradiance
        : causticIrradiance
    ;
    const Core::GpuGraphResourceId lightingSurfelIrradiance = usesLaggedLightingHistory
        ? historySurfelIrradiance
        : surfelIrradiance
    ;
    const Core::GpuTaskResourceUse deferredLightingUses[] = {
        ReadUse(albedo),
        ReadUse(normal),
        ReadUse(worldPosition),
        ReadUse(depth, Core::ResourceStates::DepthRead),
        ReadUse(lightingShadowVisibility),
        ReadUse(lightingCausticIrradiance),
        ReadUse(lightingSurfelIrradiance),
        WriteUse(opaqueColor, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse deferredCompositeUses[] = {
        ReadUse(opaqueColor),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    const Core::GpuTaskResourceUse presentUses[] = {
        ReadUse(compositeColor),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    const Core::GpuTaskResourceUse laggedLightingStashUses[] = {
        ReadUse(shadowVisibility, Core::ResourceStates::CopySource),
        ReadUse(causticIrradiance, Core::ResourceStates::CopySource),
        ReadUse(surfelIrradiance, Core::ResourceStates::CopySource),
        WriteUse(historyShadowVisibility, Core::ResourceStates::CopyDest),
        WriteUse(historyCausticIrradiance, Core::ResourceStates::CopyDest),
        WriteUse(historySurfelIrradiance, Core::ResourceStates::CopyDest),
    };

    const bool graphBuilt =
        addWorkTask(Work::GraphicsPrefix, graphicsPrefixUses, LengthOf(graphicsPrefixUses))
        && addWorkTask(Work::RayEffects, rayEffectsUses, LengthOf(rayEffectsUses))
        && addWorkTask(Work::Caustics, causticsUses, LengthOf(causticsUses))
        && addWorkTask(Work::SurfelGi, surfelGiUses, LengthOf(surfelGiUses))
        && addWorkTask(Work::AvboitRaster, avboitRasterUses, LengthOf(avboitRasterUses))
        && addWorkTask(Work::AsyncEffectsTiming, nullptr, 0u)
        && addWorkTask(Work::AvboitDepthWarp, avboitDepthWarpUses, LengthOf(avboitDepthWarpUses))
        && addWorkTask(Work::AvboitExtinction, avboitExtinctionUses, LengthOf(avboitExtinctionUses))
        && addWorkTask(Work::AvboitIntegration, avboitIntegrationUses, LengthOf(avboitIntegrationUses))
        && addWorkTask(Work::AvboitAccumulation, avboitAccumulationUses, LengthOf(avboitAccumulationUses))
        && addWorkTask(Work::DeferredLighting, deferredLightingUses, LengthOf(deferredLightingUses))
        && addWorkTask(Work::DeferredComposite, deferredCompositeUses, LengthOf(deferredCompositeUses))
        && addWorkTask(Work::GraphicsPresent, presentUses, LengthOf(presentUses))
        && addWorkTask(Work::LaggedLightingStash, laggedLightingStashUses, LengthOf(laggedLightingStashUses))
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

