// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/avboit_pass_upload_helper.h>


#include <core/graphics/vulkan/backend.h>

#include <impl/ecs_render/csg/csg_graph_resource_snapshot.h>
#include <impl/ecs_render/material/material_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AvboitPassUploadHelper::AvboitPassUploadHelper(
    RendererMaterialSystem& materialSystem
)
    : m_materialSystem(materialSystem){
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitPassUploadHelper::gather(
    const AvboitPassUploadInputs& inputs,
    MaterialPassDrawItemPartitions& drawItems,
    InstanceGpuDataVector& instanceData,
    CsgFrameGpuData& csgFrameData,
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector& materialTypedRanges,
#endif
    MaterialTypedByteDataVector& materialTypedBytes,
    AvboitPassUploadResult& outResult
){
    outResult = AvboitPassUploadResult{};
    if(
        !inputs.framebuffer
        || !inputs.csgFrameState
        || !inputs.frameBindings
        || !inputs.csgResources
        || !inputs.meshViewState
    )
        return false;
    m_materialSystem.gatherMaterialPassDrawItems(
        inputs.framebuffer,
        inputs.pass,
        true,
        (*inputs.csgFrameState),
        drawItems,
        instanceData,
        csgFrameData,
#if defined(NWB_DEBUG)
        materialTypedRanges,
#endif
        materialTypedBytes,
        RendererResourceLookupMode::PreparedOnly,
        inputs.meshViewState
    );

    outResult.hasDrawItems = !drawItems.empty();
    outResult.hasCsgDrawItems = !drawItems.csg.empty();
    if(!outResult.hasDrawItems){
        outResult.ready = true;
        return true;
    }
    const bool csgReady = !outResult.hasCsgDrawItems || (
        csgFrameData.hasWork()
        && inputs.csgReceiverRanges.valid()
        && inputs.csgCutters.valid()
        && inputs.csgClipContextSlots.valid()
        && (inputs.pass != MaterialPipelinePass::AvboitExtinction || inputs.csgIntervalSampleState.valid())
        && inputs.csgResources->frameReady(csgFrameData)
        && m_materialSystem.materialPassDrawResourcesReady(drawItems.csg, (*inputs.frameBindings))
    );
    if(
        !inputs.materialInstances.valid()
        || !inputs.materialTyped.valid()
        || !inputs.frameBindings->frameReady(instanceData.size(), materialTypedBytes.size())
        || !m_materialSystem.materialPassDrawResourcesReady(drawItems.regular, (*inputs.frameBindings))
        || !csgReady
    )
        return false;
    outResult.ready = true;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

