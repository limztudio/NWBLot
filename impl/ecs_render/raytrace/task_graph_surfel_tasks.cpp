// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/task_graph_surfel_tasks.h>

#include <impl/ecs_render/kernel/renderer_constants_private.h>

#include <core/graphics/backend_selection.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiled_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool SurfelIrradianceClearGraphTask::record(
    const Payload& payload,
    Core::CommandList& commandList,
    const Core::GpuTaskRecordContext& context
){
    Core::Texture* const destination = context.taskGraph.textureForResource(payload.destination);
    // This Compute-only callback may not end a Graphics render pass. The packet must begin outside rendering,
    // which also preserves the command-IR replay precondition for its captured native texture clear.
    if(!destination || commandList.isRenderPassActive())
        return false;

    const Core::GpuClearTextureTaskDesc clearDesc{
        .destination = payload.destination,
        .subresources = s_FramebufferSubresources,
        .valueType = Core::GpuClearTextureTaskValueType::Float,
        .floatValue = Core::Color(0.f, 0.f, 0.f, 0.f),
    };
    if(
        context.commandIrCapture
        && !context.commandIrCapture->captureClearTexture(
            context.task,
            context.packet,
            context.queue,
            payload.destination,
            clearDesc
        )
    )
        return false;

    commandList.clearTextureFloat(destination, clearDesc.subresources, clearDesc.floatValue);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

