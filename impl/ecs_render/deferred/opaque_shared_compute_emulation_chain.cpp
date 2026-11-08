// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_shared_compute_emulation_chain.h"

#include <impl/ecs_render/deferred/graph_resource_import_builder.h>
#include <impl/ecs_render/deferred/presentation_settings.adhd.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<OpaqueSharedComputeEmulationChainResult> OpaqueSharedComputeEmulationChainBuilder::declare(const OpaqueSharedComputeEmulationChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena, const Span<Core::GpuTaskId> tasks){
    using namespace RendererTaskGraphDetail;

    const DeferredGraphResourceImportResult& resources = inputs.resources;
    const Array<Core::GpuTaskResourceUse, 4u> generateResourceUses = {
        ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer),
        ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange),
        ReadBufferUse(resources.materialTyped, inputs.materialTypedRange),
        WriteUse(inputs.sharedOutput, Core::ResourceStates::UnorderedAccess),
    };

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> rasterResourceUses{ scratchArena };
    rasterResourceUses.reserve(inputs.objectGeometryReads.size() + s_OpaqueSharedRasterResourceUseCapacity);
    rasterResourceUses.insert(rasterResourceUses.end(), inputs.objectGeometryReads.begin(), inputs.objectGeometryReads.end());
    rasterResourceUses.push_back(ReadUse(resources.meshView, Core::ResourceStates::ConstantBuffer));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialInstances, inputs.materialInstanceRange));
    rasterResourceUses.push_back(ReadBufferUse(resources.materialTyped, inputs.materialTypedRange));
    rasterResourceUses.push_back(ReadUse(inputs.sharedOutput, ECSRenderDetail::s_ObjectGeometryRasterState));
    rasterResourceUses.push_back(ReadWriteUse(resources.albedo, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.normal, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.worldPosition, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.specularRoughness, Core::ResourceStates::RenderTarget));
    rasterResourceUses.push_back(ReadWriteUse(resources.depth, Core::ResourceStates::DepthWrite));

    Array<Core::GpuTaskResourceSetUse, s_OpaqueSharedResourceSetUseCapacity> resourceSetUses = {};
    usize resourceSetUseCount = 0u;
    resourceSetUses[resourceSetUseCount++] = inputs.materialGeometrySetUse;
    if(inputs.materialSampledTextureSetUse.resourceSet.valid())
        resourceSetUses[resourceSetUseCount++] = inputs.materialSampledTextureSetUse;

    NWB_DEFINE_SHARED_COMPUTE_EMULATION_PHASE_TABLES("render.graphics_prefix.opaque", "Opaque");
    const usize phaseCount = SharedComputeEmulationPhaseCountForDrawCount(inputs.plan.drawCount);
    if(!IsSupportedSharedComputeEmulationPhaseCount(phaseCount) || phaseCount > tasks.size()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: invalid shared opaque compute-emulation phase count"));
        return MakeUnexpected(Failure{});
    }
    Core::GpuTaskId dependency = inputs.gbufferTask;
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUseSpan(resourceSetUses.data(), resourceSetUseCount);
    for(usize phaseIndex = 0u; phaseIndex < phaseCount; ++phaseIndex){
        const b8 isRasterPhase = phaseIndex % s_SharedComputeEmulationPhasesPerDraw != 0u;
        const auto phaseTask = declarePhase(
            inputs,
            sharedPhaseIdentities[phaseIndex],
            sharedPhaseMarkers[phaseIndex],
            dependency,
            isRasterPhase ? SharedComputeEmulationPhase::Raster : SharedComputeEmulationPhase::Generate,
            phaseIndex / s_SharedComputeEmulationPhasesPerDraw,
            phaseIndex + 1u == phaseCount,
            isRasterPhase ? Span<const Core::GpuTaskResourceUse>(rasterResourceUses) : Span<const Core::GpuTaskResourceUse>(generateResourceUses),
            resourceSetUseSpan
        );
        if(!phaseTask){
            tasks[phaseIndex] = {};
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shared opaque compute-emulation phase"));
            return MakeUnexpected(Failure{});
        }
        tasks[phaseIndex] = *phaseTask;
        dependency = *phaseTask;
    }
    return OpaqueSharedComputeEmulationChainResult(dependency, phaseCount);
}


Expected<Core::GpuTaskId> OpaqueSharedComputeEmulationChainBuilder::declarePhase(
    const OpaqueSharedComputeEmulationChainInputs& inputs,
    const Name& identity,
    const AStringView markerLabel,
    const Core::GpuTaskId& dependency,
    const SharedComputeEmulationPhase::Enum phase,
    const usize drawIndex,
    const b8 finishTiming,
    const Span<const Core::GpuTaskResourceUse> resourceUses,
    const Span<const Core::GpuTaskResourceSetUse> resourceSetUses
){
    Core::GpuTaskDesc desc;
    RendererTaskGraphDetail::MakeSharedComputeEmulationPhaseTaskDesc(identity, markerLabel, dependency, resourceUses, resourceSetUses, desc);
    ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask::Payload payload{
        m_materialSystem,
        inputs.targets,
        inputs.timingTicket,
        inputs.meshViewSetupReady,
        inputs.sceneShadingSetupReady,
        inputs.sharedComputeEmulationTiming,
        inputs.frameBindings,
        {},
    };
    payload.plan = inputs.plan;
    payload.drawIndex = drawIndex;
    payload.instanceCount = inputs.instanceCount;
    payload.materialTypedByteCount = inputs.materialTypedByteCount;
    payload.materialDrawBuffersUploaded = inputs.materialDrawBuffersUploaded;
    payload.materialFrameStatesGraphOwned = inputs.materialFrameStatesGraphOwned;
    payload.materialGeometryStatesGraphOwned = inputs.materialGeometryStatesGraphOwned;
    payload.finishTiming = finishTiming;
    payload.phase = phase;
    return m_graph.addTask<ECSRenderDetail::OpaqueRegularSharedComputeEmulationGraphTask>(desc, Move(payload));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

