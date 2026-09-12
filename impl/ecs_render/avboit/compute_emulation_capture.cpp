// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/avboit/compute_emulation_capture.h>


#include <core/graphics/vulkan/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool AvboitComputeEmulationCapture::capture(
    const AvboitComputeEmulationCaptureInputs& inputs,
    ECSRenderDetail::AvboitAliasFreeComputeEmulationGraphPlan& plan,
    ECSRenderDetail::OpaqueCsgIntervalSampleComputeEmulationGraphPlan& csgPlan,
    Core::Alloc::ScratchArena& scratchArena,
    usize instanceCount,
    usize materialTypedByteCount,
    AvboitComputeEmulationCaptureResult& outResult
){
    outResult = AvboitComputeEmulationCaptureResult{};
    if(!inputs.drawItems || !inputs.csgFrameData)
        return false;
    const MaterialPassDrawItemPartitions& drawItems = *inputs.drawItems;
    // A phase owns one alias-free stream; mixed work keeps local interleaving.
    outResult.regularCaptured = drawItems.csg.computeDrawItems.empty()
        && inputs.geometryOwned
        && inputs.sampledTexturesCollected
        && plan.capture(drawItems.regular, scratchArena)
    ;
    outResult.csgCaptured = drawItems.regular.computeDrawItems.empty()
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
    // All-compute draws share one output only as an explicit D/R sequence; keep mesh/CSG out.
    outResult.sharedCaptured = !outResult.regularCaptured
        && drawItems.regular.meshDrawItems.empty()
        && drawItems.csg.empty()
        && inputs.geometryOwned
        && inputs.sampledTexturesCollected
        && outResult.sharedPlan.capture(
            drawItems.regular,
            ECSRenderDetail::s_SharedComputeEmulationMaximumDrawCount
        )
        && ECSRenderDetail::IsSupportedSharedComputeEmulationDrawCount(
            outResult.sharedPlan.drawCount
        )
    ;
    NWB_ASSERT(
        !(outResult.regularCaptured && outResult.csgCaptured)
    );
    NWB_ASSERT(
        !outResult.sharedCaptured
        || (!outResult.regularCaptured
            && !outResult.csgCaptured)
    );
    if(outResult.sharedCaptured){
        outResult.sharedInstanceCount = instanceCount;
        outResult.sharedMaterialTypedByteCount = materialTypedByteCount;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
