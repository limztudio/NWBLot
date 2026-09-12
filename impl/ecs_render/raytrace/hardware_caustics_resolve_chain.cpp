// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/hardware_caustics_resolve_chain.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
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
    outResult = HardwareCausticsResolveChainResult{};
    if(
        !inputs.targets
        || !inputs.geometryTask.valid()
        || !inputs.producerDispatched
        || !inputs.timingTicket
        || !inputs.resolveTiming
    )
        return false;
    Core::GpuTaskSchedulingHint hardwareResolvePrepareScheduling = inputs.baseScheduling;
    hardwareResolvePrepareScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolvePrepareDesc;
    hardwareResolvePrepareDesc
        .setIdentity(Name("render.hardware_caustics.resolve_prepare"))
        .setMarkerLabel("Hardware Caustics Resolve Prepare")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolvePrepareScheduling)
        .setDependencies(&inputs.geometryTask, 1u)
        .setResourceUses(inputs.prepareUses, inputs.prepareUseCount)
    ;
    outResult.causticResolvePrepareTask = m_raytracingSystem.declareCausticResolvePrepareTask(
        m_graph,
        hardwareResolvePrepareDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolvePrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-prepare graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveWaveletScheduling = hardwareResolvePrepareScheduling;
    hardwareResolveWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveWaveletDesc;
    hardwareResolveWaveletDesc
        .setIdentity(Name("render.hardware_caustics.resolve_wavelet"))
        .setMarkerLabel("Hardware Caustics Resolve Wavelet")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveWaveletScheduling)
        .setDependencies(&outResult.causticResolvePrepareTask, 1u)
        .setResourceUses(inputs.waveletUses, inputs.waveletUseCount)
    ;
    outResult.causticResolveWaveletTask = m_raytracingSystem.declareCausticResolveWaveletTask(
        m_graph,
        hardwareResolveWaveletDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics first-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveSecondWaveletScheduling = hardwareResolveWaveletScheduling;
    hardwareResolveSecondWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveSecondWaveletDesc;
    hardwareResolveSecondWaveletDesc
        .setIdentity(Name("render.hardware_caustics.resolve_second_wavelet"))
        .setMarkerLabel("Hardware Caustics Resolve Second Wavelet")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveSecondWaveletScheduling)
        .setDependencies(&outResult.causticResolveWaveletTask, 1u)
        .setResourceUses(
            inputs.secondWaveletUses,
            inputs.secondWaveletUseCount
        )
    ;
    outResult.causticResolveSecondWaveletTask = m_raytracingSystem.declareCausticResolveSecondWaveletTask(
        m_graph,
        hardwareResolveSecondWaveletDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveSecondWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics second-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveThirdWaveletScheduling = hardwareResolveSecondWaveletScheduling;
    hardwareResolveThirdWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveThirdWaveletDesc;
    hardwareResolveThirdWaveletDesc
        .setIdentity(Name("render.hardware_caustics.resolve_third_wavelet"))
        .setMarkerLabel("Hardware Caustics Resolve Third Wavelet")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveThirdWaveletScheduling)
        .setDependencies(&outResult.causticResolveSecondWaveletTask, 1u)
        .setResourceUses(
            inputs.thirdWaveletUses,
            inputs.thirdWaveletUseCount
        )
    ;
    outResult.causticResolveThirdWaveletTask = m_raytracingSystem.declareCausticResolveThirdWaveletTask(
        m_graph,
        hardwareResolveThirdWaveletDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveThirdWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics third-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveFourthWaveletScheduling = hardwareResolveThirdWaveletScheduling;
    hardwareResolveFourthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveFourthWaveletDesc;
    hardwareResolveFourthWaveletDesc
        .setIdentity(Name("render.hardware_caustics.resolve_fourth_wavelet"))
        .setMarkerLabel("Hardware Caustics Resolve Fourth Wavelet")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveFourthWaveletScheduling)
        .setDependencies(&outResult.causticResolveThirdWaveletTask, 1u)
        .setResourceUses(
            inputs.fourthWaveletUses,
            inputs.fourthWaveletUseCount
        )
    ;
    outResult.causticResolveFourthWaveletTask = m_raytracingSystem.declareCausticResolveFourthWaveletTask(
        m_graph,
        hardwareResolveFourthWaveletDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveFourthWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fourth-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveFifthWaveletScheduling = hardwareResolveFourthWaveletScheduling;
    hardwareResolveFifthWaveletScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveFifthWaveletDesc;
    hardwareResolveFifthWaveletDesc
        .setIdentity(Name("render.hardware_caustics.resolve_fifth_wavelet"))
        .setMarkerLabel("Hardware Caustics Resolve Fifth Wavelet")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveFifthWaveletScheduling)
        .setDependencies(&outResult.causticResolveFourthWaveletTask, 1u)
        .setResourceUses(
            inputs.fifthWaveletUses,
            inputs.fifthWaveletUseCount
        )
    ;
    outResult.causticResolveFifthWaveletTask = m_raytracingSystem.declareCausticResolveFifthWaveletTask(
        m_graph,
        hardwareResolveFifthWaveletDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveFifthWaveletTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics fifth-wavelet graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveUpsampleScheduling = hardwareResolveFifthWaveletScheduling;
    hardwareResolveUpsampleScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveUpsampleDesc;
    hardwareResolveUpsampleDesc
        .setIdentity(Name("render.hardware_caustics.resolve_upsample"))
        .setMarkerLabel("Hardware Caustics Resolve Upsample")
        .setQueue(GraphicsPreferredComputeQueueRequest())
        .setScheduling(hardwareResolveUpsampleScheduling)
        .setDependencies(&outResult.causticResolveFifthWaveletTask, 1u)
        .setResourceUses(inputs.upsampleUses, inputs.upsampleUseCount)
    ;
    outResult.causticResolveUpsampleTask = m_raytracingSystem.declareCausticResolveUpsampleTask(
        m_graph,
        hardwareResolveUpsampleDesc,
        (*inputs.targets),
        inputs.producerDispatched,
        true
    );
    if(!outResult.causticResolveUpsampleTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve-upsample graph task"));
        return false;
    }

    Core::GpuTaskSchedulingHint hardwareResolveScheduling = hardwareResolveUpsampleScheduling;
    hardwareResolveScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc hardwareResolveDesc;
    hardwareResolveDesc
        .setIdentity(Name("render.hardware_caustics.resolve_timing_close"))
        .setMarkerLabel("Hardware Caustics Resolve Timing Close")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(hardwareResolveScheduling)
        .setDependencies(&outResult.causticResolveUpsampleTask, 1u)
    ;
    outResult.hardwareCausticsTask = m_raytracingSystem.declareCausticResolveTask(
        m_graph,
        hardwareResolveDesc,
        (*inputs.timingTicket),
        inputs.producerDispatched,
        inputs.resolveTiming
    );
    if(!outResult.hardwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics resolve graph task"));
        return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

