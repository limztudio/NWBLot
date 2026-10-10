// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <core/common/log.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RefractionResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The raytracing system and the frame targets are required: the resolve always records against
    // this frame's targets. The readiness flag and dispatch latches stay optional: the screen
    // fallback route declares no hardware preparation, and only the traced route publishes a dispatch
    // diagnostic. References (not nullable pointers) carry the required bindings so a missing binding
    // fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererRayTracingSystem& system;
        const DeferredFrameTargets& targets;
        RayTracingRefractionGraphResources resources;
        const bool* hardwarePreparationReady = nullptr;
        bool* dispatchLogged = nullptr;
        bool* screenFallbackDispatchLogged = nullptr;
    };

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){

        RayTracingRefractionGraphResources resources = payload.resources;
        if(resources.usesHardwareTrace && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)){
            // Shared preparation can accept a nonfatal TLAS miss. Use the already prepared screen pipeline, whose
            // inputs are a subset of this task's declared resources, instead of tracing a stale/unbuilt TLAS.
            resources.pipeline = resources.screenFallbackPipeline;
            resources.usesHardwareTrace = false;
            resources.tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
            resources.materialContextSlotsHeapSlot = 0u;
        }
        return payload.system.recordRefractionResolve(commandList, payload.targets, resources);
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        // Only an accepted packet may publish a dispatch diagnostic. Rejected recording retries leave the latch
        // untouched, and the resource-selection/recording path remains free of mutable renderer state.
        const bool usedHardware = payload.resources.usesHardwareTrace && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        if(usedHardware)
            payload.system.confirmCsgTraceContextReadSubmission(token);
        bool* const dispatchLogged = payload.resources.usesHardwareTrace && !usedHardware ? payload.screenFallbackDispatchLogged : payload.dispatchLogged;
        if(dispatchLogged && !*dispatchLogged){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AVBOIT refraction resolve: {}"), usedHardware ? NWB_TEXT("hardware") : NWB_TEXT("screen-space"));
            *dispatchLogged = true;
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

