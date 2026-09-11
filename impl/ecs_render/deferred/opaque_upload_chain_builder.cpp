// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/deferred/opaque_upload_chain_builder.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/kernel/task_graph_queue_requests.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/renderer_draw_types.h>

#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


OpaqueUploadChainBuilder::OpaqueUploadChainBuilder(
    Core::GpuTaskGraph& graph,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem
)
    : m_graph(graph)
    , m_materialSystem(materialSystem)
    , m_csgSystem(csgSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool OpaqueUploadChainBuilder::declare(
    const OpaqueUploadChainInputs& inputs,
    OpaqueUploadChainResult& outResult
){
    using namespace RendererTaskGraphDetail;
    outResult = OpaqueUploadChainResult{};
    if(
        !inputs.targets
        || !inputs.frameBindings
        || !inputs.csgResources
        || !inputs.drawItems
        || !inputs.instanceData
        || !inputs.csgFrameData
#if defined(NWB_DEBUG)
        || !inputs.materialTypedRanges
#endif
        || !inputs.materialTypedBytes
        || !inputs.dependencyTask.valid()
    )
        return false;

    DeferredFrameTargets& deferredTargets = *inputs.targets;
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings = *inputs.frameBindings;
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources = *inputs.csgResources;
    const MaterialPassDrawItemPartitions& opaqueDrawItems = *inputs.drawItems;
    InstanceGpuDataVector& instanceData = *inputs.instanceData;
    const CsgFrameGpuData& csgFrameData = *inputs.csgFrameData;
    const MaterialTypedByteDataVector& materialTypedBytes = *inputs.materialTypedBytes;

    Core::GpuTaskSchedulingHint immutableUploadScheduling;
    immutableUploadScheduling.cost = Core::GpuTaskCostHint::Tiny;
    immutableUploadScheduling.forceSubmissionBoundary = false;
    immutableUploadScheduling.allowPacketMerge = true;
    immutableUploadScheduling.mergeWithPrevious = true;

    const bool hasOpaqueDrawItems = !opaqueDrawItems.empty();
    outResult.hasOpaqueDrawItems = hasOpaqueDrawItems;
    Core::GpuTaskId materialDrawUploadTask = inputs.dependencyTask;
    if(hasOpaqueDrawItems){
        if(
            !inputs.materialInstances.valid()
            || !inputs.materialTyped.valid()
            || !frameBindings.frameReady(instanceData.size(), materialTypedBytes.size())
        )
            return false;
        m_materialSystem.prepareMaterialPassInstanceUploadData(instanceData, csgResources);
#if defined(NWB_DEBUG)
        if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData))
            return false;
        NWB_ASSERT(instanceData.size() == inputs.materialTypedRanges->size());
        ECSRenderDetail::AssertMaterialTypedUploadRanges(*inputs.materialTypedRanges, materialTypedBytes);
#endif

        const Core::GpuUploadBlobId instanceBlob = m_graph.copyUploadData(
            instanceData.data(),
            instanceData.size() * sizeof(InstanceGpuData),
            alignof(InstanceGpuData)
        );
        const Core::GpuUploadBlobId materialTypedBlob = m_graph.copyUploadData(
            materialTypedBytes.data(),
            materialTypedBytes.size(),
            alignof(u32)
        );
        if(!instanceBlob.valid() || !materialTypedBlob.valid())
            return false;

        Core::GpuTaskDesc instanceUploadDesc;
        instanceUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_instances_upload"))
            .setMarkerLabel("Material Instances Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_graph.addUploadBufferTask(
            instanceUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = instanceBlob,
                .destination = inputs.materialInstances,
                // Both draw buffers use automatic Common restoration when the native packet closes.  Keep that
                // graph-visible boundary exact; the G-buffer read below owns the transient SRV transition.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid())
            return false;

        Core::GpuTaskDesc materialTypedUploadDesc;
        materialTypedUploadDesc
            .setIdentity(Name("render.graphics_prefix.material_typed_upload"))
            .setMarkerLabel("Material Typed Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&materialDrawUploadTask, 1u)
        ;
        materialDrawUploadTask = m_graph.addUploadBufferTask(
            materialTypedUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = materialTypedBlob,
                .destination = inputs.materialTyped,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!materialDrawUploadTask.valid())
            return false;
        outResult.materialDrawBuffersUploaded = true;
    }
    outResult.materialUploadTask = materialDrawUploadTask;

    // Freeze every opaque CSG upload byte after preflight fixed the buffer, descriptor, and target generations.
    // Native G-buffer recording consumes these values without rebuilding either CSG uniform payload from live state.
    const bool hasCsgFrameGpuWork = csgFrameData.hasWork();
    outResult.hasCsgFrameGpuWork = hasCsgFrameGpuWork;
    Core::GpuTaskId csgFrameUploadTask = materialDrawUploadTask;
    if(hasCsgFrameGpuWork){
        if(
            !inputs.csgReceiverRanges.valid()
            || !inputs.csgCutters.valid()
            || !inputs.csgClipContextSlots.valid()
            || !inputs.csgIntervalSampleState.valid()
            || !csgResources.frameReady(csgFrameData)
        )
            return false;
#if defined(NWB_DEBUG)
        if(
            csgFrameData.receiverRanges.size() > Limit<usize>::s_Max / sizeof(CsgReceiverRangeGpuData)
            || csgFrameData.cutters.size() > Limit<usize>::s_Max / sizeof(CsgCutterGpuData)
        )
            return false;
#endif

        CsgClipContextSlots csgClipContextSlotData;
        CsgIntervalSampleStateGpuData csgIntervalSampleStateData;
        if(
            !m_csgSystem.prepareCsgClipContextSlotData(
                deferredTargets,
                csgFrameData,
                csgResources,
                frameBindings,
                csgClipContextSlotData
            )
            || !m_csgSystem.prepareCsgIntervalSampleStateData(
                deferredTargets,
                csgFrameData,
                csgResources,
                frameBindings,
                csgIntervalSampleStateData
            )
        )
            return false;

        const Core::GpuUploadBlobId receiverRangesBlob = m_graph.copyUploadData(
            csgFrameData.receiverRanges.data(),
            csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData),
            alignof(CsgReceiverRangeGpuData)
        );
        const Core::GpuUploadBlobId cuttersBlob = m_graph.copyUploadData(
            csgFrameData.cutters.data(),
            csgFrameData.cutters.size() * sizeof(CsgCutterGpuData),
            alignof(CsgCutterGpuData)
        );
        const Core::GpuUploadBlobId clipContextSlotsBlob = m_graph.copyUploadData(
            &csgClipContextSlotData,
            sizeof(csgClipContextSlotData),
            alignof(CsgClipContextSlots)
        );
        const Core::GpuUploadBlobId intervalSampleStateBlob = m_graph.copyUploadData(
            &csgIntervalSampleStateData,
            sizeof(csgIntervalSampleStateData),
            alignof(CsgIntervalSampleStateGpuData)
        );
        if(
            !receiverRangesBlob.valid()
            || !cuttersBlob.valid()
            || !clipContextSlotsBlob.valid()
            || !intervalSampleStateBlob.valid()
        )
            return false;

        Core::GpuTaskDesc receiverRangesUploadDesc;
        receiverRangesUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_receiver_ranges_upload"))
            .setMarkerLabel("CSG Receiver Ranges Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_graph.addUploadBufferTask(
            receiverRangesUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = receiverRangesBlob,
                .destination = inputs.csgReceiverRanges,
                // CSG structured buffers restore Common at native packet close; G-buffer owns their transient SRV
                // state exactly like the graph-owned material streams above.
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid())
            return false;

        Core::GpuTaskDesc cuttersUploadDesc;
        cuttersUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_cutters_upload"))
            .setMarkerLabel("CSG Cutters Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_graph.addUploadBufferTask(
            cuttersUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = cuttersBlob,
                .destination = inputs.csgCutters,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid())
            return false;

        Core::GpuTaskDesc clipContextSlotsUploadDesc;
        clipContextSlotsUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_clip_context_slots_upload"))
            .setMarkerLabel("CSG Clip Context Slots Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_graph.addUploadBufferTask(
            clipContextSlotsUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = clipContextSlotsBlob,
                .destination = inputs.csgClipContextSlots,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid())
            return false;

        Core::GpuTaskDesc intervalSampleStateUploadDesc;
        intervalSampleStateUploadDesc
            .setIdentity(Name("render.graphics_prefix.csg_interval_sample_state_upload"))
            .setMarkerLabel("CSG Interval Sample State Upload")
            .setQueue(GraphicsUploadQueueRequest())
            .setScheduling(immutableUploadScheduling)
            .setDependencies(&csgFrameUploadTask, 1u)
        ;
        csgFrameUploadTask = m_graph.addUploadBufferTask(
            intervalSampleStateUploadDesc,
            Core::GpuUploadBufferTaskDesc{
                .source = intervalSampleStateBlob,
                .destination = inputs.csgIntervalSampleState,
                .finalState = Core::ResourceStates::Common,
            }
        );
        if(!csgFrameUploadTask.valid())
            return false;
        outResult.csgFrameBuffersUploaded = true;
    }
    outResult.csgUploadTask = csgFrameUploadTask;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
