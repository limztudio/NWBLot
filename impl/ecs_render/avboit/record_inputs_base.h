// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/avboit/compute_emulation_record.h>
#include <impl/ecs_render/avboit/frame_graph_avboit_upload_result.h>
#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/material/task_graph_compute_emulation_plan.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/shared/renderer_frame_bindings.h>
#include <impl/ecs_render/shared/task_graph_draw_snapshots.h>

#include <core/common/log.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AvboitRecordInputsBase : public FrameGraphAvboitMaterialUploadResult{
    DeferredFrameTargets& targets;
    Core::GpuGraphResourceId albedo;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId avboitMaterialDomain;
    Core::GpuGraphResourceId avboitCsgDomain;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId materialInstances;
    Core::GpuGraphResourceId materialTyped;
    Core::GpuGraphResourceId csgReceiverRanges;
    Core::GpuGraphResourceId csgCutters;
    Core::GpuGraphResourceId csgClipContextSlots;
    Core::GpuGraphResourceId csgIntervalSampleState;
    Core::GpuGraphResourceId csgRemovedIntervalDepth;
    Core::GpuGraphResourceId csgRemovedIntervalCapNormal;
    Core::GpuGraphResourceId csgRemovedIntervalData;
    Core::GpuGraphResourceId csgRemovedIntervalCount;
    Core::TextureSubresourceSet csgRemovedIntervalSubresources;
    Core::TextureSubresourceSet csgRemovedIntervalCountSubresources;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::BufferRange instanceRange;
    Core::BufferRange materialTypedRange;
    Core::BufferRange receiverRange;
    Core::BufferRange cutterRange;
    b8 intervalOutputsGraphOwned = false;


public:
    explicit AvboitRecordInputsBase(DeferredFrameTargets& targetsIn)noexcept
        : targets(targetsIn)
    {}


public:
    void applyResources(const DeferredGraphResourceImportResult& resources, const b8 intervalOutputsOwned)noexcept{
        albedo = resources.albedo;
        normal = resources.normal;
        worldPosition = resources.worldPosition;
        depth = resources.depth;
        refractionInstance = resources.refractionInstance;
        avboitMaterialDomain = resources.avboitMaterialDomain;
        avboitCsgDomain = resources.avboitCsgDomain;
        meshView = resources.meshView;
        materialInstances = resources.materialInstances;
        materialTyped = resources.materialTyped;
        csgReceiverRanges = resources.csgReceiverRanges;
        csgCutters = resources.csgCutters;
        csgClipContextSlots = resources.csgClipContextSlots;
        csgIntervalSampleState = resources.csgIntervalSampleState;
        csgRemovedIntervalDepth = resources.csgRemovedIntervalDepth;
        csgRemovedIntervalCapNormal = resources.csgRemovedIntervalCapNormal;
        csgRemovedIntervalData = resources.csgRemovedIntervalData;
        csgRemovedIntervalCount = resources.csgRemovedIntervalCount;
        csgRemovedIntervalSubresources = resources.csgRemovedIntervalSubresources;
        csgRemovedIntervalCountSubresources = resources.csgRemovedIntervalCountSubresources;
        currentBindlessSlots = resources.currentBindlessSlots;
        intervalOutputsGraphOwned = intervalOutputsOwned;
    }

    void applyUploadResult(const FrameGraphAvboitMaterialUploadResult& upload)noexcept{
        static_cast<FrameGraphAvboitMaterialUploadResult&>(*this) = upload;
    }

    // Reuse requires the regular emulation plan and excludes CSG/shared plans.
    [[nodiscard]] b8 generatedGeometryReusePlansValid()const noexcept{
        const b8 generatedGeometryReused = reusedGeometryProducer.valid();
        if((generatedGeometryReused || producesReusableGeometry) && (!regularComputeEmulationPlanCaptured || csgComputeEmulationPlanCaptured || sharedComputeEmulationPlanCaptured))
            return false;
        return true;
    }

    [[nodiscard]] b8 generatedGeometryReused()const noexcept{
        return reusedGeometryProducer.valid();
    }
};

