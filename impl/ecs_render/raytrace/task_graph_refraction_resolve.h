#pragma once

#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <core/common/log.h>
#include <core/task/gpu/task_graph.h>

NWB_IMPL_BEGIN

namespace RendererTaskGraphDetail{

struct RefractionResolveGraphTask{
    struct Payload{
        RendererRayTracingSystem* system = nullptr;
        const DeferredFrameTargets* targets = nullptr;
        RayTracingRefractionGraphResources resources;
        const bool* hardwarePreparationReady = nullptr;
        bool* dispatchLogged = nullptr;
        bool* screenFallbackDispatchLogged = nullptr;
    };

    static bool record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.system || !payload.targets)
            return false;
        RayTracingRefractionGraphResources resources = payload.resources;
        if(resources.usesHardwareTrace && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)){
            // Shared preparation can accept a nonfatal TLAS miss. Use the already prepared screen pipeline, whose
            // inputs are a subset of this task's declared resources, instead of tracing a stale/unbuilt TLAS.
            resources.pipeline = resources.screenFallbackPipeline;
            resources.usesHardwareTrace = false;
            resources.tlasHeapHandle = Core::GpuDescriptorHandle::invalid();
            resources.materialContextSlotsHeapSlot = 0u;
        }
        return payload.system->recordRefractionResolve(commandList, *payload.targets, resources);
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken&){
        // Only an accepted packet may publish a dispatch diagnostic. Rejected recording retries leave the latch
        // untouched, and the resource-selection/recording path remains free of mutable renderer state.
        const bool usedHardware = payload.resources.usesHardwareTrace
            && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        bool* const dispatchLogged = payload.resources.usesHardwareTrace && !usedHardware
            ? payload.screenFallbackDispatchLogged : payload.dispatchLogged;
        if(dispatchLogged && !*dispatchLogged){
            NWB_LOGGER_INFO(NWB_TEXT("AVBOIT refraction resolve: {}"),
                usedHardware ? NWB_TEXT("hardware") : NWB_TEXT("screen-space"));
            *dispatchLogged = true;
        }
    }
};

}

NWB_IMPL_END
