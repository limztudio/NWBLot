// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/mesh/mesh_view_private.h>

#include <core/graphics/gpu_timing.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererMeshSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshViewSetupGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    // The graphics runtime, the async-prefix timing slot, the timing-ticket slot, and the
    // single-packet span flag are required: mesh-view setup always records the prefix timing
    // decision for this frame. Only the shadow-visibility task stays optional: the queue lookup
    // may be absent when no visibility pass runs. References (not nullable pointers) carry the
    // required bindings so a missing binding fails at declaration time instead of silently
    // returning `false` inside Record.
    struct Payload{
        Core::GraphicsRuntime& graphics;
        Optional<Core::GpuTimingMeasure>& asyncPrefixTiming;
        Core::GpuTimingSubmissionTicket*& timingTicket;
        const bool& asyncPrefixTimingSpansOnePacket;
        const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Update the CPU mirror after packet accepts so rejected recordings still retry.
struct MeshViewUploadCommitGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    // The mesh system and the ready flag are required: the commit always publishes mesh-view
    // readiness for this frame. References (not nullable pointers) carry those bindings so a
    // missing binding fails at declaration time instead of silently returning `false` inside Record.
    struct Payload{
        RendererMeshSystem& meshSystem;
        ECSRenderDetail::MeshViewGpuData viewState;
        bool uploadRequired = false;
        bool& ready;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    )noexcept;
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token);
    static void Discarded(Payload& payload)noexcept;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

