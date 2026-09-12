// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prefix_scene_upload_builder.h"

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/lighting_content_stamp.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>

#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PrefixSceneUploadBuilder::PrefixSceneUploadBuilder(
    Core::GpuTaskGraph& graph,
    RendererDeferredSystem& deferredSystem,
    RendererMeshSystem& meshSystem,
    Core::GraphicsRuntime& graphics
)
    : m_graph(graph)
    , m_deferredSystem(deferredSystem)
    , m_meshSystem(meshSystem)
    , m_graphics(graphics){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool PrefixSceneUploadBuilder::declare(
    const PrefixSceneUploadInputs& inputs,
    PrefixSceneUploadResult& outResult
){
    outResult = PrefixSceneUploadResult{};
    if(
        !inputs.meshViewState
        || !inputs.meshView.valid()
        || !inputs.lights.valid()
        || !inputs.sceneShading.valid()
        || !inputs.shadowPrepareTask.valid()
        || !inputs.asyncPrefixTiming
        || !inputs.meshViewSetupTimingTicket
        || !inputs.sceneShadingSetupTimingTicket
        || !inputs.asyncPrefixTimingSpansOnePacket
        || !inputs.shadowVisibilityTask
        || !inputs.meshViewSetupReady
        || !inputs.sceneShadingSetupReady
        || !inputs.outSceneLightingContentHash
    )
        return false;

    const ECSRenderDetail::MeshViewGpuData& meshViewState = *inputs.meshViewState;
    const bool meshViewUploadRequired = inputs.meshViewUploadRequired;
    using namespace RendererTaskGraphDetail;

    ECSRenderDetail::SceneLightGpuData sceneLightData[NWB_SCENE_MAX_LIGHTS] = {};
    ECSRenderDetail::SceneShadingGpuData sceneShadingState;
    u32 sceneLightCount = 0u;
    RayTracingLightingClassification rayTracingLightingClassification;
    bool sceneLightUploadRequired = false;
    bool sceneShadingUploadRequired = false;
    if(!m_deferredSystem.prepareSceneShadingBufferUploads(
        inputs.meshViewAspectRatio,
        inputs.rayTracingLightingInput,
        sceneLightData,
        LengthOf(sceneLightData),
        sceneLightCount,
        rayTracingLightingClassification,
        sceneLightUploadRequired,
        sceneShadingState,
        sceneShadingUploadRequired
    ))
        return false;

    *inputs.outSceneLightingContentHash = ComputeSceneLightingContentHash(sceneShadingState, sceneLightData, sceneLightCount);

    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_setup"))
        .setMarkerLabel("Mesh View Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewSetupScheduling)
        .setDependencies(&inputs.shadowPrepareTask, 1u)
    ;
    outResult.meshViewSetupTask = m_graph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .graphics = &m_graphics,
            .asyncPrefixTiming = inputs.asyncPrefixTiming,
            .timingTicket = inputs.meshViewSetupTimingTicket,
            .asyncPrefixTimingSpansOnePacket = inputs.asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = inputs.shadowVisibilityTask,
        }
    );
    if(!outResult.meshViewSetupTask.valid())
        return false;

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskId meshViewUploadTask = outResult.meshViewSetupTask;
    if(meshViewUploadRequired){
        const Core::GpuUploadBlobId meshViewBlob = m_graph.copyUploadData(
            &meshViewState,
            sizeof(meshViewState),
            alignof(ECSRenderDetail::MeshViewGpuData)
        );
        Core::GpuTaskDesc meshViewUploadDesc;
        meshViewUploadDesc
            .setIdentity(Name("render.graphics_prefix.mesh_view_upload"))
            .setMarkerLabel("Mesh View Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&outResult.meshViewSetupTask, 1u)
        ;
        meshViewUploadTask = meshViewBlob.valid()
            ? m_graph.addUploadBufferTask(
                meshViewUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = meshViewBlob,
                    .destination = inputs.meshView,
                    // The backing buffer deliberately restores Common when a native packet closes.  Declare that
                    // exact graph-visible boundary here; the G-buffer consumer below owns the Common ->
                    // ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!meshViewUploadTask.valid())
            return false;
    }

    Core::GpuTaskSchedulingHint meshViewCommitScheduling = immutableUploadScheduling;
    Core::GpuTaskDesc meshViewCommitDesc;
    meshViewCommitDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_upload_commit"))
        .setMarkerLabel("Mesh View Upload Commit")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewCommitScheduling)
        .setDependencies(&meshViewUploadTask, 1u)
    ;
    const Core::GpuTaskId meshViewCommitTask = m_graph.addTask<ECSRenderDetail::MeshViewUploadCommitGraphTask>(
        meshViewCommitDesc,
        ECSRenderDetail::MeshViewUploadCommitGraphTask::Payload{
            .meshSystem = &m_meshSystem,
            .viewState = meshViewState,
            .uploadRequired = meshViewUploadRequired,
            .ready = inputs.meshViewSetupReady,
        }
    );
    if(!meshViewCommitTask.valid())
        return false;

    Core::GpuTaskId sceneUploadTask = meshViewCommitTask;
    if(sceneLightUploadRequired){
        const usize sceneLightByteCount = static_cast<usize>(sceneLightCount) * sizeof(sceneLightData[0u]);
        const Core::GpuUploadBlobId sceneLightBlob = m_graph.copyUploadData(
            sceneLightData,
            sceneLightByteCount,
            alignof(ECSRenderDetail::SceneLightGpuData)
        );
        Core::GpuTaskDesc sceneLightUploadDesc;
        sceneLightUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_lights_upload"))
            .setMarkerLabel("Scene Lights Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneLightBlob.valid()
            ? m_graph.addUploadBufferTask(
                sceneLightUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneLightBlob,
                    .destination = inputs.lights,
                    // These shared frame buffers retain Common between native packets.  The first declared reader
                    // owns the transition to ShaderResource.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid())
            return false;
    }

    if(sceneShadingUploadRequired){
        const Core::GpuUploadBlobId sceneShadingBlob = m_graph.copyUploadData(
            &sceneShadingState,
            sizeof(sceneShadingState),
            alignof(ECSRenderDetail::SceneShadingGpuData)
        );
        Core::GpuTaskDesc sceneShadingUploadDesc;
        sceneShadingUploadDesc
            .setIdentity(Name("render.graphics_prefix.scene_shading_upload"))
            .setMarkerLabel("Scene Shading Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&sceneUploadTask, 1u)
        ;
        sceneUploadTask = sceneShadingBlob.valid()
            ? m_graph.addUploadBufferTask(
                sceneShadingUploadDesc,
                Core::GpuUploadBufferTaskDesc{
                    .source = sceneShadingBlob,
                    .destination = inputs.sceneShading,
                    // See the light upload above: preserve the resource's automatic Common boundary and let the
                    // first declared reader lower its ConstantBuffer transition.
                    .finalState = Core::ResourceStates::Common,
                }
            )
            : Core::GpuTaskId{}
        ;
        if(!sceneUploadTask.valid())
            return false;
    }

    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc
        .setIdentity(Name("render.graphics_prefix.scene_shading_setup"))
        .setMarkerLabel("Scene Shading Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(sceneShadingSetupScheduling)
        .setDependencies(&sceneUploadTask, 1u)
    ;
    ECSRenderDetail::SceneShadingSetupGraphTask::Payload sceneShadingSetupPayload;
    sceneShadingSetupPayload.deferredSystem = &m_deferredSystem;
    sceneShadingSetupPayload.timingTicket = inputs.sceneShadingSetupTimingTicket;
    sceneShadingSetupPayload.ready = inputs.sceneShadingSetupReady;
    NWB_MEMCPY(
        sceneShadingSetupPayload.lightData,
        sizeof(sceneShadingSetupPayload.lightData),
        sceneLightData,
        sizeof(sceneLightData)
    );
    sceneShadingSetupPayload.sceneShadingState = sceneShadingState;
    sceneShadingSetupPayload.lightCount = sceneLightCount;
    sceneShadingSetupPayload.lightUploadRequired = sceneLightUploadRequired;
    sceneShadingSetupPayload.sceneShadingUploadRequired = sceneShadingUploadRequired;
    outResult.sceneShadingSetupTask = m_graph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(
        sceneShadingSetupDesc,
        Move(sceneShadingSetupPayload)
    );
    if(!outResult.sceneShadingSetupTask.valid())
        return false;
    outResult.tailTask = outResult.sceneShadingSetupTask;
    outResult.lightingClassification = rayTracingLightingClassification;
    NWB_MEMCPY(
        outResult.lightData,
        sizeof(outResult.lightData),
        sceneLightData,
        sizeof(sceneLightData)
    );
    outResult.lightCount = sceneLightCount;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

