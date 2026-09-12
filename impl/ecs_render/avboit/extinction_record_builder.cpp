// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/extinction_record_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/avboit/avboit_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitExtinctionRecordBuilder::AvboitExtinctionRecordBuilder(
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


[[nodiscard]] bool AvboitExtinctionRecordBuilder::declare(
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    AvboitExtinctionRecordInputs& inputs,
    RendererTaskGraphDetail::AvboitExtinctionGraphTask::Payload& extinctionPayload,
    RendererTaskGraphDetail::AvboitExtinctionComputeEmulationGraphTask::Payload& computeEmulationPayload,
    AvboitExtinctionRecordResult& outResult
){
    using namespace RendererTaskGraphDetail;
    static_cast<void>(csgResources);
    outResult = AvboitExtinctionRecordResult{};
    if(!inputs.targets)
        return false;
    if(!inputs.extinctionTimingTicket)
        return false;
    if(!inputs.extinctionComputeEmulationTiming)
        return false;
    if(!inputs.depthWarpCompletionTask.valid() || !inputs.uploadTask.valid())
        return false;

    const bool extinctionCsgIntervalSampleImageStatesGraphOwned =
        inputs.intervalOutputsGraphOwned && inputs.csgStreamsUploaded
    ;
    const bool extinctionCsgClipBufferStatesGraphOwned = inputs.csgStreamsUploaded;
    NWB_ASSERT(
        !extinctionCsgIntervalSampleImageStatesGraphOwned
        || (
            extinctionPayload.extinctionPhasePrepared
            && extinctionPayload.extinctionSnapshot.captured
        )
    );
    NWB_ASSERT(
        !extinctionCsgClipBufferStatesGraphOwned
        || (
            extinctionPayload.extinctionPhasePrepared
            && extinctionPayload.extinctionSnapshot.captured
        )
    );
    extinctionPayload.extinctionCsgIntervalSampleImageStatesGraphOwned =
        extinctionCsgIntervalSampleImageStatesGraphOwned
    ;
    extinctionPayload.extinctionCsgClipBufferStatesGraphOwned =
        extinctionCsgClipBufferStatesGraphOwned
    ;
    extinctionPayload.extinctionMaterialFrameStatesGraphOwned = inputs.streamsUploaded;
    extinctionPayload.extinctionMaterialGeometryStatesGraphOwned =
        inputs.streamsUploaded
        && extinctionPayload.extinctionMaterialGeometryStatesGraphOwned
    ;
    Core::GpuGraphResourceSetId extinctionComputeEmulationOutputSet;
    Core::Alloc::ScratchArena extinctionComputeEmulationResourceScratch(RendererArenaScope::s_TaskGraphArena);
    const bool extinctionComputeEmulationPlanCaptured =
        inputs.regularComputeEmulationPlanCaptured
        || inputs.csgComputeEmulationPlanCaptured
    ;
    bool extinctionComputeEmulationOutputStatesGraphOwned = false;
    if(inputs.regularComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned = GatherImportedOutputBufferResourceSet(
            m_graph,
            computeEmulationPayload.plan,
            extinctionComputeEmulationResourceScratch,
            Name("render.avboit.extinction.compute_emulation.outputs"),
            "AVBOIT Extinction Compute Emulation Outputs",
            extinctionComputeEmulationOutputSet
        );
    }
    else if(inputs.csgComputeEmulationPlanCaptured){
        extinctionComputeEmulationOutputStatesGraphOwned =
            GatherImportedOutputBufferResourceSet(
                m_graph,
                computeEmulationPayload.csgPlan,
                extinctionComputeEmulationResourceScratch,
                Name("render.avboit.extinction.csg_compute_emulation.outputs"),
                "AVBOIT Extinction CSG Compute Emulation Outputs",
                extinctionComputeEmulationOutputSet
            )
        ;
    }
    if(
        extinctionComputeEmulationPlanCaptured
        && !extinctionComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction compute-emulation output states"
        ));
    }
    Core::GpuGraphResourceId extinctionSharedComputeEmulationOutput;
    const bool extinctionSharedComputeEmulationOutputStatesGraphOwned =
        inputs.sharedComputeEmulationPlanCaptured
        && GatherRegularSharedComputeEmulationResource(
            m_graph,
            inputs.sharedComputeEmulationPlan,
            "AVBOIT Extinction Shared Compute Emulation Output",
            extinctionSharedComputeEmulationOutput
        )
    ;
    if(
        inputs.sharedComputeEmulationPlanCaptured
        && !extinctionSharedComputeEmulationOutputStatesGraphOwned
    ){
        NWB_LOGGER_WARNING(NWB_TEXT(
            "RendererSystem: could not declare graph-owned AVBOIT Extinction shared compute-emulation output"
        ));
    }
    extinctionPayload.extinctionComputeEmulationOutputStatesGraphOwned =
        inputs.regularComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    extinctionPayload.extinctionCsgComputeEmulationOutputStatesGraphOwned =
        inputs.csgComputeEmulationPlanCaptured
        && extinctionComputeEmulationOutputStatesGraphOwned
    ;
    extinctionPayload.extinctionComputeEmulationTiming =
        extinctionComputeEmulationOutputStatesGraphOwned
            ? inputs.extinctionComputeEmulationTiming
            : nullptr
    ;
    const Core::BufferRange extinctionInstanceRange(
        0u,
        extinctionPayload.extinctionSnapshot.instanceCount * sizeof(InstanceGpuData)
    );
    const Core::BufferRange extinctionMaterialTypedRange(0u, extinctionPayload.extinctionSnapshot.materialTypedByteCount);
    const Core::BufferRange extinctionReceiverRange(
        0u,
        extinctionPayload.extinctionSnapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData)
    );
    const Core::BufferRange extinctionCutterRange(
        0u,
        extinctionPayload.extinctionSnapshot.csgCutters.size() * sizeof(CsgCutterGpuData)
    );

    Core::Alloc::ScratchArena extinctionResourceScratch(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionResourceUses{ extinctionResourceScratch };
    extinctionResourceUses.reserve(
        16u
        + (inputs.streamsUploaded ? 7u : 0u)
        + (extinctionCsgIntervalSampleImageStatesGraphOwned ? 4u : 0u)
    );
    // Keep the full raster contract on every route so crossings retain hazards and lowering.
    extinctionResourceUses.push_back(ReadUse(inputs.albedo));
    extinctionResourceUses.push_back(ReadUse(inputs.refractionInstance));
    extinctionResourceUses.push_back(ReadUse(inputs.normal, Core::ResourceStates::ShaderResource));
    extinctionResourceUses.push_back(ReadUse(inputs.worldPosition, Core::ResourceStates::ShaderResource));
    extinctionResourceUses.push_back(ReadUse(inputs.depth));
    extinctionResourceUses.push_back(ReadWriteUse(inputs.avboitLowRaster, Core::ResourceStates::RenderTarget));
    extinctionResourceUses.push_back(ReadUse(inputs.avboitDepthWarp));
    extinctionResourceUses.push_back(ReadUse(inputs.avboitControl));
    extinctionResourceUses.push_back(ReadWriteUse(inputs.avboitExtinction, Core::ResourceStates::UnorderedAccess));
    extinctionResourceUses.push_back(ReadWriteUse(inputs.avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess));
    if(inputs.streamsUploaded){
        extinctionResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        extinctionResourceUses.push_back(ReadBufferUse(inputs.materialInstances, extinctionInstanceRange));
        extinctionResourceUses.push_back(ReadBufferUse(inputs.materialTyped, extinctionMaterialTypedRange));
        if(inputs.csgStreamsUploaded){
            extinctionResourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, extinctionReceiverRange));
            extinctionResourceUses.push_back(ReadBufferUse(inputs.csgCutters, extinctionCutterRange));
            extinctionResourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            // Interval producer owns this sample state through all low-raster phases.
            extinctionResourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
            if(extinctionCsgIntervalSampleImageStatesGraphOwned){
                // Interval producer wrote these aliases; graph lowers the same-UAV handoff.
                extinctionResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalDepth,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalCapNormal,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalData,
                    inputs.csgRemovedIntervalSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
                extinctionResourceUses.push_back(ReadTextureUse(
                    inputs.csgRemovedIntervalCount,
                    inputs.csgRemovedIntervalCountSubresources,
                    Core::ResourceStates::UnorderedAccess
                ));
            }
        }
    }
    const Core::GpuTaskResourceSetUse extinctionMaterialGeometrySetUse{
        .resourceSet = inputs.materialGeometrySet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionMaterialSampledTextureSetUse{
        .resourceSet = inputs.materialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputUavSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse extinctionComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = extinctionComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::VertexBuffer,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse extinctionMaterialResourceSetUses[3u] = {};
    usize extinctionMaterialResourceSetUseCount = 0u;
    if(extinctionPayload.extinctionMaterialGeometryStatesGraphOwned)
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialGeometrySetUse;
    if(inputs.materialSampledTextureSet.valid())
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] = extinctionMaterialSampledTextureSetUse;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        extinctionMaterialResourceSetUses[extinctionMaterialResourceSetUseCount++] =
            extinctionComputeEmulationOutputVertexBufferSetUse;
    }
    extinctionResourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    extinctionResourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    extinctionResourceUses.push_back(ReadUse(inputs.avboitCsgDomain));

    Core::GpuTaskSchedulingHint avboitExtinctionScheduling;
    avboitExtinctionScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitExtinctionScheduling.forceSubmissionBoundary = false;
    avboitExtinctionScheduling.allowPacketMerge = true;
    avboitExtinctionScheduling.mergeWithPrevious = true;
    avboitExtinctionScheduling.allowMergeAcrossConsumerFrontier = true;


    // Keep the final upload as stream anchor; replacing it hides a broken producer handoff.
    const Core::GpuTaskId extinctionStreamTask = inputs.uploadTask;
    if(inputs.streamsUploaded)
        m_avboitSystem.taskGraphStage().m_extinctionStreamTask = extinctionStreamTask;
    Core::GpuTaskId extinctionDependency = inputs.uploadTask;
    if(extinctionComputeEmulationOutputStatesGraphOwned){
        computeEmulationPayload.graphics = &m_graphics;
        computeEmulationPayload.materialSystem = &m_materialSystem;
        computeEmulationPayload.targets = inputs.targets;
        computeEmulationPayload.timingTicket = extinctionPayload.timingTicket;
        computeEmulationPayload.extinctionTiming = inputs.extinctionComputeEmulationTiming;
        computeEmulationPayload.instanceCount = extinctionPayload.extinctionSnapshot.instanceCount;
        computeEmulationPayload.materialTypedByteCount = extinctionPayload.extinctionSnapshot.materialTypedByteCount;
        computeEmulationPayload.materialDrawBuffersUploaded = inputs.streamsUploaded;
        computeEmulationPayload.csgFrameBuffersUploaded = inputs.csgStreamsUploaded;
        computeEmulationPayload.csgIntervalSampleImageStatesGraphOwned =
            extinctionCsgIntervalSampleImageStatesGraphOwned;
        computeEmulationPayload.csgClipBufferStatesGraphOwned =
            extinctionCsgClipBufferStatesGraphOwned;
        computeEmulationPayload.materialFrameStatesGraphOwned =
            extinctionPayload.extinctionMaterialFrameStatesGraphOwned;
        computeEmulationPayload.materialGeometryStatesGraphOwned =
            extinctionPayload.extinctionMaterialGeometryStatesGraphOwned;

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionComputeEmulationResourceUses{
            extinctionResourceScratch
        };
        extinctionComputeEmulationResourceUses.reserve(
            4u + (inputs.csgComputeEmulationPlanCaptured ? 8u : 0u)
        );
        extinctionComputeEmulationResourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        extinctionComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialInstances, extinctionInstanceRange)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadBufferUse(inputs.materialTyped, extinctionMaterialTypedRange)
        );
        extinctionComputeEmulationResourceUses.push_back(
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer)
        );
        if(inputs.csgComputeEmulationPlanCaptured){
            extinctionComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgReceiverRanges, extinctionReceiverRange)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadBufferUse(inputs.csgCutters, extinctionCutterRange)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(
                ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer)
            );
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalDepth,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCapNormal,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalData,
                inputs.csgRemovedIntervalSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
            extinctionComputeEmulationResourceUses.push_back(ReadTextureUse(
                inputs.csgRemovedIntervalCount,
                inputs.csgRemovedIntervalCountSubresources,
                Core::ResourceStates::UnorderedAccess
            ));
        }
        Core::GpuTaskResourceSetUse extinctionComputeEmulationResourceSetUses[3u] = {};
        usize extinctionComputeEmulationResourceSetUseCount = 0u;
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionMaterialGeometrySetUse;
        if(inputs.materialSampledTextureSet.valid()){
            extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
                extinctionMaterialSampledTextureSetUse;
        }
        extinctionComputeEmulationResourceSetUses[extinctionComputeEmulationResourceSetUseCount++] =
            extinctionComputeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint extinctionComputeEmulationScheduling;
        extinctionComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionComputeEmulationScheduling.allowPacketMerge = true;
        extinctionComputeEmulationScheduling.mergeWithPrevious = true;
        // Next raster consumes the producer UAV output and shares its timing ticket.
        extinctionComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc extinctionComputeEmulationDesc;
        extinctionComputeEmulationDesc
            .setIdentity(inputs.csgComputeEmulationPlanCaptured
                ? Name("render.avboit.extinction.csg_compute_emulation")
                : Name("render.avboit.extinction.compute_emulation"))
            .setMarkerLabel(inputs.csgComputeEmulationPlanCaptured
                ? "AVBOIT Extinction CSG Compute Emulation"
                : "AVBOIT Extinction Compute Emulation")
            .setQueue(GraphicsComputeQueueRequest())
            .setScheduling(extinctionComputeEmulationScheduling)
            .setDependencies(&extinctionDependency, 1u)
            .setResourceUses(
                extinctionComputeEmulationResourceUses.data(),
                extinctionComputeEmulationResourceUses.size()
            )
            .setResourceSetUses(
                extinctionComputeEmulationResourceSetUses,
                extinctionComputeEmulationResourceSetUseCount
            )
        ;
        m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask = m_graph.addTask<
            AvboitExtinctionComputeEmulationGraphTask
        >(
            extinctionComputeEmulationDesc,
            Move(computeEmulationPayload)
        );
        if(!m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT(
                "RendererSystem: could not declare AVBOIT Extinction compute-emulation producer"
            ));
            return false;
        }
        extinctionDependency = m_avboitSystem.taskGraphStage().m_extinctionComputeEmulationTask;
        avboitExtinctionScheduling.allowMergeAcrossConsumerFrontier = true;
    }
    if(extinctionSharedComputeEmulationOutputStatesGraphOwned){
        // Keep the retained output concrete so the compiler preserves alternating uses.
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedGenerateResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedGenerateResourceUses.reserve(5u);
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            inputs.meshView,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(ReadBufferUse(inputs.materialInstances, extinctionInstanceRange));
        extinctionSharedGenerateResourceUses.push_back(ReadBufferUse(inputs.materialTyped, extinctionMaterialTypedRange));
        extinctionSharedGenerateResourceUses.push_back(ReadUse(
            inputs.currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer
        ));
        extinctionSharedGenerateResourceUses.push_back(WriteUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::UnorderedAccess
        ));

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> extinctionSharedRasterResourceUses{
            extinctionResourceScratch
        };
        extinctionSharedRasterResourceUses.assign(
            extinctionResourceUses.begin(),
            extinctionResourceUses.end()
        );
        extinctionSharedRasterResourceUses.push_back(ReadUse(
            extinctionSharedComputeEmulationOutput,
            Core::ResourceStates::VertexBuffer
        ));

        Core::GpuTaskSchedulingHint extinctionSharedComputeEmulationScheduling;
        extinctionSharedComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        extinctionSharedComputeEmulationScheduling.forceSubmissionBoundary = false;
        extinctionSharedComputeEmulationScheduling.allowPacketMerge = true;
        extinctionSharedComputeEmulationScheduling.mergeWithPrevious = true;
        // Integration and Accumulation consume the terminal raster; successors carry dependencies.
        extinctionSharedComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        static_cast<void>(inputs.targets);
        Optional<Core::GpuTimingMeasure>& avboitExtinctionComputeEmulationTiming = *inputs.extinctionComputeEmulationTiming;
        ECSRenderDetail::RegularSharedComputeEmulationGraphPlan& planAlias = inputs.sharedComputeEmulationPlan;
        const usize countAlias = inputs.sharedComputeEmulationInstanceCount;
        const usize bytesAlias = inputs.sharedComputeEmulationMaterialTypedByteCount;
        const bool streamsUploaded = inputs.streamsUploaded;
        Core::GpuTaskGraph* graphAlias = &m_graph;
        DeferredFrameTargets* deferredTargetsPtr = inputs.targets;
        Core::GraphicsRuntime* graphicsAlias = &m_graphics;
        RendererMaterialSystem* materialSystemAlias = &m_materialSystem;
        const auto addExtinctionSharedComputeEmulationPhase = [
            deferredTargetsPtr,
            graphAlias,
            graphicsAlias,
            materialSystemAlias,
            &planAlias,
            &avboitExtinctionComputeEmulationTiming,
            &frameBindings,
            countAlias,
            bytesAlias,
            streamsUploaded,
            extinctionMaterialFrameStatesGraphOwned = extinctionPayload.extinctionMaterialFrameStatesGraphOwned,
            extinctionMaterialGeometryStatesGraphOwned = extinctionPayload.extinctionMaterialGeometryStatesGraphOwned,
            phaseTimingTicket = extinctionPayload.timingTicket,
            &extinctionSharedComputeEmulationScheduling
        ](
            const Name identity,
            const AStringView markerLabel,
            const Core::GpuTaskId& dependency,
            const AvboitExtinctionSharedComputeEmulationGraphTask::Phase phase,
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
                .setScheduling(extinctionSharedComputeEmulationScheduling)
                .setDependencies(&dependency, 1u)
                .setResourceUses(resourceUses.data(), resourceUses.size())
                .setResourceSetUses(resourceSetUses, resourceSetUseCount)
            ;
            AvboitExtinctionSharedComputeEmulationGraphTask::Payload payload;
            payload.frameBindings = frameBindings;
            payload.graphics = graphicsAlias;
            payload.materialSystem = materialSystemAlias;
            payload.targets = deferredTargetsPtr;
            payload.timingTicket = phaseTimingTicket;
            payload.extinctionTiming = &avboitExtinctionComputeEmulationTiming;
            payload.plan = planAlias;
            payload.drawIndex = drawIndex;
            payload.instanceCount = countAlias;
            payload.materialTypedByteCount = bytesAlias;
            payload.materialDrawBuffersUploaded = streamsUploaded;
            payload.materialFrameStatesGraphOwned = extinctionMaterialFrameStatesGraphOwned;
            payload.materialGeometryStatesGraphOwned = extinctionMaterialGeometryStatesGraphOwned;
            payload.beginTiming = beginTiming;
            payload.finishTiming = finishTiming;
            payload.phase = phase;
            return graphAlias->addTask<AvboitExtinctionSharedComputeEmulationGraphTask>(
                desc,
                Move(payload)
            );
        };
        using ExtinctionSharedPhase = AvboitExtinctionSharedComputeEmulationGraphTask::Phase;
        const Name extinctionSharedComputeEmulationPhaseIdentities[] = {
            Name("render.avboit.extinction.shared_compute_emulation_generate_a"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_a"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_b"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_b"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_c"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_c"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_d"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_d"),
            Name("render.avboit.extinction.shared_compute_emulation_generate_e"),
            Name("render.avboit.extinction.shared_compute_emulation_raster_e"),
        };
        const AStringView extinctionSharedComputeEmulationPhaseMarkers[] = {
            "AVBOIT Extinction Shared Compute Emulation Generate A",
            "AVBOIT Extinction Shared Compute Emulation Raster A",
            "AVBOIT Extinction Shared Compute Emulation Generate B",
            "AVBOIT Extinction Shared Compute Emulation Raster B",
            "AVBOIT Extinction Shared Compute Emulation Generate C",
            "AVBOIT Extinction Shared Compute Emulation Raster C",
            "AVBOIT Extinction Shared Compute Emulation Generate D",
            "AVBOIT Extinction Shared Compute Emulation Raster D",
            "AVBOIT Extinction Shared Compute Emulation Generate E",
            "AVBOIT Extinction Shared Compute Emulation Raster E",
        };
        const usize extinctionSharedComputeEmulationPhaseCount =
            ECSRenderDetail::SharedComputeEmulationPhaseCountForDrawCount(
                inputs.sharedComputeEmulationPlan.drawCount
            )
        ;
        NWB_ASSERT(ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            inputs.sharedComputeEmulationPlan.drawCount
        ));
        NWB_ASSERT(extinctionSharedComputeEmulationPhaseCount <= LengthOf(extinctionSharedComputeEmulationPhaseIdentities));
        Core::GpuTaskId extinctionSharedComputeEmulationDependency = extinctionDependency;
        for(usize phaseIndex = 0u;
            phaseIndex < extinctionSharedComputeEmulationPhaseCount;
            ++phaseIndex
        ){
            const bool isRasterPhase =
                phaseIndex % ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw != 0u;
            m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex] =
                addExtinctionSharedComputeEmulationPhase(
                    extinctionSharedComputeEmulationPhaseIdentities[phaseIndex],
                    extinctionSharedComputeEmulationPhaseMarkers[phaseIndex],
                    extinctionSharedComputeEmulationDependency,
                    isRasterPhase ? ExtinctionSharedPhase::Raster : ExtinctionSharedPhase::Generate,
                    phaseIndex / ECSRenderDetail::s_SharedComputeEmulationPhasesPerDraw,
                    phaseIndex == 0u,
                    phaseIndex + 1u == extinctionSharedComputeEmulationPhaseCount,
                    isRasterPhase
                        ? extinctionSharedRasterResourceUses
                        : extinctionSharedGenerateResourceUses,
                    extinctionMaterialResourceSetUses,
                    extinctionMaterialResourceSetUseCount
                )
            ;
            if(!m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex].valid()){
                NWB_LOGGER_WARNING(NWB_TEXT(
                    "RendererSystem: could not declare AVBOIT Extinction shared compute-emulation phase"
                ));
                return false;
            }
            extinctionSharedComputeEmulationDependency =
                m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTasks[phaseIndex];
        }
        m_avboitSystem.taskGraphStage().m_extinctionSharedComputeEmulationTaskCount =
            extinctionSharedComputeEmulationPhaseCount;
        // Terminal raster is the Extinction endpoint; Integration follows with graph-derived ownership.
        m_avboitSystem.taskGraphStage().m_extinctionTask = extinctionSharedComputeEmulationDependency;
    }
    else{
    Core::GpuTaskDesc extinctionDesc;
    extinctionDesc
        .setIdentity(Name("render.avboit.extinction"))
        .setMarkerLabel("AVBOIT Extinction")
        .setQueue(GraphicsComputeQueueRequest())
        .setScheduling(avboitExtinctionScheduling)
        .setDependencies(&extinctionDependency, 1u)
        .setResourceUses(extinctionResourceUses.data(), extinctionResourceUses.size())
        .setResourceSetUses(
            extinctionMaterialResourceSetUseCount != 0u ? extinctionMaterialResourceSetUses : nullptr,
            extinctionMaterialResourceSetUseCount
        )
    ;
    m_avboitSystem.taskGraphStage().m_extinctionTask = m_graph.addTask<AvboitExtinctionGraphTask>(
        extinctionDesc,
        Move(extinctionPayload)
    );
    if(!m_avboitSystem.taskGraphStage().m_extinctionTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
        return false;
    }
    }
    outResult.extinctionTask = m_avboitSystem.taskGraphStage().m_extinctionTask;
    outResult.declared = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