// The interval producer wrote these aliases; the graph lowers their same-UAV handoff.
template<typename UseVector>
inline void AppendCsgRemovedIntervalUses(
    const Core::GpuGraphResourceId removedIntervalDepth,
    const Core::GpuGraphResourceId removedIntervalCapNormal,
    const Core::GpuGraphResourceId removedIntervalData,
    const Core::GpuGraphResourceId removedIntervalCount,
    const Core::TextureSubresourceSet& subresources,
    const Core::TextureSubresourceSet& countSubresources,
    UseVector& resourceUses
)noexcept(noexcept(resourceUses.push_back(RendererTaskGraphDetail::ReadTextureUse(removedIntervalDepth, subresources, Core::ResourceStates::UnorderedAccess)))){
    resourceUses.push_back(RendererTaskGraphDetail::ReadTextureUse(removedIntervalDepth, subresources, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(RendererTaskGraphDetail::ReadTextureUse(removedIntervalCapNormal, subresources, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(RendererTaskGraphDetail::ReadTextureUse(removedIntervalData, subresources, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(RendererTaskGraphDetail::ReadTextureUse(removedIntervalCount, countSubresources, Core::ResourceStates::UnorderedAccess));
}

#define NWB_MAKE_AVBOIT_RASTER_RECORD_PHASE_DESC(phaseIdPrefix, phaseLabelPrefix, rasterIdentityIn, rasterLabelIn, resourceBaseCapacityIn) \
    AvboitRasterRecordPhaseDesc{ \
        .regularOutputSetLabel = phaseLabelPrefix " Compute Emulation Outputs", \
        .csgOutputSetLabel = phaseLabelPrefix " CSG Compute Emulation Outputs", \
        .sharedOutputLabel = phaseLabelPrefix " Shared Compute Emulation Output", \
        .regularComputeEmulationLabel = phaseLabelPrefix " Compute Emulation", \
        .csgComputeEmulationLabel = phaseLabelPrefix " CSG Compute Emulation", \
        .sharedPhaseIdentities = sharedPhaseIdentities, \
        .sharedPhaseLabels = sharedPhaseMarkers, \
        .rasterLabel = (rasterLabelIn), \
        .regularOutputSetIdentity = Name(phaseIdPrefix ".compute_emulation.outputs"), \
        .csgOutputSetIdentity = Name(phaseIdPrefix ".csg_compute_emulation.outputs"), \
        .regularComputeEmulationIdentity = Name(phaseIdPrefix ".compute_emulation"), \
        .csgComputeEmulationIdentity = Name(phaseIdPrefix ".csg_compute_emulation"), \
        .rasterIdentity = (rasterIdentityIn), \
        .resourceBaseCapacity = (resourceBaseCapacityIn), \
    }

struct AvboitRasterRecordStageOutputs{
    Core::GpuTaskId& reusedGeometryProducer;
    Core::GpuTaskId& streamTask;
    Core::GpuTaskId& computeEmulationTask;
    Span<Core::GpuTaskId> sharedComputeEmulationTasks{};
    usize& sharedComputeEmulationTaskCount;
    Core::GpuTaskId& rasterTask;
};

class AvboitRecordBuilderBase : NoCopy{
public:
    AvboitRecordBuilderBase(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererMaterialSystem& materialSystem, RendererAvboitSystem& avboitSystem)noexcept
        : m_graph(graph)
        , m_graphics(graphics)
        , m_materialSystem(materialSystem)
        , m_avboitSystem(avboitSystem)
    {}


protected:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererMaterialSystem& m_materialSystem;
    RendererAvboitSystem& m_avboitSystem;
};

template<typename ComputeEmulationTaskT, typename SharedComputeEmulationTaskT, typename RasterTaskT>
[[nodiscard]] inline b8 DeclareAvboitRasterRecordPhase(
    Core::GpuTaskGraph& graph,
    Core::GraphicsRuntime& graphics,
    RendererMaterialSystem& materialSystem,
    Core::Alloc::ScratchArena& scratchArena,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitRecordInputsBase& inputs,
    const Core::GpuTaskId initialRasterDependency,
    const Span<const Core::GpuTaskResourceUse> baseResourceUses,
    Core::GpuTimingSubmissionTicket& timingTicket,
    Optional<Core::GpuTimingMeasure>& computeEmulationTiming,
    const AvboitRasterRecordPhaseDesc& phaseDesc,
    AvboitRasterPassTaskPayload& rasterPayload,
    AvboitComputeEmulationTaskPayload& computeEmulationPayload,
    const AvboitRasterRecordStageOutputs& stageOutputs
){
    using namespace RendererTaskGraphDetail;

    const b8 generatedGeometryReused = inputs.generatedGeometryReused();
    if(!inputs.generatedGeometryReusePlansValid())
        return false;
    rasterPayload.generatedGeometryReused = generatedGeometryReused;
    stageOutputs.reusedGeometryProducer = inputs.reusedGeometryProducer;

    const b8 streamsUploaded = inputs.streamsUploaded || rasterPayload.streamsUploaded;
    const b8 hasCsgIntervalReads = inputs.csgStreamsUploaded;
    NWB_ASSERT(!hasCsgIntervalReads || (rasterPayload.phasePrepared && rasterPayload.snapshot.captured));
    rasterPayload.materialFrameStatesGraphOwned = streamsUploaded;
    rasterPayload.materialGeometryStatesGraphOwned = streamsUploaded && rasterPayload.materialGeometryStatesGraphOwned;

    Core::GpuGraphResourceSetId computeEmulationOutputSet;
    const b8 computeEmulationPlanCaptured = inputs.regularComputeEmulationPlanCaptured || inputs.csgComputeEmulationPlanCaptured;
    b8 computeEmulationOutputStatesGraphOwned = false;
    if(inputs.regularComputeEmulationPlanCaptured){
        const auto outputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            computeEmulationPayload.plan,
            scratchArena,
            phaseDesc.regularOutputSetIdentity,
            phaseDesc.regularOutputSetLabel
        );
        computeEmulationOutputStatesGraphOwned = outputSetResult.has_value();
        if(outputSetResult)
            computeEmulationOutputSet = *outputSetResult;
    }
    else if(inputs.csgComputeEmulationPlanCaptured){
        const auto outputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            computeEmulationPayload.csgPlan,
            scratchArena,
            phaseDesc.csgOutputSetIdentity,
            phaseDesc.csgOutputSetLabel
        );
        computeEmulationOutputStatesGraphOwned = outputSetResult.has_value();
        if(outputSetResult)
            computeEmulationOutputSet = *outputSetResult;
    }
    if(computeEmulationPlanCaptured && !computeEmulationOutputStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT compute-emulation output states"));
    if(generatedGeometryReused && !computeEmulationOutputStatesGraphOwned)
        return false;
    rasterPayload.computeEmulationOutputStatesGraphOwned = inputs.regularComputeEmulationPlanCaptured && computeEmulationOutputStatesGraphOwned;
    rasterPayload.csgComputeEmulationOutputStatesGraphOwned = inputs.csgComputeEmulationPlanCaptured && computeEmulationOutputStatesGraphOwned;
    rasterPayload.computeEmulationTiming = computeEmulationOutputStatesGraphOwned ? &computeEmulationTiming : nullptr;

    Core::GpuGraphResourceId sharedComputeEmulationOutput;
    b8 sharedComputeEmulationOutputStatesGraphOwned = false;
    if(inputs.sharedComputeEmulationPlanCaptured){
        const auto sharedOutputResult = GatherRegularSharedComputeEmulationResource(graph, inputs.sharedComputeEmulationPlan, phaseDesc.sharedOutputLabel);
        sharedComputeEmulationOutputStatesGraphOwned = sharedOutputResult.has_value();
        if(sharedOutputResult)
            sharedComputeEmulationOutput = *sharedOutputResult;
    }
    if(inputs.sharedComputeEmulationPlanCaptured && !sharedComputeEmulationOutputStatesGraphOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned AVBOIT shared compute-emulation output state"));

    inputs.instanceRange = Core::BufferRange(0u, rasterPayload.snapshot.instanceCount * sizeof(InstanceGpuData));
    inputs.materialTypedRange = Core::BufferRange(0u, rasterPayload.snapshot.materialTypedByteCount);
    inputs.receiverRange = Core::BufferRange(0u, rasterPayload.snapshot.csgReceiverRanges.size() * sizeof(CsgReceiverRangeGpuData));
    inputs.cutterRange = Core::BufferRange(0u, rasterPayload.snapshot.csgCutters.size() * sizeof(CsgCutterGpuData));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(
        phaseDesc.resourceBaseCapacity
        + (streamsUploaded ? s_AvboitStreamUseCount : 0u)
        + (hasCsgIntervalReads ? s_AvboitCsgIntervalUseCount : 0u)
    );
    for(const Core::GpuTaskResourceUse& use : baseResourceUses)
        resourceUses.push_back(use);
    if(streamsUploaded){
        resourceUses.push_back(ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer));
        resourceUses.push_back(ReadBufferUse(inputs.materialInstances, inputs.instanceRange));
        resourceUses.push_back(ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange));
        if(inputs.csgStreamsUploaded){
            resourceUses.push_back(ReadBufferUse(inputs.csgReceiverRanges, inputs.receiverRange));
            resourceUses.push_back(ReadBufferUse(inputs.csgCutters, inputs.cutterRange));
            resourceUses.push_back(ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer));
            resourceUses.push_back(ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer));
        }
    }
    if(hasCsgIntervalReads){
        AppendCsgRemovedIntervalUses(
            inputs.csgRemovedIntervalDepth,
            inputs.csgRemovedIntervalCapNormal,
            inputs.csgRemovedIntervalData,
            inputs.csgRemovedIntervalCount,
            inputs.csgRemovedIntervalSubresources,
            inputs.csgRemovedIntervalCountSubresources,
            resourceUses
        );
    }
    const Core::GpuTaskResourceSetUse materialGeometrySetUse{ .resourceSet = inputs.materialGeometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse materialSampledTextureSetUse{ .resourceSet = inputs.materialSampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read, };
    const Core::GpuTaskResourceSetUse computeEmulationOutputUavSetUse{ .resourceSet = computeEmulationOutputSet, .range = {}, .requiredState = Core::ResourceStates::UnorderedAccess, .access = Core::GpuTaskResourceAccess::Write, };
    const Core::GpuTaskResourceSetUse computeEmulationOutputVertexBufferSetUse{
        .resourceSet = computeEmulationOutputSet,
        .range = {},
        .requiredState = ECSRenderDetail::s_ObjectGeometryRasterState,
        .access = Core::GpuTaskResourceAccess::Read,
    };
    Array<Core::GpuTaskResourceSetUse, s_AvboitMaterialResourceSetUseCapacity> materialResourceSetUses = {};
    usize materialResourceSetUseCount = 0u;
    if(rasterPayload.materialGeometryStatesGraphOwned)
        materialResourceSetUses[materialResourceSetUseCount++] = materialGeometrySetUse;
    if(inputs.materialSampledTextureSet.valid())
        materialResourceSetUses[materialResourceSetUseCount++] = materialSampledTextureSetUse;
    if(computeEmulationOutputStatesGraphOwned)
        materialResourceSetUses[materialResourceSetUseCount++] = computeEmulationOutputVertexBufferSetUse;
    resourceUses.push_back(ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(inputs.avboitMaterialDomain));
    resourceUses.push_back(ReadUse(inputs.avboitCsgDomain));

    // Keep the final upload as stream anchor; replacing it hides a broken producer handoff.
    if(streamsUploaded)
        stageOutputs.streamTask = inputs.uploadTask;
    Core::GpuTaskId rasterDependency = initialRasterDependency;
    if(streamsUploaded && !objectGeometry.prepare(rasterPayload.snapshot.regularIndexedDrawItems, frameBindings, scratchArena, rasterDependency, resourceUses, &timingTicket))
        return false;
    if(streamsUploaded && !objectGeometry.prepare(rasterPayload.snapshot.csgIndexedDrawItems, frameBindings, scratchArena, rasterDependency, resourceUses, &timingTicket))
        return false;

    Core::GpuTaskSchedulingHint rasterScheduling;
    rasterScheduling.cost = Core::GpuTaskCostHint::Large;
    rasterScheduling.forceSubmissionBoundary = false;
    rasterScheduling.allowPacketMerge = true;
    rasterScheduling.mergeWithPrevious = true;
    rasterScheduling.allowMergeAcrossConsumerFrontier = true;

    if(computeEmulationOutputStatesGraphOwned && !generatedGeometryReused){
        computeEmulationPayload.conservativeGeometryScissor = inputs.producesReusableGeometry;
        computeEmulationPayload.instanceCount = rasterPayload.snapshot.instanceCount;
        computeEmulationPayload.materialTypedByteCount = rasterPayload.snapshot.materialTypedByteCount;
        computeEmulationPayload.materialDrawBuffersUploaded = streamsUploaded;
        computeEmulationPayload.csgFrameBuffersUploaded = inputs.csgStreamsUploaded;
        computeEmulationPayload.materialFrameStatesGraphOwned = rasterPayload.materialFrameStatesGraphOwned;
        computeEmulationPayload.materialGeometryStatesGraphOwned = rasterPayload.materialGeometryStatesGraphOwned;

        Array<Core::GpuTaskResourceUse, s_AvboitEmulationBaseCount + s_AvboitEmulationPlanUseCount> computeEmulationResourceUses = {
            ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer),
            ReadBufferUse(inputs.materialInstances, inputs.instanceRange),
            ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        usize computeEmulationResourceUseCount = s_AvboitEmulationBaseCount;
        if(inputs.csgComputeEmulationPlanCaptured){
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadBufferUse(inputs.csgReceiverRanges, inputs.receiverRange);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadBufferUse(inputs.csgCutters, inputs.cutterRange);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadUse(inputs.csgClipContextSlots, Core::ResourceStates::ConstantBuffer);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadUse(inputs.csgIntervalSampleState, Core::ResourceStates::ConstantBuffer);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadTextureUse(inputs.csgRemovedIntervalDepth, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadTextureUse(inputs.csgRemovedIntervalCapNormal, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadTextureUse(inputs.csgRemovedIntervalData, inputs.csgRemovedIntervalSubresources, Core::ResourceStates::UnorderedAccess);
            computeEmulationResourceUses[computeEmulationResourceUseCount++] = ReadTextureUse(inputs.csgRemovedIntervalCount, inputs.csgRemovedIntervalCountSubresources, Core::ResourceStates::UnorderedAccess);
        }
        Array<Core::GpuTaskResourceSetUse, s_AvboitComputeEmulationResourceSetUseCapacity> computeEmulationResourceSetUses = {};
        usize computeEmulationResourceSetUseCount = 0u;
        computeEmulationResourceSetUses[computeEmulationResourceSetUseCount++] = materialGeometrySetUse;
        if(inputs.materialSampledTextureSet.valid())
            computeEmulationResourceSetUses[computeEmulationResourceSetUseCount++] = materialSampledTextureSetUse;
        computeEmulationResourceSetUses[computeEmulationResourceSetUseCount++] = computeEmulationOutputUavSetUse;

        Core::GpuTaskSchedulingHint computeEmulationScheduling;
        computeEmulationScheduling.cost = Core::GpuTaskCostHint::Medium;
        computeEmulationScheduling.forceSubmissionBoundary = false;
        computeEmulationScheduling.allowPacketMerge = true;
        computeEmulationScheduling.mergeWithPrevious = true;
        computeEmulationScheduling.allowMergeAcrossConsumerFrontier = true;
        Core::GpuTaskDesc computeEmulationDesc;
        computeEmulationDesc
            .setIdentity(inputs.csgComputeEmulationPlanCaptured ? phaseDesc.csgComputeEmulationIdentity : phaseDesc.regularComputeEmulationIdentity)
            .setMarkerLabel(inputs.csgComputeEmulationPlanCaptured ? phaseDesc.csgComputeEmulationLabel : phaseDesc.regularComputeEmulationLabel)
            .setScheduling(computeEmulationScheduling)
            .setDependencies({ &rasterDependency, 1u })
            .setResourceUses({ computeEmulationResourceUses.data(), computeEmulationResourceUseCount })
            .setResourceSetUses({ computeEmulationResourceSetUses.data(), computeEmulationResourceSetUseCount })
        ;
        const auto computeEmulationTaskResult = graph.addTask<ComputeEmulationTaskT>(computeEmulationDesc, Move(computeEmulationPayload));
        if(!computeEmulationTaskResult){
            stageOutputs.computeEmulationTask = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT compute-emulation producer"));
            return false;
        }
        stageOutputs.computeEmulationTask = *computeEmulationTaskResult;
        rasterDependency = stageOutputs.computeEmulationTask;
        rasterScheduling.allowMergeAcrossConsumerFrontier = true;
    }

    if(sharedComputeEmulationOutputStatesGraphOwned){
        const Array<Core::GpuTaskResourceUse, 5u> sharedGenerateResourceUses = {
            ReadUse(inputs.meshView, Core::ResourceStates::ConstantBuffer),
            ReadBufferUse(inputs.materialInstances, inputs.instanceRange),
            ReadBufferUse(inputs.materialTyped, inputs.materialTypedRange),
            ReadUse(inputs.currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            WriteUse(sharedComputeEmulationOutput, Core::ResourceStates::UnorderedAccess),
        };

        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> sharedRasterResourceUses{ scratchArena };
        sharedRasterResourceUses.reserve(resourceUses.size() + 1u);
        sharedRasterResourceUses.assign(resourceUses.begin(), resourceUses.end());
        sharedRasterResourceUses.push_back(ReadUse(sharedComputeEmulationOutput, ECSRenderDetail::s_ObjectGeometryRasterState));

        const usize sharedPhaseCount = SharedComputeEmulationPhaseCountForDrawCount(inputs.sharedComputeEmulationPlan.drawCount);
        NWB_ASSERT(IsSupportedSharedComputeEmulationDrawCount(inputs.sharedComputeEmulationPlan.drawCount));
        NWB_ASSERT(sharedPhaseCount <= phaseDesc.sharedPhaseIdentities.size());
        NWB_ASSERT(sharedPhaseCount <= phaseDesc.sharedPhaseLabels.size());
        NWB_ASSERT(sharedPhaseCount <= stageOutputs.sharedComputeEmulationTasks.size());
        Core::GpuTaskId sharedDependency = rasterDependency;
        const Span<const Core::GpuTaskResourceSetUse> materialResourceSetUseSpan(materialResourceSetUses.data(), materialResourceSetUseCount);
        for(usize phaseIndex = 0u; phaseIndex < sharedPhaseCount; ++phaseIndex){
            const b8 isRasterPhase = phaseIndex % s_SharedComputeEmulationPhasesPerDraw != 0u;
            Core::GpuTaskDesc desc;
            MakeSharedComputeEmulationPhaseTaskDesc(
                phaseDesc.sharedPhaseIdentities[phaseIndex],
                phaseDesc.sharedPhaseLabels[phaseIndex],
                sharedDependency,
                isRasterPhase ? Span<const Core::GpuTaskResourceUse>(sharedRasterResourceUses) : Span<const Core::GpuTaskResourceUse>(sharedGenerateResourceUses),
                materialResourceSetUseSpan,
                desc
            );
            typename SharedComputeEmulationTaskT::Payload payload{
                .graphics = graphics,
                .materialSystem = materialSystem,
                .targets = inputs.targets,
                .timingTicket = timingTicket,
                .timing = computeEmulationTiming,
                .frameBindings = frameBindings,
                .plan = inputs.sharedComputeEmulationPlan,
                .drawIndex = phaseIndex / s_SharedComputeEmulationPhasesPerDraw,
                .instanceCount = inputs.sharedComputeEmulationInstanceCount,
                .materialTypedByteCount = inputs.sharedComputeEmulationMaterialTypedByteCount,
                .materialDrawBuffersUploaded = streamsUploaded,
                .materialFrameStatesGraphOwned = rasterPayload.materialFrameStatesGraphOwned,
                .materialGeometryStatesGraphOwned = rasterPayload.materialGeometryStatesGraphOwned,
                .beginTiming = phaseIndex == 0u,
                .finishTiming = phaseIndex + 1u == sharedPhaseCount,
                .phase = isRasterPhase ? SharedComputeEmulationPhase::Raster : SharedComputeEmulationPhase::Generate,
            };
            const auto sharedTaskResult = graph.addTask<SharedComputeEmulationTaskT>(desc, Move(payload));
            if(!sharedTaskResult){
                stageOutputs.sharedComputeEmulationTasks[phaseIndex] = {};
                NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT shared compute-emulation phase"));
                return false;
            }
            stageOutputs.sharedComputeEmulationTasks[phaseIndex] = *sharedTaskResult;
            sharedDependency = stageOutputs.sharedComputeEmulationTasks[phaseIndex];
        }
        stageOutputs.sharedComputeEmulationTaskCount = sharedPhaseCount;
        stageOutputs.rasterTask = sharedDependency;
        return true;
    }

    const Array<Core::GpuTaskId, 2u> rasterDependencies = { rasterDependency, inputs.reusedGeometryProducer };
    const usize rasterDependencyCount = generatedGeometryReused && inputs.reusedGeometryProducer != rasterDependency ? rasterDependencies.size() : 1u;
    Core::GpuTaskDesc rasterDesc;
    rasterDesc
        .setIdentity(phaseDesc.rasterIdentity)
        .setMarkerLabel(phaseDesc.rasterLabel)
        .setScheduling(rasterScheduling)
        .setDependencies({ rasterDependencies.data(), rasterDependencyCount })
        .setResourceUses(resourceUses)
        .setResourceSetUses({ materialResourceSetUseCount != 0u ? materialResourceSetUses.data() : nullptr, materialResourceSetUseCount })
    ;
    const auto rasterTaskResult = graph.addTask<RasterTaskT>(rasterDesc, Move(rasterPayload));
    if(!rasterTaskResult){
        stageOutputs.rasterTask = {};
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT raster graph task"));
        return false;
    }
    stageOutputs.rasterTask = *rasterTaskResult;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

