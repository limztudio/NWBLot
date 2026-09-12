// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/occupancy_record_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_clear_timing.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitOccupancyRecordBuilder::AvboitOccupancyRecordBuilder(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererMaterialSystem& materialSystem,
    RendererAvboitSystem& avboitSystem
)
    : m_graph(graph)
    , m_graphics(graphics)
    , m_materialSystem(materialSystem)
    , m_avboitSystem(avboitSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitOccupancyRecordBuilder::declare(
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    AvboitOccupancyRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitOccupancyGraphTask::Payload& occupancyPayload,
    RendererTaskGraphDetail::AvboitOccupancyComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitOccupancyRecordResult& outResult
){
    using namespace RendererTaskGraphDetail;
    static_cast<void>(csgResources);
    outResult = AvboitOccupancyRecordResult{};
    if(!inputs.targets)
        return false;
    if(!inputs.preTimingTicket)
        return false;
    if(!inputs.occupancyComputeEmulationTiming)
        return false;
    if(!inputs.clearTask.valid() || !inputs.uploadTask.valid())
        return false;

    const bool occupancyCsgIntervalSampleImageStatesGraphOwned =
        inputs.intervalOutputsGraphOwned && inputs.csgStreamsUploaded
    ;
    const bool occupancyCsgClipBufferStatesGraphOwned = inputs.csgStreamsUploaded;
    NWB_ASSERT(
        !occupancyCsgIntervalSampleImageStatesGraphOwned
        || (
            occupancyPayload.occupancyStreamsUploaded
            && occupancyPayload.occupancySnapshot.captured
        )
    );
    NWB_ASSERT(
        !occupancyCsgClipBufferStatesGraphOwned
        || (
            occupancyPayload.occupancyStreamsUploaded
            && occupancyPayload.occupancySnapshot.captured
        )
    );
    occupancyPayload.occupancyCsgIntervalSampleImageStatesGraphOwned =
        occupancyCsgIntervalSampleImageStatesGraphOwned
    ;
    occupancyPayload.occupancyCsgClipBufferStatesGraphOwned =
        occupancyCsgClipBufferStatesGraphOwned
    ;
    occupancyPayload.occupancyMaterialFrameStatesGraphOwned = occupancyPayload.occupancyStreamsUploaded;
    occupancyPayload.occupancyMaterialGeometryStatesGraphOwned =
        occupancyPayload.occupancyStreamsUploaded
        && occupancyPayload.occupancyMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId occupancyComputeEmulationOutputSet;
    Core::Alloc::ScratchArena occupancyComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool occupancyComputeEmulationPlanCaptured =
        inputs.regularComputeEmulationPlanCaptured
        || inputs.csgComputeEmulationPlanCaptured
    ;
    bool occupancyComputeEmulationOutputStatesGraphOwned = false;
    if(inputs.regularComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned = GatherImportedOutputBufferResourceSet(
            m_graph,
            computeEmulationPayload.plan,
            occupancyComputeEmulationResourceScratch,
            Name("render.avboit.occupancy.compute_emulation.outputs"),
            "AVBOIT Occupancy Compute Emulation Outputs",
            occupancyComputeEmulationOutputSet
        );
    }
    else if(inputs.csgComputeEmulationPlanCaptured){
        occupancyComputeEmulationOutputStatesGraphOwned =
            GatherImportedOutputBufferResourceSet(
                m_graph,
                computeEmulationPayload.csgPlan,
                occupancyComputeEmulationResourceScratch,
                Name("render.avboit.occupancy.csg_compute_emulation.outputs"),
                "AVBOIT Occupancy CSG Compute Emulation Outputs",
                occupancyComputeEmulationOutputSet
            )
        ;
    }
    if(
        occupancyComputeEmulationPlanCaptured
        && !occupancyComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy compute-emulation output states"
        ));
    }
    occupancyPayload.occupancyComputeEmulationOutputStatesGraphOwned =
        inputs.regularComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    occupancyPayload.occupancyCsgComputeEmulationOutputStatesGraphOwned =
        inputs.csgComputeEmulationPlanCaptured
        && occupancyComputeEmulationOutputStatesGraphOwned
    ;
    occupancyPayload.occupancyComputeEmulationTiming =
        occupancyComputeEmulationOutputStatesGraphOwned
            ? inputs.occupancyComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId occupancySharedComputeEmulationOutput;
    const bool occupancySharedComputeEmulationOutputStatesGraphOwned =
        inputs.sharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_graph,
            inputs.sharedComputeEmulationPlan,
            "AVBOIT Occupancy Shared Compute Emulation Output",
            occupancySharedComputeEmulationOutput
        )
    ;
    if(
        inputs.sharedComputeEmulationPlanCaptured
        && !occupancySharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Occupancy shared compute-emulation output state"
        ));
    }

    const Core::BufferRange occupancyInstanceRange(
        0u,
        occupancyPayload.occupancySnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange occupancyMaterialTypedRange(0u, occupancyPayload.occupancySnapshot.materialTypedByteCount);
    const Core::BufferRange occupancyReceiverRange(
        0u,
        occupancyPayload.occupancySnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange occupancyCutterRange(
        0u,
        occupancyPayload.occupancySnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );
    inputs.instanceRange = occupancyInstanceRange;
    inputs.materialTypedRange = occupancyMaterialTypedRange;
    inputs.receiverRange = occupancyReceiverRange;
    inputs.cutterRange = occupancyCutterRange;

    Core::Alloc::ScratchArena avboitPreResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> avboitPreResourceUses{ avboitPreResourceScratch };
    avboitPreResourceUses.reserve(
        13u
        + (occupancyPayload.occupancyStreamsUploaded ? 7u : 0u)
        + (occupancyCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    avboitPreResourceUses.push_back(ReadUse(inputs.albedo));
    avboitPreResourceUses.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    avboitPreResourceUses.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
    avboitPreResourceUses.push_back(ReadUse(inputs.depth));
    avboitPreResourceUses.push_back(ReadUse(inputs.refractionInstance));
    avboitPreResourceUses.push_back(ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget));
    avboitPreResourceUses.push_back(ReadWriteUse(inputs.avboitCoverage, Core::ResourceStates::UnorderedAccess));
    if(occupancyPayload.occupancyStreamsUploaded){
        avboitPreResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        avboitPreResourceUses.push_back(ReadBufferUse(inputs.materialInstances, occupancyInstanceRange));
        avboitPreResourceUses.push_back(ReadBufferUse(inputs.materialTyped, occupancyMaterialTypedRange));
        if(inputs.csgStreamsUploaded){
            avboitPreResourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, occupancyReceiverRange));
            avboitPreResourceUses.push_back(ReadBufferUse(inputs.csgCutters, occupancyCutterRange));
            avboitPreResourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // Interval producer owns this state; occupancy only samples it.
            avboitPreResourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        }
    }
    if(occupancyCsgIntervalSampleImageStatesGraphOwned){
        // Interval producer wrote these aliases; lower the required UAV handoff for occupancy shaders.
        avboitPreResourceUses.push_back(ReadTextureUse(
            inputs.csgRemovedIntervalDepth,
            inputs.csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            inputs.csgRemovedIntervalCapNormal,
            inputs.csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            inputs.csgRemovedIntervalData,
            inputs.csgRemovedIntervalSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
        avboitPreResourceUses.push_back(ReadTextureUse(
            inputs.csgRemovedIntervalCount,
            inputs.csgRemovedIntervalCountSubresources,
            Core::ResourceStates::UnorderedAccess
        ));
    }
    const Core::GpuTaskResourceSetUse occupancyMaterialGeometrySetUse{
        .resourceSet = inputs.materialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyMaterialSampledTextureSetUse{
        .resourceSet = inputs.materialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse occupancyMaterialResourceSetUses[3u] = {};
    usize occupancyMaterialResourceSetUseCount = 0u;
    if(occupancyPayload.occupancyMaterialGeometryStatesGraphOwned)
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialGeometrySetUse;
    if(inputs.materialSampledTextureSet.valid())
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] = occupancyMaterialSampledTextureSetUse;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        occupancyMaterialResourceSetUses[occupancyMaterialResourceSetUseCount++] =
            occupancyComputeEmulationOutputVertexBufferSetUse;
    }
    avboitPreResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    avboitPreResourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    avboitPreResourceUses.push_back(ReadUse(inputs.avboitCsgDomain, Core::ResourceStates::ShaderResource));

    const Core::GpuTaskResourceSetUse occupancyComputeEmulationOutputUavSetUse{
        .resourceSet = occupancyComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    // Keep the final upload as stream anchor; replacing it would hide a broken producer handoff.
    const Core::GpuTaskId occupancyStreamTask = inputs.uploadTask;
    if(occupancyPayload.occupancyStreamsUploaded)
        m_avboitSystem.taskGraphStage().m_occupancyStreamTask = occupancyStreamTask;
    Core::GpuTaskId occupancyDependency = inputs.clearTask;

    Core::GpuTaskSchedulingHint avboitOccupancyScheduling;
    avboitOccupancyScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitOccupancyScheduling.forceSubmissionBoundary = false;
    avboitOccupancyScheduling.allowPacketMerge = true;
    avboitOccupancyScheduling.mergeWithPrevious = true;
    // Occupancy closes the serial AVBOIT Pre packet; keep timing across consumer frontiers.
    avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    if(occupancyComputeEmulationOutputStatesGraphOwned){
        computeEmulationPayload.graphics = &m_graphics;
        computeEmulationPayload.materialSystem = &m_materialSystem;
        computeEmulationPayload.targets = inputs.targets;
        computeEmulationPayload.timingTicket = inputs.preTimingTicket;
        computeEmulationPayload.occupancyTiming = inputs.occupancyComputeEmulationTiming;
        computeEmulationPayload.instanceCount = occupancyPayload.occupancySnapshot.instanceCount;
        computeEmulationPayload.materialTypedByteCount =
            occupancyPayload.occupancySnapshot.materialTypedByteCount;
        computeEmulationPayload.materialDrawBuffersUploaded =
            occupancyPayload.occupancyStreamsUploaded;
        computeEmulationPayload.csgFrameBuffersUploaded = inputs.csgStreamsUploaded;
        computeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            occupancyCsgIntervalSampleImageStatesGraphOwned;
        computeEmulationPayload.csgClipBufferStatesGraphOwned =
            occupancyCsgClipBufferStatesGraphOwned;
        computeEmulationPayload.materialFrameStatesGraphOwned =
            occupancyPayload.occupancyMaterialFrameStatesGraphOwned;
        computeEmulationPayload.materialGeometryStatesGraphOwned =
            occupancyPayload.occupancyMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancyComputeEmulationResourceUses{
            occupancyComputeEmulationResourceScratch
        };
        occupancyComputeEmulationResourceUses.reserve(
            4u + (inputs.csgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        occupancyComputeEmulationResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        occupancyComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialInstances, occupancyInstanceRange)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialTyped, occupancyMaterialTypedRange)
        );
        occupancyComputeEmulationResourceUses.push_back(
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(inputs.csgComputeEmulationPlanCaptured){
            occupancyComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgReceiverRanges, occupancyReceiverRange)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgCutters, occupancyCutterRange)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalDepth,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCapNormal,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalData,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            occupancyComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCount,
                inputs.csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse occupancyComputeEmulationResourceSetUses[3u] = {};
        usize occupancyComputeEmulationResourceSetUseCount = 0u;
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyMaterialGeometrySetUse;
        if(inputs.materialSampledTextureSet.valid()){
            occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
                occupancyMaterialSampledTextureSetUse;
        }
        occupancyComputeEmulationResourceSetUses[occupancyComputeEmulationResourceSetUseCount++] =
            occupancyComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint occupancyComputeEmulationScheduling;
        occupancyComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancyComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancyComputeEmulationScheduling.allowPacketMerge = true;
        occupancyComputeEmulationScheduling.mergeWithPrevious = true;
        // Keep producer/raster pair in AVBOIT Pre Graphics packet for one authoritative handoff.
        occupancyComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc occupancyComputeEmulationDesc;
        occupancyComputeEmulationDesc
            .setIdentity(inputs.csgComputeEmulationPlanCaptured
                ? Name("render.avboit.occupancy.csg_compute_emulation")
                : Name("render.avboit.occupancy.compute_emulation"))
            .setMarkerLabel(inputs.csgComputeEmulationPlanCaptured
                ? "AVBOIT Occupancy CSG Compute Emulation"
                : "AVBOIT Occupancy Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(occupancyComputeEmulationScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(
                occupancyComputeEmulationResourceUses.data(),
                occupancyComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                occupancyComputeEmulationResourceSetUses,
                occupancyComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask = m_graph.addTask<
            AvboitOccupancyComputeEmulationGraphTask
        >(
            occupancyComputeEmulationDesc,
            Move(computeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Occupancy compute-emulation producer"
            ));
            return false;
        }
        occupancyDependency = m_avboitSystem.taskGraphStage().m_occupancyComputeEmulationTask;
        avboitOccupancyScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    DeferredFrameTargets& deferredTargets = *inputs.targets;
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket = *inputs.preTimingTicket;
    Optional<Core::GpuTimingMeasure>& avboitOccupancyComputeEmulationTiming = *inputs.occupancyComputeEmulationTiming;
    static_cast<void>(frameBindings);
    const ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& occupancySharedComputeEmulationPlan = inputs.sharedComputeEmulationPlan;
    const usize occupancySharedComputeEmulationInstanceCount = inputs.sharedComputeEmulationInstanceCount;
    const usize occupancySharedComputeEmulationMaterialTypedByteCount = inputs.sharedComputeEmulationMaterialTypedByteCount;
    const Core::GpuGraphResourceId meshView = inputs.meshView;
    const Core::GpuGraphResourceId materialInstances = inputs.materialInstances;
    const Core::GpuGraphResourceId materialTyped = inputs.materialTyped;
    const Core::GpuGraphResourceId currentBindlessSlots = inputs.currentBindlessSlots;

    if(occupancySharedComputeEmulationOutputStatesGraphOwned){
        // Retained output appears in every phase; keep it exact to preserve alternating uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedGenerateResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedGenerateResourceUses.reserve(5u);
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(ReadBufferUse(materialInstances, occupancyInstanceRange));
        occupancySharedGenerateResourceUses.push_back(ReadBufferUse(materialTyped, occupancyMaterialTypedRange));
        occupancySharedGenerateResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        occupancySharedGenerateResourceUses.push_back(WriteUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> occupancySharedRasterResourceUses{
            avboitPreResourceScratch
        };
        occupancySharedRasterResourceUses.assign(
            avboitPreResourceUses.begin(),
            avboitPreResourceUses.end()
        );
        occupancySharedRasterResourceUses.push_back(ReadUse(
            occupancySharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint occupancySharedComputeEmulationScheduling;
        occupancySharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        occupancySharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        occupancySharedComputeEmulationScheduling.allowPacketMerge = true;
        occupancySharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Keep the full alternating chain in AVBOIT Pre so one list owns timing and handoff.
        occupancySharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addOccupancySharedComputeEmulationPhase = [
            this,
            &deferredTargets,
            &occupancySharedComputeEmulationPlan,
            &avboitOccupancyComputeEmulationTiming,
            &frameBindings,
            occupancySharedComputeEmulationInstanceCount,
            occupancySharedComputeEmulationMaterialTypedByteCount,
            occupancyStreamsUploaded = occupancyPayload.occupancyStreamsUploaded,
            occupancyMaterialFrameStatesGraphOwned = occupancyPayload.occupancyMaterialFrameStatesGraphOwned,
            occupancyMaterialGeometryStatesGraphOwned = occupancyPayload.occupancyMaterialGeometryStatesGraphOwned,
            &avboitPreTimingTicket,
            &occupancySharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitOccupancySharedComputeEmulationGraphTask::Phase phase,
            const usize drawIndex,
            const bool beginTiming,
            const bool finishTiming,
            const Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
            const Core::GpuTaskResourceSetUse* const resourceSetUses,
            const usize resourceSetUseCount
        ){
            Core::GpuTaskDesc desc;
            desc
                .setIdentity(identity)
                .setMarkerLabel(markerLabel)
                .setQueue(GraphicsComputeQueueRequest())
                .setScheduling(occupancySharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitOccupancySharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.graphics = &m_graphics;
            payload.materialSystem = &m_materialSystem;
            payload.targets = &deferredTargets;
            payload.timingTicket = &avboitPreTimingTicket;
            payload.occupancyTiming = &avboitOccupancyComputeEmulationTiming;
            payload.plan = occupancySharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = occupancySharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = occupancySharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = occupancyStreamsUploaded;
            payload.materialFrameStatesGraphOwned = occupancyMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = occupancyMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_graph.addTask<AvboitOccupancySharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using OccupancySharedPhase = AvboitOccupancySharedComputeEmulationGraphTask::Phase;
        const Name occupancySharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.occupancy.shared_compute_emulation_generate_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_a"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_b"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_c"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_d"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_d"),
            Name("render.avboit.occupancy.shared_compute_emulation_generate_e"),
            Name("render.avboit.occupancy.shared_compute_emulation_raster_e"),
        };
        const AStringView occupancySharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Occupancy Shared Compute Emulation Generate A",
            "AVBOIT Occupancy Shared Compute Emulation Raster A",
            "AVBOIT Occupancy Shared Compute Emulation Generate B",
            "AVBOIT Occupancy Shared Compute Emulation Raster B",
            "AVBOIT Occupancy Shared Compute Emulation Generate C",
            "AVBOIT Occupancy Shared Compute Emulation Raster C",
            "AVBOIT Occupancy Shared Compute Emulation Generate D",
            "AVBOIT Occupancy Shared Compute Emulation Raster D",
            "AVBOIT Occupancy Shared Compute Emulation Generate E",
            "AVBOIT Occupancy Shared Compute Emulation Raster E",
        };
        const usize occupancySharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                occupancySharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            occupancySharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(
            occupancySharedComputeEmulationPhaseCount
            <= LengthOf(occupancySharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId occupancySharedComputeEmulationDependency = occupancyDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < occupancySharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex] =
                addOccupancySharedComputeEmulationPhase(
                    occupancySharedComputeEmulationPhaseIdentities[phaseIndex],
                    occupancySharedComputeEmulationPhaseMarkers[phaseIndex],
                    occupancySharedComputeEmulationDependency,
                    isRasterPhase ? OccupancySharedPhase::Raster : OccupancySharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == occupancySharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? occupancySharedRasterResourceUses
                        : occupancySharedGenerateResourceUses,
                    occupancyMaterialResourceSetUses,
                    occupancyMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Occupancy shared compute-emulation phase"
                ));
                return false;
            }
            occupancySharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_occupancySharedComputeEmulationTaskCount =
            occupancySharedComputeEmulationPhaseCount;
        // Terminal raster stays the Occupancy endpoint for warp, timing, cache, and tokens.
        m_avboitSystem.taskGraphStage().m_occupancyTask = occupancySharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc avboitOccupancyDesc;
        avboitOccupancyDesc
            .setIdentity(Name("render.avboit.pre"))
            .setMarkerLabel("AVBOIT Pre")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitOccupancyScheduling)
            .setDependencies(&occupancyDependency, 1u)
            .setResourceUses(avboitPreResourceUses.data(), avboitPreResourceUses.size())
            .setResourceSetUses(
                occupancyMaterialResourceSetUseCount != 0u ? occupancyMaterialResourceSetUses : nullptr,
                occupancyMaterialResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_occupancyTask = m_graph.addTask<AvboitOccupancyGraphTask>(
            avboitOccupancyDesc,
            Move(occupancyPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_occupancyTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT occupancy graph task"));
            return false;
        }
    }

    outResult.occupancyTask = m_avboitSystem.taskGraphStage().m_occupancyTask;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
