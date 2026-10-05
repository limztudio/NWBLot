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

    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        ShadowPreparationOutcome* outcome = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool deferredBindlessSlotsWereUploaded = false;
        bool currentBindlessSlotsGraphOwned = false;
        bool shadowMaterialContextBatchGraphOwned = false;
        bool sceneBvhBatchGraphOwned = false;
        bool sceneTlasBuildGraphOwned = false;
        bool meshBlasBuildsGraphOwned = false;
        bool meshBlasGeometryBuildInputStatesGraphOwned = false;
        bool meshSwBvhBuildsGraphOwned = false;
        bool preparedMeshSwBvhBuildsRecordedByGraph = false;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void Accepted(Payload& payload, const Core::QueueSubmissionToken& token);
    static void Discarded(Payload& payload);
};


// Pure-software preparation shares scratch between every frozen mesh build. Keep each typed sentinel setup next to
// its matching compute callback so a later mesh cannot clear a prior mesh's sort/payload rendezvous state.
struct ShadowPrepareSoftwareBvhBuildGraphTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = { Core::GpuQueueCapability::Compute };

    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        PreparedMeshSwBvhBuild build;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool Record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

