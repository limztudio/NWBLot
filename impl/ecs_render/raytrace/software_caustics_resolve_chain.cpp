// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "software_caustics_resolve_chain.h"

#include <impl/ecs_render/raytrace/caustics_resolve_chain.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SoftwareCausticsResolveChainBuilder::SoftwareCausticsResolveChainBuilder(Core::GpuTaskGraph& graph, RendererRayTracingSystem& raytracingSystem)noexcept
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<SoftwareCausticsResolveChainResult> SoftwareCausticsResolveChainBuilder::declare(const SoftwareCausticsResolveChainInputs& inputs, Core::Alloc::ScratchArena& scratchArena){
    SoftwareCausticsResolveChainResult result{};
    CausticsResolveChainInputs sharedInputs(inputs.targets, inputs.producerDispatched, inputs.timingTicket, inputs.resolveTiming);
    static_cast<CausticsResolveChainStageUseInputs&>(sharedInputs) = inputs;
    sharedInputs.geometryTask = inputs.geometryTask;
    sharedInputs.baseScheduling = inputs.baseScheduling;
    sharedInputs.stateSources = inputs.stateSources;
    sharedInputs.stateSourceCount = inputs.stateSourceCount;
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
    auto sharedResult = sharedBuilder.declare(sharedInputs, naming, scratchArena);
    if(!sharedResult)
        return MakeUnexpected(Failure{});
    static_cast<CausticsResolveChainStageTasks&>(result) = *sharedResult;
    result.softwareCausticsTask = sharedResult->causticsTask;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

