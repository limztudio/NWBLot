// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/compute_emulation_capture.h>

#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<AvboitComputeEmulationCaptureResult> AvboitComputeEmulationCapture::capture(
    const AvboitComputeEmulationCaptureInputs& inputs,
    ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan,
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan& csgPlan,
    Core::Alloc::ScratchArena& scratchArena,
    usize instanceCount,
    usize materialTypedByteCount
){
    AvboitComputeEmulationCaptureResult result{};
    if(!inputs.drawItems || !inputs.csgFrameData)
        return MakeUnexpected(Failure{});
    const MaterialPassDrawItemPartitions& drawItems = *inputs.drawItems;
    // A phase owns one alias-free stream; mixed work keeps local interleaving.
    result.regularCaptured = drawItems.csg.computeDrawItems.empty()
        && inputs.geometryOwned
        && inputs.sampledTexturesCollected
        && plan.capture(drawItems.regular, scratchArena)
    ;
    result.csgCaptured = drawItems.regular.computeDrawItems.empty()
        && inputs.csgStreamsUploaded
        && inputs.intervalOutputsGraphOwned
        && inputs.geometryOwned
        && inputs.sampledTexturesCollected
        && csgPlan.capture(
            drawItems.csg,
            *inputs.csgFrameData,
            scratchArena
        )
    ;
    // All-compute draws share one output only as an explicit D/R sequence; mixed raster routes keep their main callback.
    result.sharedCaptured = !result.regularCaptured
        && drawItems.regular.meshDrawItems.empty()
        && drawItems.regular.indexedDrawItems.empty()
        && drawItems.csg.empty()
        && inputs.geometryOwned
        && inputs.sampledTexturesCollected
        && result.sharedPlan.capture(
            drawItems.regular,
            ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
        )
        && ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            result.sharedPlan.drawCount
        )
    ;
    NWB_ASSERT(
        !(result.regularCaptured && result.csgCaptured)
    );
    NWB_ASSERT(
        !result.sharedCaptured
        || (!result.regularCaptured
            && !result.csgCaptured)
    );
    if(result.sharedCaptured){
        result.sharedInstanceCount = instanceCount;
        result.sharedMaterialTypedByteCount = materialTypedByteCount;
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

