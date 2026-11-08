// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "opaque_compute_emulation_planner.h"

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueComputeEmulationPlanner::OpaqueComputeEmulationPlanner(
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystem,
    DeferredFrameTargets& targets,
    Core::GpuTimingSubmissionTicket*& timingTicket,
    const b8& meshViewSetupReady,
    const b8& sceneShadingSetupReady
)noexcept
    : m_regularPayload(arena, materialSystem, targets, timingTicket, meshViewSetupReady, sceneShadingSetupReady)
    , m_csgPayload(arena, materialSystem, targets, timingTicket, meshViewSetupReady, sceneShadingSetupReady)
{}

void OpaqueComputeEmulationPlanner::capture(
    Core::GpuTaskGraph& graph,
    Core::Alloc::ScratchArena& scratchArena,
    const MaterialPassDrawItemPartitions& opaqueDrawItems,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const b8 hasOpaqueCsgFrameWork,
    const b8 hasCsgFrameGpuWork,
    const b8 gbufferMaterialSampledTexturesCollected,
    ECSRenderDetail::GbufferGraphTask::Payload& gbufferPayload,
    Optional<Core::GpuTimingMeasure>& opaqueRegularSharedComputeEmulationTiming
){
    using namespace RendererTaskGraphDetail;

    // Generated-vertex buffers persist per mesh, so only the alias-free regular opaque case hoists compute ahead of raster.
    m_regularPayload.frameBindings = frameBindings;
    const b8 opaqueComputeEmulationPlanCaptured = gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_regularPayload.plan.capture(opaqueDrawItems.regular, scratchArena)
    ;
    if(opaqueComputeEmulationPlanCaptured){
        const auto opaqueComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            m_regularPayload.plan,
            scratchArena,
            Name("render.graphics_prefix.opaque_compute_emulation.outputs"),
            "Opaque Compute Emulation Outputs"
        );
        m_regularOutputOwned = opaqueComputeEmulationOutputSetResult.has_value();
        if(opaqueComputeEmulationOutputSetResult)
            m_regularOutputSet = *opaqueComputeEmulationOutputSetResult;
    }
    if(opaqueComputeEmulationPlanCaptured && !m_regularOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque compute-emulation output states"));
    gbufferPayload.regularComputeEmulationOutputStatesGraphOwned = m_regularOutputOwned;

    // A persistent generated-vertex buffer is normally a local dispatch/raster bridge.
    // Handle the small alias classes explicitly: two through five regular opaque compute items sharing one frozen output and heap slot.
    // Opaque CSG remains out of scope so no CSG producer/raster phase can observe this alternation.
    const b8 opaqueSharedComputeEmulationPlanCaptured =
        !hasOpaqueCsgFrameWork
        && !m_regularOutputOwned
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_sharedPlan.capture(opaqueDrawItems.regular, s_SharedComputeEmulationMaximumDrawCount)
    ;
    if(opaqueSharedComputeEmulationPlanCaptured){
        const auto opaqueSharedComputeEmulationOutputResult = GatherRegularSharedComputeEmulationResource(graph, m_sharedPlan, "Opaque Shared Compute Emulation Output");
        m_sharedOutputOwned = opaqueSharedComputeEmulationOutputResult.has_value();
        if(opaqueSharedComputeEmulationOutputResult)
            m_sharedOutput = *opaqueSharedComputeEmulationOutputResult;
    }
    if(opaqueSharedComputeEmulationPlanCaptured && !m_sharedOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned shared opaque compute-emulation output state"));
    gbufferPayload.regularSharedComputeEmulationDrawsGraphOwned = m_sharedOutputOwned;
    gbufferPayload.regularSharedComputeEmulationTiming = m_sharedOutputOwned ? &opaqueRegularSharedComputeEmulationTiming : nullptr;

    m_csgPayload.frameBindings = frameBindings;
    const b8 opaqueCsgReceiverComputeEmulationPlanCaptured = hasOpaqueCsgFrameWork
        && hasCsgFrameGpuWork
        && gbufferPayload.materialFrameStatesGraphOwned
        && gbufferPayload.materialGeometryStatesGraphOwned
        && gbufferMaterialSampledTexturesCollected
        && m_csgPayload.plan.capture(opaqueDrawItems.csgReceiverSurface, opaqueDrawItems.regular, csgFrameData, scratchArena)
    ;
    if(opaqueCsgReceiverComputeEmulationPlanCaptured){
        const auto opaqueCsgReceiverComputeEmulationOutputSetResult = GatherImportedOutputBufferResourceSet(
            graph,
            m_csgPayload.plan,
            scratchArena,
            Name("render.graphics_prefix.opaque_csg_receiver_compute_emulation.outputs"),
            "Opaque CSG Receiver Compute Emulation Outputs"
        );
        m_csgOutputOwned = opaqueCsgReceiverComputeEmulationOutputSetResult.has_value();
        if(opaqueCsgReceiverComputeEmulationOutputSetResult)
            m_csgOutputSet = *opaqueCsgReceiverComputeEmulationOutputSetResult;
    }
    if(opaqueCsgReceiverComputeEmulationPlanCaptured && !m_csgOutputOwned)
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare graph-owned opaque CSG receiver compute-emulation output states"));
    gbufferPayload.csgReceiverComputeEmulationOutputStatesGraphOwned = m_csgOutputOwned;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

