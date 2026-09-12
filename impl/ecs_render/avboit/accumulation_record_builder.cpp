// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/accumulation_record_builder.h>


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


AvboitAccumulationRecordBuilder::AvboitAccumulationRecordBuilder(
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


[[nodiscard]] bool AvboitAccumulationRecordBuilder::declare(
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    AvboitAccumulationRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitAccumulationGraphTask::Payload& accumulationPayload,
    RendererTaskGraphDetail::AvboitAccumulationComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitAccumulationRecordResult& outResult
){
    using namespace RendererTaskGraphDetail;
    static_cast<void>(csgResources);
    outResult = AvboitAccumulationRecordResult{};
    if(!inputs.targets)
        return false;
    if(!inputs.accumulationTimingTicket)
        return false;
    if(!inputs.accumulationComputeEmulationTiming)
        return false;
    if(!inputs.integrationTask.valid() || !inputs.uploadTask.valid())
        return false;


    const bool accumulationCsgIntervalSampleImageStatesGraphOwned =
        inputs.intervalOutputsGraphOwned && inputs.csgStreamsUploaded
    ;
    const bool accumulationCsgClipBufferStatesGraphOwned = inputs.csgStreamsUploaded;
    NWB_ASSERT(
        !accumulationCsgIntervalSampleImageStatesGraphOwned
        || (
            accumulationPayload.accumulationPhasePrepared
            && accumulationPayload.accumulationSnapshot.captured
        )
    );
    NWB_ASSERT(
        !accumulationCsgClipBufferStatesGraphOwned
        || (
            accumulationPayload.accumulationPhasePrepared
            && accumulationPayload.accumulationSnapshot.captured
        )
    );
    accumulationPayload.accumulationCsgIntervalSampleImageStatesGraphOwned =
        accumulationCsgIntervalSampleImageStatesGraphOwned
    ;
    accumulationPayload.accumulationCsgClipBufferStatesGraphOwned =
        accumulationCsgClipBufferStatesGraphOwned
    ;
    accumulationPayload.accumulationMaterialFrameStatesGraphOwned = inputs.streamsUploaded;
    accumulationPayload.accumulationMaterialGeometryStatesGraphOwned =
        inputs.streamsUploaded
        && accumulationPayload.accumulationMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId accumulationComputeEmulationOutputSet;
    Core::Alloc::ScratchArena accumulationComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool accumulationComputeEmulationPlanCaptured =
        inputs.regularComputeEmulationPlanCaptured
        || inputs.csgComputeEmulationPlanCaptured
    ;
    bool accumulationComputeEmulationOutputStatesGraphOwned = false;
    if(inputs.regularComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned = GatherImportedOutputBufferResourceSet(
            m_graph,
            computeEmulationPayload.plan,
            accumulationComputeEmulationResourceScratch,
            Name("render.avboit.accumulation.compute_emulation.outputs"),
            "AVBOIT Accumulation Compute Emulation Outputs",
            accumulationComputeEmulationOutputSet
        );
    }
    else if(inputs.csgComputeEmulationPlanCaptured){
        accumulationComputeEmulationOutputStatesGraphOwned =
            GatherImportedOutputBufferResourceSet(
                m_graph,
                computeEmulationPayload.csgPlan,
                accumulationComputeEmulationResourceScratch,
                Name("render.avboit.accumulation.csg_compute_emulation.outputs"),
                "AVBOIT Accumulation CSG Compute Emulation Outputs",
                accumulationComputeEmulationOutputSet
            )
        ;
    }
    if(
        accumulationComputeEmulationPlanCaptured
        && !accumulationComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation compute-emulation output states"
        ));
    }
    accumulationPayload.accumulationComputeEmulationOutputStatesGraphOwned =
        inputs.regularComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    accumulationPayload.accumulationCsgComputeEmulationOutputStatesGraphOwned =
        inputs.csgComputeEmulationPlanCaptured
        && accumulationComputeEmulationOutputStatesGraphOwned
    ;
    accumulationPayload.accumulationComputeEmulationTiming =
        accumulationComputeEmulationOutputStatesGraphOwned
            ? inputs.accumulationComputeEmulationTiming
            : nullptr
    ;
    Core::GpuGraphResourceId accumulationSharedComputeEmulationOutput;
    const bool accumulationSharedComputeEmulationOutputStatesGraphOwned =
        inputs.sharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_graph,
            inputs.sharedComputeEmulationPlan,
            "AVBOIT Accumulation Shared Compute Emulation Output",
            accumulationSharedComputeEmulationOutput
        )
    ;
    if(
        inputs.sharedComputeEmulationPlanCaptured
        && !accumulationSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Accumulation shared compute-emulation output state"
        ));
    }

    const Core::BufferRange instanceRangeValue(
        0u,
        accumulationPayload.accumulationSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange materialTypedRangeValue(
        0u,
        accumulationPayload.accumulationSnapshot.materialTypedByteCount
    );
    const Core::BufferRange receiverRangeValue(
        0u,
        accumulationPayload.accumulationSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange cutterRangeValue(
        0u,
        accumulationPayload.accumulationSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );
    inputs.instanceRange = instanceRangeValue;
    inputs.materialTypedRange = materialTypedRangeValue;
    inputs.receiverRange = receiverRangeValue;
    inputs.cutterRange = cutterRangeValue;

    Core::Alloc::ScratchArena accumulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationResourceUses{ accumulationResourceScratch };
    accumulationResourceUses.reserve(
        12u
        + (inputs.streamsUploaded ? 7u : 0u)
        + (accumulationCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    accumulationResourceUses.push_back(ReadUse(inputs.albedo));
    accumulationResourceUses.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    accumulationResourceUses.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));


    // accumulationFramebuffer binds inputs.depth read-only, tracked as DepthRead.
    accumulationResourceUses.push_back(ReadUse(inputs.depth, Core::ResourceStates::DepthRead));
    accumulationResourceUses.push_back(ReadUse(inputs.avboitTransmittance));
    accumulationResourceUses.push_back(ReadUse(inputs.avboitDepthWarp));
    accumulationResourceUses.push_back(ReadUse(inputs.avboitControl));
    accumulationResourceUses.push_back(ReadUse(inputs.refractionInstance));
    accumulationResourceUses.push_back(ReadUse(inputs.refractionDepth));
    accumulationResourceUses.push_back(ReadWriteUse(inputs.avboitForegroundColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(inputs.avboitForegroundExtinction, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(inputs.avboitAccumColor, Core::ResourceStates::RenderTarget));
    accumulationResourceUses.push_back(ReadWriteUse(inputs.avboitAccumExtinction, Core::ResourceStates::RenderTarget));
    if(inputs.streamsUploaded){
        accumulationResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        accumulationResourceUses.push_back(ReadBufferUse(inputs.materialInstances, inputs.instanceRange));
        accumulationResourceUses.push_back(ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange));
        if(inputs.csgStreamsUploaded){
            accumulationResourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, inputs.receiverRange));
            accumulationResourceUses.push_back(ReadBufferUse(inputs.csgCutters, inputs.cutterRange));
            accumulationResourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // Interval producer owns this state; accumulation only samples it.
            accumulationResourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(accumulationCsgIntervalSampleImageStatesGraphOwned){
                // Interval producer wrote these aliases; graph lowers the same-UAV handoff.
                accumulationResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalDepth,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalCapNormal,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalData,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                accumulationResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalCount,
                    inputs.csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse accumulationMaterialGeometrySetUse{
        .resourceSet = inputs.materialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationMaterialSampledTextureSetUse{
        .resourceSet = inputs.materialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputUavSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse accumulationComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = accumulationComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse accumulationMaterialResourceSetUses[3u] = {};
    usize accumulationMaterialResourceSetUseCount = 0u;
    if(accumulationPayload.accumulationMaterialGeometryStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
    }
    if(inputs.materialSampledTextureSet.valid()){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationMaterialSampledTextureSetUse;
    }
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        accumulationMaterialResourceSetUses[accumulationMaterialResourceSetUseCount++] =
            accumulationComputeEmulationOutputVertexBufferSetUse;
    }
    accumulationResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    accumulationResourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    accumulationResourceUses.push_back(ReadUse(inputs.avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitAccumulationScheduling;
    avboitAccumulationScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitAccumulationScheduling.forceSubmissionBoundary = false;
    avboitAccumulationScheduling.allowPacketMerge = true;
    avboitAccumulationScheduling.mergeWithPrevious = true;
    avboitAccumulationScheduling.allowMergeAcrossConsumerFrontier = true;

    // Keep the final upload as stream anchor; replacing it hides a broken handoff.
    const Core::GpuTaskId accumulationStreamTask = inputs.uploadTask;
    if(inputs.streamsUploaded)
        m_avboitSystem.taskGraphStage().m_accumulationStreamTask = accumulationStreamTask;
    Core::GpuTaskId accumulationDependency = inputs.uploadTask;
    if(accumulationComputeEmulationOutputStatesGraphOwned){
        computeEmulationPayload.graphics = &m_graphics;
        computeEmulationPayload.materialSystem = &m_materialSystem;
        computeEmulationPayload.targets = inputs.targets;
        computeEmulationPayload.timingTicket = accumulationPayload.timingTicket;
        computeEmulationPayload.accumulationTiming = inputs.accumulationComputeEmulationTiming;
        computeEmulationPayload.materialDrawBuffersUploaded = inputs.streamsUploaded;
        computeEmulationPayload.csgFrameBuffersUploaded = inputs.csgStreamsUploaded;
        computeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            accumulationCsgIntervalSampleImageStatesGraphOwned;
        computeEmulationPayload.csgClipBufferStatesGraphOwned =
            accumulationCsgClipBufferStatesGraphOwned;
        computeEmulationPayload.materialFrameStatesGraphOwned =
            accumulationPayload.accumulationMaterialFrameStatesGraphOwned;
        computeEmulationPayload.materialGeometryStatesGraphOwned =
            accumulationPayload.accumulationMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationComputeEmulationResourceUses{
            accumulationResourceScratch
        };
        accumulationComputeEmulationResourceUses.reserve(
            4u + (inputs.csgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        accumulationComputeEmulationResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        accumulationComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialInstances, inputs.instanceRange)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange)
        );
        accumulationComputeEmulationResourceUses.push_back(
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(inputs.csgComputeEmulationPlanCaptured){
            accumulationComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgReceiverRanges, inputs.receiverRange)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgCutters, inputs.cutterRange)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalDepth,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCapNormal,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalData,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            accumulationComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCount,
                inputs.csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse accumulationComputeEmulationResourceSetUses[3u] = {};
        usize accumulationComputeEmulationResourceSetUseCount = 0u;
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationMaterialGeometrySetUse;
        if(inputs.materialSampledTextureSet.valid()){
            accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
                accumulationMaterialSampledTextureSetUse;
        }
        accumulationComputeEmulationResourceSetUses[accumulationComputeEmulationResourceSetUseCount++] =
            accumulationComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint accumulationComputeEmulationScheduling;
        accumulationComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationComputeEmulationScheduling.allowPacketMerge = true;
        accumulationComputeEmulationScheduling.mergeWithPrevious = true;
        // Next raster consumes the producer UAV output and shares its timing ticket.
        accumulationComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc accumulationComputeEmulationDesc;
        accumulationComputeEmulationDesc
            .setIdentity(inputs.csgComputeEmulationPlanCaptured
                ? Name("render.avboit.accumulation.csg_compute_emulation")
                : Name("render.avboit.accumulation.compute_emulation"))
            .setMarkerLabel(inputs.csgComputeEmulationPlanCaptured
                ? "AVBOIT Accumulation CSG Compute Emulation"
                : "AVBOIT Accumulation Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(accumulationComputeEmulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(
                accumulationComputeEmulationResourceUses.data(),
                accumulationComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                accumulationComputeEmulationResourceSetUses,
                accumulationComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask = m_graph.addTask<
            AvboitAccumulationComputeEmulationGraphTask
        >(
            accumulationComputeEmulationDesc,
            Move(computeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Accumulation compute-emulation producer"
            ));
            return false;
        }
        accumulationDependency = m_avboitSystem.taskGraphStage().m_accumulationComputeEmulationTask;
        avboitAccumulationScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(accumulationSharedComputeEmulationOutputStatesGraphOwned){
        // Retained output appears in every phase; keep it exact to preserve alternating uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedGenerateResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedGenerateResourceUses.reserve(5u);
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            inputs.meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(ReadBufferUse(inputs.materialInstances, inputs.instanceRange));
        accumulationSharedGenerateResourceUses.push_back(ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange));
        accumulationSharedGenerateResourceUses.push_back(ReadUse(
            inputs.currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        accumulationSharedGenerateResourceUses.push_back(WriteUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> accumulationSharedRasterResourceUses{
            accumulationResourceScratch
        };
        accumulationSharedRasterResourceUses.assign(
            accumulationResourceUses.begin(),
            accumulationResourceUses.end()
        );
        accumulationSharedRasterResourceUses.push_back(ReadUse(
            accumulationSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint accumulationSharedComputeEmulationScheduling;
        accumulationSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        accumulationSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        accumulationSharedComputeEmulationScheduling.allowPacketMerge = true;
        accumulationSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Keep the full alternating chain in AVBOIT Pre so one list owns timing and handoff.
        accumulationSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        const auto addAccumulationSharedComputeEmulationPhase = [
            this,
            &frameBindings,
            &inputs,
            accumulationMaterialFrameStatesGraphOwned = accumulationPayload.accumulationMaterialFrameStatesGraphOwned,
            accumulationMaterialGeometryStatesGraphOwned = accumulationPayload.accumulationMaterialGeometryStatesGraphOwned,
            &accumulationSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitAccumulationSharedComputeEmulationGraphTask::Phase phase,
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
                .setScheduling(accumulationSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitAccumulationSharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.graphics = &m_graphics;
            payload.materialSystem = &m_materialSystem;
            payload.targets = inputs.targets;
            payload.timingTicket = inputs.accumulationTimingTicket;
            payload.accumulationTiming = inputs.accumulationComputeEmulationTiming;
            payload.plan = inputs.sharedComputeEmulationPlan;
            payload.drawIndex = drawIndex;
            payload.instanceCount = inputs.sharedComputeEmulationInstanceCount;
            payload.materialTypedByteCount = inputs.sharedComputeEmulationMaterialTypedByteCount;
            payload.materialDrawBuffersUploaded = inputs.streamsUploaded;
            payload.materialFrameStatesGraphOwned = accumulationMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = accumulationMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return m_graph.addTask<AvboitAccumulationSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using AccumulationSharedPhase = AvboitAccumulationSharedComputeEmulationGraphTask::Phase;
        const Name accumulationSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.accumulation.shared_compute_emulation_generate_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_a"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_b"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_c"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_d"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_d"),
            Name("render.avboit.accumulation.shared_compute_emulation_generate_e"),
            Name("render.avboit.accumulation.shared_compute_emulation_raster_e"),
        };
        const AStringView accumulationSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Accumulation Shared Compute Emulation Generate A",
            "AVBOIT Accumulation Shared Compute Emulation Raster A",
            "AVBOIT Accumulation Shared Compute Emulation Generate B",
            "AVBOIT Accumulation Shared Compute Emulation Raster B",
            "AVBOIT Accumulation Shared Compute Emulation Generate C",
            "AVBOIT Accumulation Shared Compute Emulation Raster C",
            "AVBOIT Accumulation Shared Compute Emulation Generate D",
            "AVBOIT Accumulation Shared Compute Emulation Raster D",
            "AVBOIT Accumulation Shared Compute Emulation Generate E",
            "AVBOIT Accumulation Shared Compute Emulation Raster E",
        };
        const usize accumulationSharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                inputs.sharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            inputs.sharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(
            accumulationSharedComputeEmulationPhaseCount
            <= LengthOf(accumulationSharedComputeEmulationPhaseIdentities)
        );
        Core::GpuTaskId accumulationSharedComputeEmulationDependency = accumulationDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < accumulationSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex] =
                addAccumulationSharedComputeEmulationPhase(
                    accumulationSharedComputeEmulationPhaseIdentities[phaseIndex],
                    accumulationSharedComputeEmulationPhaseMarkers[phaseIndex],
                    accumulationSharedComputeEmulationDependency,
                    isRasterPhase ? AccumulationSharedPhase::Raster : AccumulationSharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == accumulationSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? accumulationSharedRasterResourceUses
                        : accumulationSharedGenerateResourceUses,
                    accumulationMaterialResourceSetUses,
                    accumulationMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Accumulation shared compute-emulation phase"
                ));
                return false;
            }
            accumulationSharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_accumulationSharedComputeEmulationTaskCount =
            accumulationSharedComputeEmulationPhaseCount;
        // Terminal raster is the Accumulation endpoint feeding finalizer, timing, and tokens.
        m_avboitSystem.taskGraphStage().m_accumulationTask = accumulationSharedComputeEmulationDependency;
    }
    else{
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(avboitAccumulationScheduling)
            .setDependencies(&accumulationDependency, 1u)
            .setResourceUses(accumulationResourceUses.data(), accumulationResourceUses.size())
            .setResourceSetUses(
                accumulationMaterialResourceSetUseCount != 0u ? accumulationMaterialResourceSetUses : nullptr,
                accumulationMaterialResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_accumulationTask = m_graph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            Move(accumulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_accumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
            return false;
        }
    }
    const Core::GpuTaskResourceUse accumulationFinalizeResourceUses[] = {
        ReadUse(inputs.avboitAccumColor, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.avboitForegroundColor),
        ReadUse(inputs.avboitForegroundExtinction),
        ReadUse(inputs.avboitAccumExtinction, Core::ResourceStates::ShaderResource),
        ReadUse(inputs.depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint accumulationFinalizeScheduling;
    accumulationFinalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    accumulationFinalizeScheduling.forceSubmissionBoundary = false;
    accumulationFinalizeScheduling.allowPacketMerge = true;
    accumulationFinalizeScheduling.mergeWithPrevious = true;
    // Finalizer is Accumulation's tail; retain its timing/acceptance packet before Lighting.
    accumulationFinalizeScheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc accumulationFinalizeDesc;
    accumulationFinalizeDesc
        .setIdentity(Name("render.avboit.accumulation_finalize"))
        .setMarkerLabel("AVBOIT Accumulation Finalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(accumulationFinalizeScheduling)
        .setDependencies(&m_avboitSystem.taskGraphStage().m_accumulationTask, 1u)
        .setResourceUses(accumulationFinalizeResourceUses, LengthOf(accumulationFinalizeResourceUses))
    ;
    m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask = m_graph.addTask<AvboitAccumulationFinalizeGraphTask>(
        accumulationFinalizeDesc,
        AvboitAccumulationFinalizeGraphTask::Payload{}
    );
    if(!m_avboitSystem.taskGraphStage().m_accumulationFinalizeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation finalizer graph task"));
        return false;
    }

    outResult.accumulationTask = m_avboitSystem.taskGraphStage().m_accumulationTask;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
