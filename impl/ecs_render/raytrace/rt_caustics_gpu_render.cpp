// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <impl/ecs_render/optics/coincident_volumes.h>
#include <core/task/gpu/compiled_graph.h>
#include <global/algorithm.h>
#include <impl/ecs_render/raytrace/rt_caustics_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererRayTracingSystem::renderGpuBvhCaustics(
    Core::CommandList& commandList,
    const ECSRenderDetail::MeshViewBufferSnapshot& meshView,
    DeferredFrameTargets& targets,
    Optional<Core::GpuTimingMeasure>* const causticPhotonTiming
){
    // Software photon producer runs before deferred lighting.

    if(!hasCausticWork(meshView))
        return false;
    NWB_ASSERT(meshView.bindingValid());
    NWB_ASSERT(targets.bindless.valid());
    const f32 temporalDecay = causticTemporalDecay();
    const auto& pipeline = m_lightSpaceShadow.m_csg.snapshot.hasCsg ? m_rayTracingState.m_swCausticCsgPipeline : m_rayTracingState.m_swCausticPipeline;
    if(
        !pipeline
        || !m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.valid()
        || !causticResolveResourcesReady(targets, temporalDecay)
    )
        return false;
    const u32 temporalPhaseCount = causticTemporalPhaseCount();
    const CausticPhotonBudget photonBudget = MakeCausticPhotonBudget(m_causticQualitySettings, s_CausticSwPhotonGridSide, temporalPhaseCount);
    const u32 photonCount = photonBudget.photonsPerFrame;

    const auto recordPhotons = [&](){
        const CausticPhotonPushConstants pushConstants = BuildCausticPhotonPushConstants(
            targets,
            meshView,
            m_rayTracingState,
            photonBudget,
            m_rayTracingState.m_sceneBvhInstanceCount,
            static_cast<u32>(m_graphics.getFrameIndex()),
            temporalPhaseCount
        );

        Core::ComputeState computeState;
        computeState.setPipeline(pipeline.get());
        commandList.setComputeState(computeState);
        Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
        NWB_ASSERT(heap.isInitialized());
        heap.bindCompute(commandList, *pipeline.get());
        commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        commandList.dispatch(DivideUp(photonCount, static_cast<u32>(NWB_CAUSTIC_SW_GROUP_SIZE)), 1u, 1u);
        advanceCausticTemporalReuse();
    };
    if(causticPhotonTiming && causticPhotonTiming->has_value()){
        recordPhotons();
        causticPhotonTiming->value().finishTiming(commandList);
        causticPhotonTiming->reset();
    }
    else{
        Core::GpuTimingMeasure timing(
            m_graphics.gpuTiming(),
            RendererGpuTimingScope::s_CausticPhotons,
            m_graphics.getDevice(),
            commandList
        );
        recordPhotons();
    }

    if(!m_rayTracingState.m_swCausticDispatchLogged){
        m_rayTracingState.m_swCausticDispatchLogged = true;
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: dispatched software caustic producer ({} photons/frame, {} temporal phases, {} full-grid budget, {} caustic lights, {} refractive instances)")
            , static_cast<u64>(photonCount)
            , static_cast<u64>(temporalPhaseCount)
            , static_cast<u64>(photonBudget.fullGridCount)
            , static_cast<u64>(m_rayTracingState.m_causticLightCount)
            , static_cast<u64>(m_rayTracingState.m_causticRefractiveInstanceCount)
        );
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

