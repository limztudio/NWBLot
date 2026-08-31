// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/raytrace/raytracing_system.h>

#include <core/graphics/task_graph/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ShadowPrepareGraphTask{
    struct Payload{
        Core::Graphics* graphics = nullptr;
        RendererRayTracingSystem* raytracingSystem = nullptr;
        ShadowPreparationOutcome* outcome = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
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
        bool deferHybridSoftwareTail = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token);
    static void discarded(Payload& payload);
};


// Pure-software preparation shares scratch between every frozen mesh build. Keep each typed sentinel setup next to
// its matching compute callback so a later mesh cannot clear a prior mesh's sort/payload rendezvous state.
struct ShadowPrepareSoftwareBvhBuildGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        PreparedMeshSwBvhBuild build;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
};


// Hybrid HW-to-SW shadow preparation keeps the established opaque-HW fallback transaction, but records its
// software continuation after the hardware build as an explicit packet-local callback. The compiler must retain it
// in Shadow Preparation's accepting Graphics packet: the tail can restore the frozen hardware material context and
// its final resource state joins the same persistent handoff as the preceding BLAS/TLAS work.
struct ShadowPrepareHybridSoftwareTailGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        bool* hardwarePreparationReady = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool shadowMaterialContextBatchGraphOwned = false;
        bool sceneBvhBatchGraphOwned = false;
        bool meshSwBvhBuildsGraphOwned = false;
        bool meshSwBvhInputStatesGraphOwned = false;
        Core::GpuUploadBlobId hybridHardwareFallbackInstanceMaterialBlob;
        Core::GpuUploadBlobId hybridHardwareFallbackInstanceBlob;
        Core::GpuUploadBlobId hybridHardwareFallbackMaterialTypedBlob;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    );
    static void discarded(Payload& payload);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

