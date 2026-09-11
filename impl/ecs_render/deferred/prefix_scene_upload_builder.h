// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/shared/renderer_push_constants_private.h>
#include <impl/assets/graphics/scene/binding_slots.h>

#include <core/graphics/gpu_timing.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererDeferredSystem;
class RendererMeshSystem;
class RendererRayTracingSystem;


// Graphics-prefix scene uploads own mesh-view plus scene light/shading upload chain.
struct PrefixSceneUploadInputs{
    const ECSRenderDetail::MeshViewGpuData* meshViewState = nullptr;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuTaskId shadowPrepareTask;
    f32 meshViewAspectRatio = 1.f;
    bool meshViewUploadRequired = false;
    Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
    Core::GpuTimingSubmissionTicket** meshViewSetupTimingTicket = nullptr;
    Core::GpuTimingSubmissionTicket** sceneShadingSetupTimingTicket = nullptr;
    const bool* asyncPrefixTimingSpansOnePacket = nullptr;
    const Core::GpuTaskId* shadowVisibilityTask = nullptr;
    bool* meshViewSetupReady = nullptr;
    bool* sceneShadingSetupReady = nullptr;
    u64* outSceneLightingContentHash = nullptr;
};

struct PrefixSceneUploadResult{
    Core::GpuTaskId meshViewSetupTask;
    Core::GpuTaskId sceneShadingSetupTask;
    Core::GpuTaskId tailTask;
    RayTracingLightingClassification lightingClassification;
    ECSRenderDetail::SceneLightGpuData lightData[NWB_SCENE_MAX_LIGHTS] = {};
    u32 lightCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class PrefixSceneUploadBuilder final : NoCopy{
public:
    PrefixSceneUploadBuilder(
        Core::GpuTaskGraph& graph,
        RendererDeferredSystem& deferredSystem,
        RendererMeshSystem& meshSystem,
        RendererRayTracingSystem& raytracingSystem,
        Core::GraphicsRuntime& graphics
    );

public:
    [[nodiscard]] bool declare(
        const PrefixSceneUploadInputs& inputs,
        PrefixSceneUploadResult& outResult
    );

private:
    Core::GpuTaskGraph& m_graph;
    RendererDeferredSystem& m_deferredSystem;
    RendererMeshSystem& m_meshSystem;
    RendererRayTracingSystem& m_raytracingSystem;
    Core::GraphicsRuntime& m_graphics;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
