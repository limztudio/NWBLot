// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "prefix_scene_upload_builder.h"

#include <impl/ecs_render/deferred/deferred_system.h>
#include <impl/ecs_render/deferred/lighting_content_stamp.h>
#include <impl/ecs_render/deferred/task_graph_prefix_tasks.h>
#include <impl/ecs_render/mesh/mesh_system.h>
#include <impl/ecs_render/mesh/task_graph_prefix_tasks.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/simd/memory.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PrefixSceneUploadBuilder::PrefixSceneUploadBuilder(Core::GpuTaskGraph& graph, RendererDeferredSystem& deferredSystem, RendererMeshSystem& meshSystem, Core::GraphicsRuntime& graphics)noexcept
    : m_graph(graph)
    , m_deferredSystem(deferredSystem)
    , m_meshSystem(meshSystem)
    , m_graphics(graphics){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<PrefixSceneUploadResult> PrefixSceneUploadBuilder::declare(const PrefixSceneUploadInputs& inputs){
    PrefixSceneUploadResult result{};
    if(
        !inputs.meshView.valid()
        || !inputs.lights.valid()
        || !inputs.sceneShading.valid()
        || !inputs.shadowPrepareTask.valid()
    )
        return MakeUnexpected(Failure{});

    const ECSRenderDetail::MeshViewGpuData& meshViewState = inputs.meshViewState;
    const bool meshViewUploadRequired = inputs.meshViewUploadRequired;

    const SceneShadingBufferUploads uploads = m_deferredSystem.prepareSceneShadingBufferUploads(inputs.meshViewAspectRatio, inputs.rayTracingLightingInput);
    const auto& sceneLightData = uploads.lightData;
    const auto& sceneShadingState = uploads.sceneShadingState;
    const u32 sceneLightCount = uploads.lightCount;
    const auto& rayTracingLightingClassification = uploads.lightingClassification;
    const bool sceneLightUploadRequired = uploads.lightUploadRequired;
    const bool sceneShadingUploadRequired = uploads.sceneShadingUploadRequired;
    result.sceneLightingContentHash = ComputeSceneLightingContentHash(sceneShadingState, sceneLightData, sceneLightCount);

    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc.setIdentity(Name("render.graphics_prefix.mesh_view_setup")).setMarkerLabel("Mesh View Setup").setScheduling(meshViewSetupScheduling).setDependencies(&inputs.shadowPrepareTask, 1u);
    const auto resultMeshViewSetupTaskResult = m_graph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .graphics = m_graphics,
            .asyncPrefixTiming = inputs.asyncPrefixTiming,
            .timingTicket = inputs.meshViewSetupTimingTicket,
            .asyncPrefixTimingSpansOnePacket = inputs.asyncPrefixTimingSpansOnePacket,
            .shadowVisibilityTask = &inputs.shadowVisibilityTask,
        }
    );
    if(!resultMeshViewSetupTaskResult){
        result.meshViewSetupTask = {};
        return MakeUnexpected(Failure{});
    }
    result.meshViewSetupTask = *resultMeshViewSetupTaskResult;

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    Core::GpuTaskId meshViewUploadTask = result.meshViewSetupTask;
    if(meshViewUploadRequired){
        const auto meshViewBlobResult = m_graph.copyUploadData(&meshViewState, sizeof(meshViewState), alignof(ECSRenderDetail::MeshViewGpuData));
        Core::GpuTaskDesc meshViewUploadDesc;
        meshViewUploadDesc.setIdentity(Name("render.graphics_prefix.mesh_view_upload")).setMarkerLabel("Mesh View Upload").setScheduling(immutableUploadScheduling).setDependencies(&result.meshViewSetupTask, 1u);
        if(!meshViewBlobResult)
            return MakeUnexpected(meshViewBlobResult.error());
        const Core::GpuUploadBlobId meshViewBlob = *meshViewBlobResult;
        const auto meshViewUploadTaskResult = m_graph.addUploadBufferTask(
            meshViewUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = meshViewBlob,
                .destination = inputs.meshView,
                // The backing buffer deliberately restores Common when a native packet closes.
                // Declare that exact graph-visible boundary here; the G-buffer consumer below owns the Common -> ConstantBuffer transition.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!meshViewUploadTaskResult){
            meshViewUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        meshViewUploadTask = *meshViewUploadTaskResult;
    }

    Core::GpuTaskSchedulingHint meshViewCommitScheduling = immutableUploadScheduling;
    Core::GpuTaskDesc meshViewCommitDesc;
    meshViewCommitDesc.setIdentity(Name("render.graphics_prefix.mesh_view_upload_commit")).setMarkerLabel("Mesh View Upload Commit").setScheduling(meshViewCommitScheduling).setDependencies(&meshViewUploadTask, 1u);
    const auto meshViewCommitTaskResult = m_graph.addTask<ECSRenderDetail::MeshViewUploadCommitGraphTask>(
        meshViewCommitDesc,
        ECSRenderDetail::MeshViewUploadCommitGraphTask::Payload{ .meshSystem = m_meshSystem, .viewState = meshViewState, .uploadRequired = meshViewUploadRequired, .ready = inputs.meshViewSetupReady, }
    );
    if(!meshViewCommitTaskResult)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskId meshViewCommitTask = *meshViewCommitTaskResult;

    Core::GpuTaskId sceneUploadTask = meshViewCommitTask;
    if(sceneLightUploadRequired){
        const usize sceneLightByteCount = static_cast<usize>(sceneLightCount) * sizeof(sceneLightData[0u]);
        const auto sceneLightBlobResult = m_graph.copyUploadData(sceneLightData, sceneLightByteCount, alignof(ECSRenderDetail::SceneLightGpuData));
        Core::GpuTaskDesc sceneLightUploadDesc;
        sceneLightUploadDesc.setIdentity(Name("render.graphics_prefix.scene_lights_upload")).setMarkerLabel("Scene Lights Upload").setScheduling(immutableUploadScheduling).setDependencies(&sceneUploadTask, 1u);
        if(!sceneLightBlobResult)
            return MakeUnexpected(sceneLightBlobResult.error());
        const Core::GpuUploadBlobId sceneLightBlob = *sceneLightBlobResult;
        const auto sceneUploadTaskResult = m_graph.addUploadBufferTask(
            sceneLightUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneLightBlob,
                .destination = inputs.lights,
                // These shared frame buffers retain Common between native packets.
                // The first declared reader owns the transition to ShaderResource.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!sceneUploadTaskResult){
            sceneUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        sceneUploadTask = *sceneUploadTaskResult;
    }

    if(sceneShadingUploadRequired){
        const auto sceneShadingBlobResult = m_graph.copyUploadData(&sceneShadingState, sizeof(sceneShadingState), alignof(ECSRenderDetail::SceneShadingGpuData));
        Core::GpuTaskDesc sceneShadingUploadDesc;
        sceneShadingUploadDesc.setIdentity(Name("render.graphics_prefix.scene_shading_upload")).setMarkerLabel("Scene Shading Upload").setScheduling(immutableUploadScheduling).setDependencies(&sceneUploadTask, 1u);
        if(!sceneShadingBlobResult)
            return MakeUnexpected(sceneShadingBlobResult.error());
        const Core::GpuUploadBlobId sceneShadingBlob = *sceneShadingBlobResult;
        const auto sceneUploadTaskResult2 = m_graph.addUploadBufferTask(
            sceneShadingUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = sceneShadingBlob,
                .destination = inputs.sceneShading,
                // See the light upload above: preserve the resource's automatic Common boundary and let the first declared reader lower its ConstantBuffer transition.
                .finalState = Core::ResourceStates::Common, }
        );
        if(!sceneUploadTaskResult2){
            sceneUploadTask = {};
            return MakeUnexpected(Failure{});
        }
        sceneUploadTask = *sceneUploadTaskResult2;
    }

    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc.setIdentity(Name("render.graphics_prefix.scene_shading_setup")).setMarkerLabel("Scene Shading Setup").setScheduling(sceneShadingSetupScheduling).setDependencies(&sceneUploadTask, 1u);
    ECSRenderDetail::SceneShadingSetupGraphTask::Payload sceneShadingSetupPayload{ .deferredSystem = m_deferredSystem, .timingTicket = inputs.sceneShadingSetupTimingTicket, .ready = inputs.sceneShadingSetupReady, .sceneShadingState = sceneShadingState, };
    NWB_SIMD_MEMCPY(sceneShadingSetupPayload.lightData, sizeof(sceneShadingSetupPayload.lightData), sceneLightData, sizeof(sceneLightData));
    sceneShadingSetupPayload.lightCount = sceneLightCount;
    sceneShadingSetupPayload.lightUploadRequired = sceneLightUploadRequired;
    sceneShadingSetupPayload.sceneShadingUploadRequired = sceneShadingUploadRequired;
    const auto resultSceneShadingSetupTaskResult = m_graph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(sceneShadingSetupDesc, Move(sceneShadingSetupPayload));
    if(!resultSceneShadingSetupTaskResult){
        result.sceneShadingSetupTask = {};
        return MakeUnexpected(Failure{});
    }
    result.sceneShadingSetupTask = *resultSceneShadingSetupTaskResult;
    result.tailTask = result.sceneShadingSetupTask;
    result.lightingClassification = rayTracingLightingClassification;
    NWB_SIMD_MEMCPY(result.lightData, sizeof(result.lightData), sceneLightData, sizeof(sceneLightData));
    result.lightCount = sceneLightCount;
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

