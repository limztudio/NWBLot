// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_csg_interval_stage.h"

#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/generated_geometry_state.h>
#include <impl/ecs_render/material/renderer_draw_types.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueCsgIntervalStage::OpaqueCsgIntervalStage(
    Core::GpuTaskGraph& graph,
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsRuntime& graphics,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket*& receiverSpanTimingTicket,
    Core::GpuTimingSubmissionTicket*& intervalCombineTimingTicket,
    Core::GpuTimingSubmissionTicket*& intervalSampleTimingTicket,
    const bool& meshViewSetupReady,
    const bool& sceneShadingSetupReady,
    Optional<Core::GpuTimingMeasure>& intervalSampleComputeEmulationTiming
)noexcept
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_intervalSampleComputeEmulationTiming(intervalSampleComputeEmulationTiming)
    , m_receiverSpanPayload{ arena, materialSystem, csgSystem, targets, receiverSpanTimingTicket, meshViewSetupReady, sceneShadingSetupReady, }
    , m_intervalCombinePayload{ arena, materialSystem, csgSystem, targets, intervalCombineTimingTicket, meshViewSetupReady, sceneShadingSetupReady, }
    , m_intervalSamplePayload{ arena, graphics, materialSystem, csgSystem, targets, intervalSampleTimingTicket, meshViewSetupReady, sceneShadingSetupReady, }
    , m_intervalSampleComputeEmulationPayload{ arena, graphics, materialSystem, targets, intervalSampleTimingTicket, meshViewSetupReady, sceneShadingSetupReady, intervalSampleComputeEmulationTiming, }
{}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void OpaqueCsgIntervalStage::capturePayloads(
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const MaterialPassDrawItemPartitions& opaqueDrawItems,
    const CsgFrameGpuData& csgFrameData,
    const usize instanceCount,
    const usize materialTypedByteCount,
    const bool materialDrawBuffersUploaded,
    const bool csgFrameBuffersUploaded,
    const bool hasOpaqueDrawItems,
    const bool hasOpaqueCsgFrameWork
){
    m_receiverSpanPayload.frameBindings = frameBindings;
    m_intervalCombinePayload.frameBindings = frameBindings;
    m_intervalSamplePayload.frameBindings = frameBindings;
    m_intervalSamplePayload.csgResources = csgResources;
    m_intervalSampleComputeEmulationPayload.frameBindings = frameBindings;
    if(!hasOpaqueCsgFrameWork)
        return;

    // The graph owns the producer fence and each preceding producer fence. Normal prefix compilation merges these callbacks with G-buffer; a FrontierSafe boundary retains each timing ticket.
    m_receiverSpanPayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_receiverSpanPayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_receiverSpanPayload.csgResources = csgResources;
    m_receiverSpanPayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
    m_intervalCombinePayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_intervalCombinePayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_intervalCombinePayload.csgResources = csgResources;
    m_intervalCombinePayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
    m_intervalSamplePayload.materialDrawBuffersUploaded = materialDrawBuffersUploaded;
    m_intervalSamplePayload.csgFrameBuffersUploaded = csgFrameBuffersUploaded;
    m_intervalSamplePayload.materialFrameStatesGraphOwned = hasOpaqueDrawItems;
    m_intervalSamplePayload.opaqueDrawSnapshot.capture(opaqueDrawItems, csgFrameData, instanceCount, materialTypedByteCount);
}

