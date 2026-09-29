// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool HasResourceUse(
    const GpuTaskGraphTaskView& task,
    const GpuGraphResourceId resource,
    const ResourceStates::Mask state,
    const GpuTaskResourceAccess::Enum access
)noexcept{
    for(usize index = 0u; index < task.resourceUseCount; ++index){
        const GpuTaskResourceUse& use = task.resourceUses[index];
        if(use.resource == resource && use.requiredState == state && use.access == access)
            return true;
    }
    return false;
}

[[nodiscard]] static bool ViewportIsValid(const Viewport& viewport)noexcept{
    return
        IsFinite(viewport.minX)
        && IsFinite(viewport.maxX)
        && IsFinite(viewport.minY)
        && IsFinite(viewport.maxY)
        && IsFinite(viewport.minZ)
        && IsFinite(viewport.maxZ)
        && viewport.maxX > viewport.minX
        && viewport.maxY >= viewport.minY
        && viewport.minZ >= 0.0f
        && viewport.minZ <= 1.0f
        && viewport.maxZ >= 0.0f
        && viewport.maxZ <= 1.0f
    ;
}

[[nodiscard]] static bool ScissorIsValid(const Rect& scissor)noexcept{
    return
        scissor.minX >= 0
        && scissor.minY >= 0
        && scissor.maxX >= scissor.minX
        && scissor.maxY >= scissor.minY
    ;
}

[[nodiscard]] static bool ImplicitScissorIsValid(const Viewport& viewport)noexcept{
    return
        viewport.minX >= 0.0f
        && viewport.minY >= 0.0f
        && viewport.maxX <= static_cast<f32>(Limit<i32>::s_Max)
        && viewport.maxY <= static_cast<f32>(Limit<i32>::s_Max)
    ;
}

[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateGraphicsState(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task
)noexcept{
    if(!graph.validPipeline(record.pipeline))
        return GpuCommandIrReplayError::InvalidRasterState;
    if(graph.pipelineAt(record.pipeline.index).type != GpuGraphPipelineType::Graphics)
        return GpuCommandIrReplayError::InvalidRasterState;
    if(!graph.validResource(record.colorAttachment))
        return GpuCommandIrReplayError::InvalidResource;
    if(graph.resourceAt(record.colorAttachment.index).type != GpuGraphResourceType::Texture)
        return GpuCommandIrReplayError::ResourceTypeMismatch;
    if(!HasResourceUse(task, record.colorAttachment, ResourceStates::RenderTarget, GpuTaskResourceAccess::Write))
        return GpuCommandIrReplayError::ResourceUseMismatch;
    if(!ViewportIsValid(record.viewport))
        return GpuCommandIrReplayError::InvalidRasterState;
    if(record.hasScissor ? !ScissorIsValid(record.scissor) : !ImplicitScissorIsValid(record.viewport))
        return GpuCommandIrReplayError::InvalidRasterState;
    if(
        !IsFinite(record.blendConstantColor.r)
        || !IsFinite(record.blendConstantColor.g)
        || !IsFinite(record.blendConstantColor.b)
        || !IsFinite(record.blendConstantColor.a)
    )
        return GpuCommandIrReplayError::InvalidRasterState;

    for(usize bindingIndex = 0u; bindingIndex < record.vertexBuffers.size(); ++bindingIndex){
        const GpuCommandIrRasterVertexBinding& binding = record.vertexBuffers[bindingIndex];
        if(binding.slot >= s_MaxVertexAttributes)
            return GpuCommandIrReplayError::InvalidRasterState;
        if(!graph.validResource(binding.resource))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceAt(binding.resource.index).type != GpuGraphResourceType::Buffer)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        if(!HasResourceUse(task, binding.resource, ResourceStates::VertexBuffer, GpuTaskResourceAccess::Read))
            return GpuCommandIrReplayError::ResourceUseMismatch;
        for(usize priorIndex = 0u; priorIndex < bindingIndex; ++priorIndex){
            if(record.vertexBuffers[priorIndex].slot == binding.slot)
                return GpuCommandIrReplayError::InvalidRasterState;
        }
    }

    if(!record.indexResource.valid()){
        if(record.indexFormat != Format::UNKNOWN || record.indexOffset != 0u)
            return GpuCommandIrReplayError::InvalidRasterState;
        return GpuCommandIrReplayError::None;
    }
    if(!graph.validResource(record.indexResource))
        return GpuCommandIrReplayError::InvalidResource;
    if(graph.resourceAt(record.indexResource.index).type != GpuGraphResourceType::Buffer)
        return GpuCommandIrReplayError::ResourceTypeMismatch;
    if(!HasResourceUse(task, record.indexResource, ResourceStates::IndexBuffer, GpuTaskResourceAccess::Read))
        return GpuCommandIrReplayError::ResourceUseMismatch;
    const u32 indexBytes = record.indexFormat == Format::R16_UINT
        ? sizeof(u16)
        : (record.indexFormat == Format::R32_UINT ? sizeof(u32) : 0u)
    ;
    if(indexBytes == 0u || record.indexOffset % indexBytes != 0u)
        return GpuCommandIrReplayError::InvalidRasterState;
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsRasterOpcode(const GpuCommandIrWireOpcode::Enum opcode)noexcept{
    switch(opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:
    case GpuCommandIrWireOpcode::BindGraphicsHeap:
    case GpuCommandIrWireOpcode::SetPushConstants:
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:
    case GpuCommandIrWireOpcode::EndRenderPass:
        return true;
    default:
        return false;
    }
}

GpuCommandIrReplayError::Enum ValidateRasterBuiltinBoundary(
    const GpuTaskId&,
    const RasterReplayState& state
)noexcept{
    return state.active ? GpuCommandIrReplayError::InvalidRasterState : GpuCommandIrReplayError::None;
}

GpuCommandIrReplayError::Enum ValidateRasterGraphOperation(
    const GpuCommandIrRasterTaskRecord& record,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    RasterReplayState& state
)noexcept{
    if(state.active && state.task != record.task)
        return GpuCommandIrReplayError::InvalidRasterState;

    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrReplayError::Enum error = __hidden_gpu_command_ir_raster_preflight::ValidateGraphicsState(
            record, graph, task
        );
        if(error != GpuCommandIrReplayError::None)
            return error;
        state.graphicsState = record;
        state.task = record.task;
        state.active = true;
        state.heapBound = false;
        state.pushConstantsSet = false;
        return GpuCommandIrReplayError::None;
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:
        if(!state.active || record.pipeline != state.graphicsState.pipeline)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.heapBound = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::SetPushConstants:
        if(
            !state.active
            || record.blobSizeBytes == 0u
            || record.blobSizeBytes > s_MaxPushConstantSize
            || (record.blobSizeBytes & 3u) != 0u
        )
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:
        if(
            !state.active || !state.heapBound || !state.pushConstantsSet
            || record.drawArguments.vertexCount == 0u || record.drawArguments.instanceCount == 0u
        )
            return GpuCommandIrReplayError::InvalidRasterDraw;
        if(
            record.opcode == GpuCommandIrWireOpcode::DrawIndexed
            && (
                !state.graphicsState.indexResource.valid()
                || record.drawArguments.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max)
            )
        )
            return GpuCommandIrReplayError::InvalidRasterDraw;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::EndRenderPass:
        if(!state.active)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.active = false;
        state.task = {};
        state.heapBound = false;
        state.pushConstantsSet = false;
        return GpuCommandIrReplayError::None;
    default:
        return GpuCommandIrReplayError::InvalidRasterState;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

