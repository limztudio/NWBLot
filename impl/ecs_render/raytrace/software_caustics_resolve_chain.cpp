// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/software_caustics_resolve_chain.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareCausticsResolveChainBuilder::SoftwareCausticsResolveChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererRayTracingSystem& raytracingSystem
)
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool SoftwareCausticsResolveChainBuilder::declare(
    const SoftwareCausticsResolveChainInputs& inputs,
    SoftwareCausticsResolveChainResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = SoftwareCausticsResolveChainResult{};
    if(
        !inputs.targets
        || !inputs.geometryTask.valid()
        || !inputs.producerDispatched
        || !inputs.timingTicket
        || !inputs.resolveTiming
    )
        return false;
    Core::GpuTaskSchedulingHint resolvePrepareScheduling = inputs.baseScheduling;
    resolvePrepareScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolvePrepareDesc;
    resolvePrepareDesc
    .setIdentity(Name("render.software_caustics.resolve_prepare"))
    .setMarkerLabel("Software Caustics Resolve Prepare")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolvePrepareScheduling)
    .setDependencies(&inputs.geometryTask, 1u)
    .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
    .setResourceUses(inputs.prepareUses, inputs.prepareUseCount)
    ;
    outResult.causticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
    m_graph,
    resolvePrepareDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolvePrepareTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-prepare graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveWaveletScheduling = resolvePrepareScheduling;
    resolveWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveWaveletDesc;
    resolveWaveletDesc
    .setIdentity(Name("render.software_caustics.resolve_wavelet"))
    .setMarkerLabel("Software Caustics Resolve Wavelet")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveWaveletScheduling)
    .setDependencies(&outResult.causticResolvePrepareTask, 1u)
    .setExternalStateSources(inputs.stateSources, inputs.stateSourceCount)
    .setResourceUses(inputs.waveletUses, inputs.waveletUseCount)
    ;
    outResult.causticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
    m_graph,
    resolveWaveletDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveWaveletTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics first-wavelet graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveSecondWaveletScheduling = resolveWaveletScheduling;
    resolveSecondWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveSecondWaveletDesc;
    resolveSecondWaveletDesc
    .setIdentity(Name("render.software_caustics.resolve_second_wavelet"))
    .setMarkerLabel("Software Caustics Resolve Second Wavelet")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveSecondWaveletScheduling)
    .setDependencies(&outResult.causticResolveWaveletTask, 1u)
    .setResourceUses(inputs.secondWaveletUses, inputs.secondWaveletUseCount)
    ;
    outResult.causticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
    m_graph,
    resolveSecondWaveletDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveSecondWaveletTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics second-wavelet graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveThirdWaveletScheduling = resolveSecondWaveletScheduling;
    resolveThirdWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveThirdWaveletDesc;
    resolveThirdWaveletDesc
    .setIdentity(Name("render.software_caustics.resolve_third_wavelet"))
    .setMarkerLabel("Software Caustics Resolve Third Wavelet")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveThirdWaveletScheduling)
    .setDependencies(&outResult.causticResolveSecondWaveletTask, 1u)
    .setResourceUses(inputs.thirdWaveletUses, inputs.thirdWaveletUseCount)
    ;
    outResult.causticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
    m_graph,
    resolveThirdWaveletDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveThirdWaveletTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics third-wavelet graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveFourthWaveletScheduling = resolveThirdWaveletScheduling;
    resolveFourthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveFourthWaveletDesc;
    resolveFourthWaveletDesc
    .setIdentity(Name("render.software_caustics.resolve_fourth_wavelet"))
    .setMarkerLabel("Software Caustics Resolve Fourth Wavelet")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveFourthWaveletScheduling)
    .setDependencies(&outResult.causticResolveThirdWaveletTask, 1u)
    .setResourceUses(inputs.fourthWaveletUses, inputs.fourthWaveletUseCount)
    ;
    outResult.causticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
    m_graph,
    resolveFourthWaveletDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveFourthWaveletTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics fourth-wavelet graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveFifthWaveletScheduling = resolveFourthWaveletScheduling;
    resolveFifthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveFifthWaveletDesc;
    resolveFifthWaveletDesc
    .setIdentity(Name("render.software_caustics.resolve_fifth_wavelet"))
    .setMarkerLabel("Software Caustics Resolve Fifth Wavelet")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveFifthWaveletScheduling)
    .setDependencies(&outResult.causticResolveFourthWaveletTask, 1u)
    .setResourceUses(inputs.fifthWaveletUses, inputs.fifthWaveletUseCount)
    ;
    outResult.causticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
    m_graph,
    resolveFifthWaveletDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveFifthWaveletTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics fifth-wavelet graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveUpsampleScheduling = resolveFifthWaveletScheduling;
    resolveUpsampleScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveUpsampleDesc;
    resolveUpsampleDesc
    .setIdentity(Name("render.software_caustics.resolve_upsample"))
    .setMarkerLabel("Software Caustics Resolve Upsample")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveUpsampleScheduling)
    .setDependencies(&outResult.causticResolveFifthWaveletTask, 1u)
    .setResourceUses(inputs.upsampleUses, inputs.upsampleUseCount)
    ;
    outResult.causticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
    m_graph,
    resolveUpsampleDesc,
    (*inputs.targets),
    inputs.producerDispatched,
    true
    );
    if(!outResult.causticResolveUpsampleTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-upsample graph task"));
    return false;
    }

    Core::GpuTaskSchedulingHint resolveScheduling = resolveUpsampleScheduling;
    resolveScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc resolveDesc;
    resolveDesc
    .setIdentity(Name("render.software_caustics.resolve_timing_close"))
    .setMarkerLabel("Software Caustics Resolve Timing Close")
    .setQueue(ComputeQueueRequest())
    .setScheduling(resolveScheduling)
    .setDependencies(&outResult.causticResolveUpsampleTask, 1u)
    ;
    outResult.softwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
    m_graph,
    resolveDesc,
    (*inputs.timingTicket),
    inputs.producerDispatched,
    inputs.resolveTiming
    );
    if(!outResult.softwareCausticsTask.valid()){
    NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve graph task"));
    return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

