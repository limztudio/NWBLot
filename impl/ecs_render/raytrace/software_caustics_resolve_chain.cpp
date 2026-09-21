// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/software_caustics_resolve_chain.h>

#include <impl/ecs_render/raytrace/caustics_resolve_chain.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
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
    SoftwareCausticsResolveChainResult& outResult,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace RendererTaskGraphDetail;
    CausticsResolveChainInputs sharedInputs;
    sharedInputs.targets = inputs.targets;
    sharedInputs.geometryTask = inputs.geometryTask;
    sharedInputs.baseScheduling = inputs.baseScheduling;
    sharedInputs.prepare = inputs.prepare;
    sharedInputs.wavelet = inputs.wavelet;
    sharedInputs.secondWavelet = inputs.secondWavelet;
    sharedInputs.thirdWavelet = inputs.thirdWavelet;
    sharedInputs.fourthWavelet = inputs.fourthWavelet;
    sharedInputs.fifthWavelet = inputs.fifthWavelet;
    sharedInputs.upsample = inputs.upsample;
    sharedInputs.stateSources = inputs.stateSources;
    sharedInputs.stateSourceCount = inputs.stateSourceCount;
    sharedInputs.producerDispatched = inputs.producerDispatched;
    sharedInputs.timingTicket = inputs.timingTicket;
    sharedInputs.resolveTiming = inputs.resolveTiming;
    const CausticsResolveChainNaming naming{
        {Name("render.software_caustics.resolve_prepare"), "Software Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-prepare graph task")},
        {Name("render.software_caustics.resolve_wavelet"), "Software Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics first-wavelet graph task")},
        {Name("render.software_caustics.resolve_second_wavelet"), "Software Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics second-wavelet graph task")},
        {Name("render.software_caustics.resolve_third_wavelet"), "Software Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics third-wavelet graph task")},
        {Name("render.software_caustics.resolve_fourth_wavelet"), "Software Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fourth-wavelet graph task")},
        {Name("render.software_caustics.resolve_fifth_wavelet"), "Software Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare deferred software-caustics fifth-wavelet graph task")},
        {Name("render.software_caustics.resolve_upsample"), "Software Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve-upsample graph task")},
        {Name("render.software_caustics.resolve_timing_close"), "Software Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare deferred software-caustics resolve graph task")},
    };
    CausticsResolveChainBuilder sharedBuilder(m_graph, m_raytracingSystem);
    CausticsResolveChainResult sharedResult;
    if(!sharedBuilder.declare(sharedInputs, naming, ComputeQueueRequest(), ComputeQueueRequest(), sharedResult, scratchArena))
        return false;
    outResult.causticResolvePrepareTask = sharedResult.causticResolvePrepareTask;
    outResult.causticResolveWaveletTask = sharedResult.causticResolveWaveletTask;
    outResult.causticResolveSecondWaveletTask = sharedResult.causticResolveSecondWaveletTask;
    outResult.causticResolveThirdWaveletTask = sharedResult.causticResolveThirdWaveletTask;
    outResult.causticResolveFourthWaveletTask = sharedResult.causticResolveFourthWaveletTask;
    outResult.causticResolveFifthWaveletTask = sharedResult.causticResolveFifthWaveletTask;
    outResult.causticResolveUpsampleTask = sharedResult.causticResolveUpsampleTask;
    outResult.softwareCausticsTask = sharedResult.causticsTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

