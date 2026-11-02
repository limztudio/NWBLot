// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_render/kernel/task_graph_queue_lookup.h>
#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PostGbufferNormalizeGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    // The raytracing system, timing-ticket slot, and shadow task slot are required.
    // Async timing stays optional: timing spans one packet on the compute route.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const Core::GpuTaskId& shadowVisibilityTask;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context){
        const Core::GpuPhysicalQueueInfo* const shadowVisibilityQueue = ECSRenderDetail::QueueForTask(context, payload.shadowVisibilityTask);
        if(!shadowVisibilityQueue)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        // Explicit uses below lower G-buffer, scene, and trace-geometry states first.
        if(shadowVisibilityQueue->queueClass == Core::CommandQueue::Compute && payload.asyncPrefixTiming && *payload.asyncPrefixTiming){
            (*payload.asyncPrefixTiming)->finishTiming(commandList);
            payload.asyncPrefixTiming->reset();
        }
        return true;
    }

    static void Accepted(Payload& payload, [[maybe_unused]] const Core::QueueSubmissionToken& token)noexcept{
        payload.raytracingSystem.confirmPreparedShadowTraceGeometryNormalization();
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

