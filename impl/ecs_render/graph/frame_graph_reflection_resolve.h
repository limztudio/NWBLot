// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/raytrace/graph_snapshots.h>
#include <impl/ecs_render/reflection/settings.h>
#include <impl/ecs_render/reflection/scene_content_stamp.h>
#include <impl/ecs_render/reflection/reflection_system.h>
#include <impl/ecs_render/reflection/composite_inputs.h>
#include <impl/ecs_render/raytrace/task_graph_scene_resources.h>
#include <impl/ecs_render/reflection/task_graph_reflection.h>

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RendererRayTracingSystem;
class RendererReflectionSystem;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Reflection resolve owns reflection snapshot plus reflection/refraction task declaration.
struct FrameGraphReflectionResolveInputs{
    DeferredFrameTargets& targets;
    const ECSRenderDetail::MeshViewBufferSnapshot& meshViewSnapshot;
    const RayTracingSceneGraphResources& sceneResources;
    const ReflectionSettings& reflectionSettings;
    const ReflectionSceneContentStamp& contentStamp;
    const CsgFrameState& csgFrameState;
    const RayTracingRefractionGraphResources& refractionResources;
    u32 reflectionFrameIndex = 0u;
    bool reflectionSceneAvailable = false;
    bool refractionActive = false;
    bool useLaggedLightingHistory = false;
    Core::GpuGraphResourceId specularRoughness;
    Core::GpuGraphResourceId refractionSpecularRoughness;
    Core::GpuGraphResourceId normal;
    Core::GpuGraphResourceId depth;
    Core::GpuGraphResourceId worldPosition;
    Core::GpuGraphResourceId refractionDepth;
    Core::GpuGraphResourceId refractionNormalIor;
    Core::GpuGraphResourceId refractionTintCoverage;
    Core::GpuGraphResourceId refractionInstance;
    Core::GpuGraphResourceId refractionResolve;
    Core::GpuGraphResourceId opaqueColor;
    Core::GpuGraphResourceId meshView;
    Core::GpuGraphResourceId currentBindlessSlots;
    Core::GpuGraphResourceId sceneShading;
    Core::GpuGraphResourceId lights;
    Core::GpuGraphResourceId avboitAccumColor;
    Core::GpuGraphResourceId avboitAccumExtinction;
    Core::GpuGraphResourceId avboitForegroundColor;
    Core::GpuGraphResourceId avboitForegroundExtinction;
    Core::GpuGraphResourceSetId hardwareTraceGeometrySet;
    Core::GpuGraphResourceSetId traceMaterialSampledTextureSet;
    Core::GpuTaskId traceContextUploadTask;
    Core::GpuTaskId lightingTask;
    Core::GpuTaskId avboitFinalTask;
    Core::GpuTaskId surfelGiTask;
    const bool* hardwarePreparationReady = nullptr;
    bool* hardwareDispatchLogged = nullptr;
    bool* fallbackDispatchLogged = nullptr;
    bool* refractionHardwareLogged = nullptr;
    bool* refractionScreenLogged = nullptr;
};

struct FrameGraphReflectionResolveResult{
    RendererTaskGraphDetail::ReflectionGraphResult reflectionGraph;
    ReflectionCompositeInputs reflectionCompositeInputs;
    bool declared = false;
    Core::GpuTaskId refractionResolveTask;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FrameGraphReflectionResolve final : NoCopy{
public:
    FrameGraphReflectionResolve(Core::GpuTaskGraph& graph, Core::GraphicsRuntime& graphics, RendererRayTracingSystem& raytracingSystem, RendererReflectionSystem& reflectionSystem)noexcept;


public:
    [[nodiscard]] Expected<FrameGraphReflectionResolveResult> declare(const FrameGraphReflectionResolveInputs& inputs, Core::Alloc::ScratchArena& scratchArena, RayTracingSceneGraphReads& sceneReads);


private:
    Core::GpuTaskGraph& m_graph;
    Core::GraphicsRuntime& m_graphics;
    RendererRayTracingSystem& m_raytracingSystem;
    RendererReflectionSystem& m_reflectionSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

