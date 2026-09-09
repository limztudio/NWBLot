// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_scene_resources.h"

#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RayTracingSceneGraphReads ImportRayTracingSceneGraphReads(
    Core::GpuTaskGraph& graph,
    const RayTracingSceneGraphResources& resources,
    const Core::ResourceStates::Mask tlasInitialState){
    if(!resources.valid())
        return {};

    const Core::BufferHandle buffers[] = {
        resources.materialContextSlotsBuffer,
        resources.instanceMaterialBuffer,
        resources.materialTypedBuffer,
        resources.instanceBuffer,
        resources.opticalScene.buffer,
    };
    Core::GpuGraphResourceId importedBuffers[LengthOf(buffers)] = {};
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return {};
        for(usize index = 0u; index < LengthOf(buffers); ++index)
            importedBuffers[index] = declarations.findImportedBuffer(buffers[index]);
    }
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        if(importedBuffers[index].valid())
            continue;
        const Name identity = buffers[index]->getCreationDescription().debugName;
        if(!identity)
            return {};
        importedBuffers[index] = graph.importBuffer(
            buffers[index], RendererTaskGraphDetail::BufferResourceDesc(identity, "Ray Scene Buffer")
        );
        if(!importedBuffers[index].valid())
            return {};
    }

    // Other shared scene consumers use this identity. Reimporting the same TLAS with matching metadata returns its
    // existing graph identity, preserving the preparation producer and cross-effect read dependencies.
    const Core::GpuGraphResourceId tlas = graph.importAccelStruct(
        resources.sceneTlas,
        RendererTaskGraphDetail::AccelStructResourceDesc(Name("render.deferred_effects.tlas"), "Scene TLAS")
            .setInitialState(tlasInitialState)
    );
    if(!tlas.valid())
        return {};

    // Every declared consumer frame uploads its immutable metadata on the same primary queue. This deliberately
    // makes no preflight/accepted-cache claim: a rejected graph cannot cause a later frame to skip initialization,
    // and normal graph write/read hazards serialize replacements after all earlier consumers of this buffer.
    const auto& opticalBytes = resources.opticalScene.upload->bytes;
    const Core::GpuUploadBlobId opticalBlob = graph.copyUploadData(opticalBytes.data(), opticalBytes.size(), alignof(u32));
    if(!opticalBlob.valid())
        return {};
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc uploadDesc;
    uploadDesc
        .setIdentity(Name("render.raytrace.optical_scene_upload"))
        .setMarkerLabel("Ray Optical Scene Upload")
        .setQueue(RendererTaskGraphDetail::GraphicsUploadQueueRequest())
        .setScheduling(scheduling)
    ;
    const Core::GpuTaskId opticalUpload = graph.addUploadBufferTask(
        uploadDesc,
        Core::GpuUploadBufferTaskDesc{
            .source = opticalBlob,
            .destination = importedBuffers[LengthOf(buffers) - 1u],
            .finalState = Core::ResourceStates::Common,
        }
    );
    if(!opticalUpload.valid())
        return {};

    RayTracingSceneGraphReads result;
    result.uses[0] = RendererTaskGraphDetail::ReadUse(tlas, Core::ResourceStates::AccelStructRead);
    for(usize index = 0u; index < LengthOf(buffers); ++index){
        const Core::ResourceStates::Mask state = index == 0u
            ? Core::ResourceStates::ConstantBuffer : Core::ResourceStates::ShaderResource;
        result.uses[index + 1u] = RendererTaskGraphDetail::ReadUse(importedBuffers[index], state);
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

