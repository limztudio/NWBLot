// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "refraction_capture_resources.h"

#include <impl/ecs_render/avboit/types.adhd.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/deferred/deferred_frame_targets.h>
#include <impl/ecs_render/kernel/task_graph_resource_utils.h>
#include <impl/ecs_render/kernel/task_graph_scheduling.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AvboitRefractionCaptureResources::appendUpload(const Name identity, const Core::GpuGraphResourceId destination, const void* bytes, const usize byteSize, const usize alignment){
    auto& graph = m_graph;
    auto& dependency = m_dependency;
    if(!destination.valid() || !bytes || byteSize == 0u)
        return false;
    const auto blobResult = graph.copyUploadData(bytes, byteSize, alignment);
    if(!blobResult)
        return false;
    const Core::GpuUploadBlobId blob = *blobResult;
    Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Upload", dependency);
    const auto producedTask = graph.addUploadBufferTask(desc, Core::GpuUploadBufferTaskDesc{ .source = blob, .destination = destination, .finalState = Core::ResourceStates::Common, });
    if(!producedTask){
        dependency = {};
        return false;
    }
    dependency = *producedTask;
    return true;
}


b8 AvboitRefractionCaptureResources::appendCsg(
    RendererCsgSystem& csgSystem,
    const DeferredFrameTargets& targets,
    const CsgFrameGpuData& csgFrameData,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& csgUses
){
    auto& graph = m_graph;
    const auto importBuffer = [&](const Core::BufferHandle& buffer){ return FindOrImportBuffer(graph, buffer, "Refraction Capture Buffer"); };
    const auto importTexture = [&](const Core::TextureHandle& texture){ return FindOrImportTexture(graph, texture, "Refraction Capture Texture"); };
    csgUses.reserve(s_RefractionCaptureCsgUseCapacity);
    const auto clipContext = csgSystem.prepareCsgClipContextSlotData(targets, csgFrameData, csgResources, frameBindings);
    if(!clipContext)
        return false;
    const auto rangesResult = importBuffer(csgResources.receiverRanges);
    const auto cuttersResult = importBuffer(csgResources.cutters);
    const auto clipResult = importBuffer(csgResources.clipContextSlots);
    const auto intervalResult = importBuffer(csgResources.intervalSampleState);
    if(
        !intervalResult
        || !rangesResult
        || !appendUpload(Name("render.refraction.capture.csg_ranges_upload"), *rangesResult, csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData))
        || !cuttersResult
        || !appendUpload(Name("render.refraction.capture.csg_cutters_upload"), *cuttersResult, csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData))
        || !clipResult
        || !appendUpload(Name("render.refraction.capture.csg_context_upload"), *clipResult, &*clipContext, sizeof(*clipContext), alignof(CsgClipContextSlots))
    )
        return false;
    const Core::GpuGraphResourceId interval = *intervalResult;
    const Core::GpuGraphResourceId ranges = *rangesResult;
    const Core::GpuGraphResourceId cutters = *cuttersResult;
    const Core::GpuGraphResourceId clip = *clipResult;
    csgUses.push_back(ReadBufferUse(ranges, Core::BufferRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData))));
    csgUses.push_back(ReadBufferUse(cutters, Core::BufferRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData))));
    csgUses.push_back(ReadUse(clip, Core::ResourceStates::ConstantBuffer));
    csgUses.push_back(ReadUse(interval, Core::ResourceStates::ConstantBuffer));
    const Array<ReferenceWrapper<const Core::TextureHandle>, 4u> intervalTextures = { targets.csgRemovedIntervalDepth, targets.csgRemovedIntervalCapNormal, targets.csgRemovedIntervalData, targets.csgRemovedIntervalCount, };
    for(const Core::TextureHandle& texture : intervalTextures){
        const auto resourceResult = importTexture(texture);
        if(!resourceResult)
            return false;
        const Core::GpuGraphResourceId resource = *resourceResult;
        csgUses.push_back(ReadTextureUse(resource, Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess));
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

