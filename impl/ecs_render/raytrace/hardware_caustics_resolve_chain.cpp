// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/hardware_caustics_resolve_chain.h>

#include <impl/ecs_render/raytrace/caustics_resolve_chain.h>

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


HardwareCausticsResolveChainBuilder::HardwareCausticsResolveChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererRayTracingSystem& raytracingSystem
)
    : m_graph(graph)
    , m_raytracingSystem(raytracingSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool HardwareCausticsResolveChainBuilder::declare(
    const HardwareCausticsResolveChainInputs& inputs,
    HardwareCausticsResolveChainResult& outResult
){
    using namespace RendererTaskGraphDetail;
    CausticsResolveChainInputs sharedInputs;
    sharedInputs.targets = inputs.targets;
    sharedInputs.geometryTask = inputs.geometryTask;
    sharedInputs.baseScheduling = inputs.baseScheduling;
    sharedInputs.prepare = {inputs.prepareUses, inputs.prepareUseCount};
    sharedInputs.wavelet = {inputs.waveletUses, inputs.waveletUseCount};
    sharedInputs.secondWavelet = {inputs.secondWaveletUses, inputs.secondWaveletUseCount};
    sharedInputs.thirdWavelet = {inputs.thirdWaveletUses, inputs.thirdWaveletUseCount};
    sharedInputs.fourthWavelet = {inputs.fourthWaveletUses, inputs.fourthWaveletUseCount};
    sharedInputs.fifthWavelet = {inputs.fifthWaveletUses, inputs.fifthWaveletUseCount};
    sharedInputs.upsample = {inputs.upsampleUses, inputs.upsampleUseCount};
    sharedInputs.producerDispatched = inputs.producerDispatched;
    sharedInputs.timingTicket = inputs.timingTicket;
    sharedInputs.resolveTiming = inputs.resolveTiming;
    const CausticsResolveChainNaming naming{
        {Name("render.hardware_caustics.resolve_prepare"), "Hardware Caustics Resolve Prepare", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task")},
        {Name("render.hardware_caustics.resolve_wavelet"), "Hardware Caustics Resolve Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task")},
        {Name("render.hardware_caustics.resolve_second_wavelet"), "Hardware Caustics Resolve Second Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task")},
        {Name("render.hardware_caustics.resolve_third_wavelet"), "Hardware Caustics Resolve Third Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task")},
        {Name("render.hardware_caustics.resolve_fourth_wavelet"), "Hardware Caustics Resolve Fourth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task")},
        {Name("render.hardware_caustics.resolve_fifth_wavelet"), "Hardware Caustics Resolve Fifth Wavelet", NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task")},
        {Name("render.hardware_caustics.resolve_upsample"), "Hardware Caustics Resolve Upsample", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task")},
        {Name("render.hardware_caustics.resolve_timing_close"), "Hardware Caustics Resolve Timing Close", NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task")},
    };
    CausticsResolveChainBuilder sharedBuilder(m_graph, m_raytracingSystem);
    CausticsResolveChainResult sharedResult;
    if(!sharedBuilder.declare(sharedInputs, naming, GraphicsPreferredComputeQueueRequest(), GraphicsQueueRequest(), sharedResult))
        return false;
    outResult.causticResolvePrepareTask = sharedResult.causticResolvePrepareTask;
    outResult.causticResolveWaveletTask = sharedResult.causticResolveWaveletTask;
    outResult.causticResolveSecondWaveletTask = sharedResult.causticResolveSecondWaveletTask;
    outResult.causticResolveThirdWaveletTask = sharedResult.causticResolveThirdWaveletTask;
    outResult.causticResolveFourthWaveletTask = sharedResult.causticResolveFourthWaveletTask;
    outResult.causticResolveFifthWaveletTask = sharedResult.causticResolveFifthWaveletTask;
    outResult.causticResolveUpsampleTask = sharedResult.causticResolveUpsampleTask;
    outResult.hardwareCausticsTask = sharedResult.causticsTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