Expected<OpaqueCsgIntervalStageTasks> OpaqueCsgIntervalStage::declare(Core::Alloc::ScratchArena& scratchArena, const OpaqueCsgIntervalStageInputs& inputs, ObjectGeometryCacheGraph& objectGeometry){
    using namespace RendererTaskGraphDetail;

    OpaqueCsgIntervalStageTasks tasks;
    Core::GpuGraphResourceSetId opaqueCsgIntervalSampleComputeEmulationOutputSet;
    Core::GpuTaskSchedulingHint csgReceiverSpanScheduling;
    csgReceiverSpanScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgReceiverSpanScheduling.forceSubmissionBoundary = false;
    csgReceiverSpanScheduling.allowPacketMerge = true;
    csgReceiverSpanScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc csgReceiverSpanDesc;
    csgReceiverSpanDesc
        .setIdentity(Name("render.graphics_prefix.csg_receiver_span"))
        .setMarkerLabel("Opaque CSG Receiver Span")
        .setScheduling(csgReceiverSpanScheduling)
        .setDependencies(&inputs.dependencyTask, 1u)
        .setResourceUses(inputs.receiverSpanResourceUses.data(), inputs.receiverSpanResourceUses.size())
    ;
    const auto receiverSpanTaskResult = m_graph.addTask< ECSRenderDetail::CsgReceiverSpanBuildGraphTask >(csgReceiverSpanDesc, Move(m_receiverSpanPayload));
    if(!receiverSpanTaskResult){
        tasks.receiverSpanTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG receiver-span task"));
        return MakeUnexpected(Failure{});
    }
    tasks.receiverSpanTask = *receiverSpanTaskResult;

    Core::GpuTaskSchedulingHint csgIntervalCombineScheduling;
    csgIntervalCombineScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgIntervalCombineScheduling.forceSubmissionBoundary = false;
    csgIntervalCombineScheduling.allowPacketMerge = true;
    csgIntervalCombineScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc csgIntervalCombineDesc;
    csgIntervalCombineDesc
        .setIdentity(Name("render.graphics_prefix.csg_interval_combine"))
        .setMarkerLabel("Opaque CSG Interval Combine")
        .setScheduling(csgIntervalCombineScheduling)
        .setDependencies(&tasks.receiverSpanTask, 1u)
        .setResourceUses(inputs.intervalCombineResourceUses.data(), inputs.intervalCombineResourceUses.size())
    ;
    const auto intervalCombineTaskResult = m_graph.addTask< ECSRenderDetail::CsgIntervalCombineGraphTask >(csgIntervalCombineDesc, Move(m_intervalCombinePayload));
    if(!intervalCombineTaskResult){
        tasks.intervalCombineTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-combine task"));
        return MakeUnexpected(Failure{});
    }
    tasks.intervalCombineTask = *intervalCombineTaskResult;

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgIntervalSampleResourceUses{ scratchArena };
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> opaqueCsgIntervalSampleComputeEmulationResourceUses{ scratchArena };
    Core::GpuGraphResourceSetId csgIntervalSampleMaterialGeometrySet;
    Core::GpuGraphResourceSetId csgIntervalSampleMaterialSampledTextureSet;
    const MaterialPassDrawItems* const csgIntervalSampleMaterialGeometryDrawSets[] = { &inputs.opaqueDrawItems.csg };
    const bool csgIntervalSampleUsesMaterialGeometry = !inputs.opaqueDrawItems.csg.empty();
    m_intervalSamplePayload.materialGeometryStatesGraphOwned = false;
    if(csgIntervalSampleUsesMaterialGeometry){
        const auto csgIntervalSampleMaterialGeometrySetResult = GatherPreparedMaterialGeometryResourceSet(
            m_graph,
            csgIntervalSampleMaterialGeometryDrawSets,
            LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
            scratchArena,
            Name("render.graphics_prefix.csg_interval_sample.material_geometry"),
            "Opaque CSG Material Geometry"
        );
        m_intervalSamplePayload.materialGeometryStatesGraphOwned = csgIntervalSampleMaterialGeometrySetResult.has_value();
        if(csgIntervalSampleMaterialGeometrySetResult)
            csgIntervalSampleMaterialGeometrySet = *csgIntervalSampleMaterialGeometrySetResult;
    }
    if(csgIntervalSampleUsesMaterialGeometry && !m_intervalSamplePayload.materialGeometryStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material geometry states"));
        return MakeUnexpected(Failure{});
    }
    bool csgIntervalSampleMaterialSampledTexturesCollected = false;
    if(csgIntervalSampleUsesMaterialGeometry){
        const auto csgIntervalSampleMaterialSampledTextureSetResult = GatherPreparedMaterialSampledTextureResourceSet(
            m_materialSystem,
            m_graph,
            csgIntervalSampleMaterialGeometryDrawSets,
            LengthOf(csgIntervalSampleMaterialGeometryDrawSets),
            scratchArena,
            Name("render.graphics_prefix.csg_interval_sample.material_sampled_textures"),
            "Opaque CSG Material Sampled Textures"
        );
        csgIntervalSampleMaterialSampledTexturesCollected = csgIntervalSampleMaterialSampledTextureSetResult.has_value();
        if(csgIntervalSampleMaterialSampledTextureSetResult)
            csgIntervalSampleMaterialSampledTextureSet = *csgIntervalSampleMaterialSampledTextureSetResult;
    }
    if(csgIntervalSampleUsesMaterialGeometry && !csgIntervalSampleMaterialSampledTexturesCollected){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare prepared opaque CSG material sampled textures"));
        return MakeUnexpected(Failure{});
    }

    // The CSG interval-sample material stream has a different placement from both regular opaque and receiver CSG work: Combine has already completed, and the following Sample callback rasterizes immediately.
    // That makes pairwise-distinct outputs graph-ownable without excluding earlier regular/receiver aliases.
    const bool opaqueCsgIntervalSampleComputeEmulationPlanCaptured = inputs.hasCsgFrameGpuWork
        && m_intervalSamplePayload.materialFrameStatesGraphOwned
        && m_intervalSamplePayload.materialGeometryStatesGraphOwned
        && csgIntervalSampleMaterialSampledTexturesCollected
        && m_intervalSampleComputeEmulationPayload.plan.capture(inputs.opaqueDrawItems.csg, inputs.csgFrameData, scratchArena)
    ;
    bool opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned = false;
    if(opaqueCsgIntervalSampleComputeEmulationPlanCaptured){
        const auto opaqueCsgIntervalSampleComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            m_graph,
            m_intervalSampleComputeEmulationPayload.plan,
            scratchArena,
            Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation.outputs"),
            "Opaque CSG Interval-Sample Compute Emulation Outputs"
        );
        opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned = opaqueCsgIntervalSampleComputeEmulationOutputSetResult.has_value();
        if(opaqueCsgIntervalSampleComputeEmulationOutputSetResult)
            opaqueCsgIntervalSampleComputeEmulationOutputSet = *opaqueCsgIntervalSampleComputeEmulationOutputSetResult;
    }
    if(opaqueCsgIntervalSampleComputeEmulationPlanCaptured && !opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG interval-sample compute-emulation outputs"));
    }
    m_intervalSamplePayload.csgComputeEmulationOutputStatesGraphOwned = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
    m_intervalSamplePayload.opaqueCsgComputeEmulationTiming = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned ? &m_intervalSampleComputeEmulationTiming : nullptr;
    // Material instance and typed-material reads.
    constexpr usize s_MaterialBufferResourceUseCount = 2u;
    constexpr usize s_CsgRemovedIntervalResourceUseCount = 4u;
    constexpr usize s_GbufferColorResourceUseCount = 4u;
    csgIntervalSampleResourceUses.reserve(1u + (inputs.hasOpaqueDrawItems ? s_MaterialBufferResourceUseCount : 0u) + (inputs.hasCsgFrameGpuWork ? s_OpaqueCsgFrameGpuResourceUseCount : 0u) + s_CsgRemovedIntervalResourceUseCount + s_GbufferColorResourceUseCount);
    csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
    if(inputs.hasOpaqueDrawItems){
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
    }
    if(inputs.hasCsgFrameGpuWork){
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
        csgIntervalSampleResourceUses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        csgIntervalSampleResourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    }
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalDepth, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCapNormal, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalData, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCount, inputs.removedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.albedo, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.normal, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.worldPosition, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.specularRoughness, Core::ResourceStates::RenderTarget));
    csgIntervalSampleResourceUses.push_back(ReadWriteUse(inputs.resources.depth, Core::ResourceStates::DepthWrite));
    const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialGeometrySetUse{ .resourceSet = csgIntervalSampleMaterialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse csgIntervalSampleMaterialSampledTextureSetUse{
        .resourceSet = csgIntervalSampleMaterialSampledTextureSet,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse{
        .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
        .range = {},
        .requiredState = Core::ResourceStates::UnorderedAccess,
        .access = Core::GpuTaskResourceAccess::Write,
    };
    const Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse{
        .resourceSet = opaqueCsgIntervalSampleComputeEmulationOutputSet,
        .range = {},
        .requiredState = ECSRenderDetail::s_GeneratedGeometryRasterState,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Core::GpuTaskResourceSetUse csgIntervalSampleMaterialResourceSetUses[3u] = {};
    usize csgIntervalSampleMaterialResourceSetUseCount = 0u;
    if(m_intervalSamplePayload.materialGeometryStatesGraphOwned){
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = csgIntervalSampleMaterialGeometrySetUse;
    }
    if(csgIntervalSampleMaterialSampledTextureSet.valid()){
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = csgIntervalSampleMaterialSampledTextureSetUse;
    }
    if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
        csgIntervalSampleMaterialResourceSetUses[csgIntervalSampleMaterialResourceSetUseCount++] = opaqueCsgIntervalSampleComputeEmulationOutputVertexBufferSetUse;
    }

    Core::GpuTaskId csgIntervalSampleDependency = tasks.intervalCombineTask;
    if(m_intervalSamplePayload.materialDrawBuffersUploaded && !objectGeometry.prepare(
        inputs.opaqueDrawItems.csg.indexedDrawItems.data(), inputs.opaqueDrawItems.csg.indexedDrawItems.size(),
        inputs.frameBindings, inputs.targets, csgIntervalSampleDependency, csgIntervalSampleResourceUses,
        scratchArena, nullptr, inputs.intervalSampleTimingTicket
    ))
        return MakeUnexpected(Failure{});
    if(opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned){
        m_intervalSampleComputeEmulationPayload.csgResources = inputs.csgResources;
        // Producer and sample share one CSG interval-sample ticket, spanning compute through the raster half.
        m_intervalSampleComputeEmulationPayload.instanceCount = inputs.instanceCount;
        m_intervalSampleComputeEmulationPayload.materialTypedByteCount = inputs.materialTypedByteCount;
        m_intervalSampleComputeEmulationPayload.materialDrawBuffersUploaded = m_intervalSamplePayload.materialDrawBuffersUploaded;
        m_intervalSampleComputeEmulationPayload.csgFrameBuffersUploaded = m_intervalSamplePayload.csgFrameBuffersUploaded;
        m_intervalSampleComputeEmulationPayload.materialFrameStatesGraphOwned = m_intervalSamplePayload.materialFrameStatesGraphOwned;
        m_intervalSampleComputeEmulationPayload.materialGeometryStatesGraphOwned = m_intervalSamplePayload.materialGeometryStatesGraphOwned;

        constexpr usize s_OpaqueCsgIntervalSampleCapacity = 12u;
        opaqueCsgIntervalSampleComputeEmulationResourceUses.reserve(s_OpaqueCsgIntervalSampleCapacity);
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadUse(inputs.resources.meshView, Core::ResourceStates::ConstantBuffer));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadBufferUse(inputs.resources.materialInstances, inputs.materialInstanceRange));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadBufferUse(inputs.resources.materialTyped, inputs.materialTypedRange));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadBufferUse(inputs.resources.csgReceiverRanges, inputs.csgReceiverRange));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadBufferUse(inputs.resources.csgCutters, inputs.csgCutterRange));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadUse(inputs.resources.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadUse(inputs.resources.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadUse(inputs.resources.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalDepth, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCapNormal, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalData, inputs.removedIntervalSubresources, Core::ResourceStates::UnorderedAccess));
        opaqueCsgIntervalSampleComputeEmulationResourceUses.push_back(ReadTextureUse(inputs.resources.csgRemovedIntervalCount, inputs.removedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess));
        Core::GpuTaskResourceSetUse opaqueCsgIntervalSampleComputeEmulationResourceSetUses[3u] = {};
        usize opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount = 0u;
        opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = csgIntervalSampleMaterialGeometrySetUse;
        if(csgIntervalSampleMaterialSampledTextureSet.valid()){
            opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = csgIntervalSampleMaterialSampledTextureSetUse;
        }
        opaqueCsgIntervalSampleComputeEmulationResourceSetUses[opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount++] = opaqueCsgIntervalSampleComputeEmulationOutputUavSetUse;
        Core::GpuTaskSchedulingHint opaqueCsgIntervalSampleComputeEmulationScheduling;
        opaqueCsgIntervalSampleComputeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        opaqueCsgIntervalSampleComputeEmulationScheduling.forceSubmissionBoundary = false;
        opaqueCsgIntervalSampleComputeEmulationScheduling.allowPacketMerge = true;
        opaqueCsgIntervalSampleComputeEmulationScheduling.mergeWithPrevious = true;
        // Combine is the explicit immediate predecessor.
        // The graph preserves its interval-image UAV handoff across any FrontierSafe split; Sample merges with this producer so its generated-vertex handoff and timing scope remain in one accepted Graphics packet.
        opaqueCsgIntervalSampleComputeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc opaqueCsgIntervalSampleComputeEmulationDesc;
        opaqueCsgIntervalSampleComputeEmulationDesc
            .setIdentity(Name("render.graphics_prefix.opaque_csg_interval_sample_compute_emulation"))
            .setMarkerLabel("Opaque CSG Interval-Sample Compute Emulation")
            .setScheduling(opaqueCsgIntervalSampleComputeEmulationScheduling)
            .setDependencies(&csgIntervalSampleDependency, 1u)
            .setResourceUses(opaqueCsgIntervalSampleComputeEmulationResourceUses.data(), opaqueCsgIntervalSampleComputeEmulationResourceUses.size())
            .setResourceSetUses(opaqueCsgIntervalSampleComputeEmulationResourceSetUses, opaqueCsgIntervalSampleComputeEmulationResourceSetUseCount)
        ;
        const auto intervalSampleComputeEmulationTaskResult = m_graph.addTask<
                ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphTask
            >(opaqueCsgIntervalSampleComputeEmulationDesc, Move(m_intervalSampleComputeEmulationPayload));
        if(!intervalSampleComputeEmulationTaskResult){
            tasks.intervalSampleComputeEmulationTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample compute-emulation producer"));
            return MakeUnexpected(Failure{});
        }
        tasks.intervalSampleComputeEmulationTask = *intervalSampleComputeEmulationTaskResult;
        csgIntervalSampleDependency = tasks.intervalSampleComputeEmulationTask;
    }

    Core::GpuTaskSchedulingHint csgIntervalSampleScheduling;
    csgIntervalSampleScheduling.cost = Core::GpuTaskCostHint::Medium;
    csgIntervalSampleScheduling.forceSubmissionBoundary = false;
    csgIntervalSampleScheduling.allowPacketMerge = true;
    csgIntervalSampleScheduling.mergeWithPrevious = true;
    csgIntervalSampleScheduling.allowMergeAcrossConsumerFrontier = opaqueCsgIntervalSampleComputeEmulationOutputStatesGraphOwned;
    Core::GpuTaskDesc csgIntervalSampleDesc;
    csgIntervalSampleDesc
        .setIdentity(Name("render.graphics_prefix.csg_interval_sample"))
        .setMarkerLabel("Opaque CSG Interval Sample")
        .setScheduling(csgIntervalSampleScheduling)
        .setDependencies(&csgIntervalSampleDependency, 1u)
        .setResourceUses(csgIntervalSampleResourceUses.data(), csgIntervalSampleResourceUses.size())
        .setResourceSetUses(csgIntervalSampleMaterialResourceSetUseCount != 0u ? csgIntervalSampleMaterialResourceSetUses : nullptr, csgIntervalSampleMaterialResourceSetUseCount)
    ;
    const auto intervalSampleTaskResult = m_graph.addTask< ECSRenderDetail::CsgIntervalSampleGraphTask >(csgIntervalSampleDesc, Move(m_intervalSamplePayload));
    if(!intervalSampleTaskResult){
        tasks.intervalSampleTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque CSG interval-sample task"));
        return MakeUnexpected(Failure{});
    }
    tasks.intervalSampleTask = *intervalSampleTaskResult;
    return tasks;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

