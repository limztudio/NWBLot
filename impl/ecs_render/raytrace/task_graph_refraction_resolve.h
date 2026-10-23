// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>
#include <impl/ecs_render/kernel/timing_names.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>
#include <core/task/gpu/capture/command_ir.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct RefractionClearTileArgumentsGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer };
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Core::BufferHandle destination;
        Core::GpuGraphResourceId resource;
    };

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(context.commandIrCapture && !context.commandIrCapture->captureClearBuffer(context.task, context.packet, context.queue, payload.resource, 0u))
            return false;
        commandList.endRenderPass();
        Core::GpuTimingMeasure timing(payload.graphics.gpuTiming(), RendererGpuTimingScope::s_RefractionClearTileArguments, payload.graphics.getDevice(), commandList);

        commandList.clearBufferUInt(*payload.destination, 0u);
        return true;
    }
};


// Diagnostic work stays on the resolve's primary Graphics queue. Shared counter writes and
// the bounded copy serialize there; accepted readbacks never introduce a renderer wait.
struct RefractionClearStatisticsGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer, true };
    struct Payload{
        Core::BufferHandle destination;
        Core::GpuGraphResourceId resource;
    };

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.destination)
            return false;
        if(context.commandIrCapture && !context.commandIrCapture->captureClearBuffer(context.task, context.packet, context.queue, payload.resource, 0u))
            return false;
        commandList.endRenderPass();
        commandList.clearBufferUInt(*payload.destination, 0u);
        return !commandList.commandRecordingFailed();
    }
};

struct RefractionStatisticsReadbackGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Transfer, true };
    struct Payload{
        Core::BufferHandle source;
        Core::BufferHandle destination;
        Core::GpuGraphResourceId sourceResource;
        Core::GpuGraphResourceId destinationResource;
        RefractionStatisticsReservation reservation;
        const bool* hardwarePreparationReady = nullptr;
        bool hardwareEnabled = false;
    };

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        if(!payload.source || !payload.destination || !payload.reservation.valid())
            return false;
        if(context.commandIrCapture && !context.commandIrCapture->captureCopyBuffer(context.task, context.packet, context.queue, payload.sourceResource, 0u, payload.destinationResource, 0u, NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE))
            return false;
        commandList.endRenderPass();
        commandList.copyBuffer(*payload.destination, 0u, *payload.source, 0u, NWB_REFRACTION_DIAGNOSTIC_COUNTER_SIZE);
        return !commandList.commandRecordingFailed();
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept{
        const bool hardwareReady = payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        payload.reservation.accept(token, hardwareReady);
    }

    static void Discarded(Payload& payload)noexcept{
        payload.reservation.discard();
    }
};


struct RefractionClassifyTilesGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };
    struct Payload{
        RendererRayTracingSystem& system;
        const DeferredFrameTargets& targets;
        RayTracingRefractionGraphResources resources;
    };

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        return payload.system.recordRefractionTileClassification(commandList, payload.targets, payload.resources);
    }
};


struct RefractionResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The raytracing system and the frame targets are required: the resolve always records against this frame's targets.
    // The readiness flag and dispatch latches stay optional: the screen fallback route declares no hardware preparation, and only the traced route publishes a dispatch diagnostic.
    struct Payload{
        RendererRayTracingSystem& system;
        const DeferredFrameTargets& targets;
        RayTracingRefractionGraphResources resources;
        const bool* hardwarePreparationReady = nullptr;
        bool* dispatchLogged = nullptr;
        bool* screenFallbackDispatchLogged = nullptr;
    };


public:
    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        RayTracingRefractionGraphResources resources = payload.resources;
        if(resources.usesHardwareTrace && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)){
            // Shared preparation can accept a nonfatal TLAS miss. Use the prepared screen pipeline, whose inputs are a subset of this task's declared resources, instead of tracing a stale/unbuilt TLAS.
            resources.pipeline = resources.screenFallbackPipeline;
            resources.densePipeline = resources.denseScreenFallbackPipeline;
            resources.usesHardwareTrace = false;
            resources.tlasHeapHandle = Core::GpuDescriptorHandle::Invalid();
            resources.materialContextSlotsHeapSlot = 0u;
        }
        return payload.system.recordRefractionResolve(commandList, payload.targets, resources);
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        // Only an accepted packet may publish a dispatch diagnostic. Rejected recording retries leave the latch untouched, and the resource-selection/recording path stays free of renderer state.
        const bool usedHardware = payload.resources.usesHardwareTrace && payload.hardwarePreparationReady && *payload.hardwarePreparationReady;
        if(usedHardware)
            payload.system.confirmRayTraceContextReadSubmission(token);
        bool* const dispatchLogged = payload.resources.usesHardwareTrace && !usedHardware ? payload.screenFallbackDispatchLogged : payload.dispatchLogged;
        if(dispatchLogged && !*dispatchLogged){
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("AVBOIT refraction resolve: {}"), usedHardware ? NWB_TEXT("hardware") : NWB_TEXT("screen-space"));
            *dispatchLogged = true;
        }
    }
};

// Diagnostic counter reuse has an explicit physical-queue contract across frame rejection.
// Keep ordinary resolve scheduling unchanged when diagnostics are disabled.
struct RefractionDiagnosticResolveGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute, true };
    using Payload = RefractionResolveGraphTask::Payload;

    static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        return RefractionResolveGraphTask::Record(payload, commandList, context);
    }

    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        RefractionResolveGraphTask::Accepted(payload, token);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

