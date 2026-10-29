// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute | Core::GpuQueueCapability::Transfer, true };

    // All bindings required: the prepare task always preflights this frame's shadow resources.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        ShadowPreparationOutcome& outcome;
        DeferredFrameTargets& targets;
        Core::GpuTimingSubmissionTicket& timingTicket;
        b8 deferredBindlessSlotsWereUploaded = false;
        b8 currentBindlessSlotsGraphOwned = false;
        b8 shadowMaterialContextBatchGraphOwned = false;
        b8 sceneBvhBatchGraphOwned = false;
        b8 sceneTlasBuildGraphOwned = false;
        b8 meshBlasBuildsGraphOwned = false;
        b8 meshBlasGeometryBuildInputStatesGraphOwned = false;
        b8 meshSwBvhBuildsGraphOwned = false;
        b8 preparedMeshSwBvhBuildsRecordedByGraph = false;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token)noexcept;
    static void Discarded(Payload& payload);
};


// Software preparation shares scratch across frozen mesh builds. Keep each sentinel by its callback so meshes never clear each other.
struct ShadowPrepareSoftwareBvhBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    // The raytracing system and the timing ticket are required: every enqueued software BVH build records against this frame's ticket.
    struct Payload{
        RendererRayTracingSystem& raytracingSystem;
        Core::GpuTimingSubmissionTicket& timingTicket;
        PreparedMeshSwBvhBuild build;
    };


public:
    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext& context);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

