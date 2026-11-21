// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "command_ir_internal.h"

#include <core/task/gpu/task_graph.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gpu_command_ir_raster_preflight{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 HasResourceUse(const GpuTaskGraphTaskView& task, const GpuGraphResourceId resource, const ResourceStates::Mask state, const GpuTaskResourceAccess::Enum access)noexcept{
    return GpuCommandIrDetail::FindTaskResourceUse(task, resource, state, access) != nullptr;
}

[[nodiscard]] static b8 ViewportIsValid(const Viewport& viewport)noexcept{
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

[[nodiscard]] static b8 ImplicitScissorIsValid(const Viewport& viewport)noexcept{
    return viewport.minX >= 0.0f && viewport.minY >= 0.0f && static_cast<f64>(viewport.maxX) <= static_cast<f64>(Limit<i32>::s_Max) && static_cast<f64>(viewport.maxY) <= static_cast<f64>(Limit<i32>::s_Max);
}

template<typename GetVertexBindingFn>
[[nodiscard]] static GpuCommandIrReplayError::Enum ValidateSetGraphicsStateGraph(
    const GpuTaskId taskId,
    const GpuGraphPipelineId pipeline,
    const GpuGraphResourceId colorAttachment,
    const u32 framebufferOwnerIndex,
    const GpuGraphResourceId indexResource,
    const Format::Enum indexFormat,
    const u32 indexOffset,
    const Viewport& viewport,
    const Rect& scissor,
    const Color& blendConstantColor,
    const u8 vertexBindingCount,
    const b8 hasScissor,
    const b8 verifyWireFields,
    const GetVertexBindingFn& getVertexBinding,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    GpuCommandIrDetail::RasterReplayState& state
)noexcept{
    b8 matchesCachedState = state.active
        && state.pipeline == pipeline
        && state.colorAttachment == colorAttachment
        && state.indexResource == indexResource
        && state.indexFormat == indexFormat
        && state.indexOffset == indexOffset
        && state.vertexBindingCount == vertexBindingCount
    ;
    if(matchesCachedState){
        for(u8 bindingIndex = 0u; bindingIndex < vertexBindingCount; ++bindingIndex){
            const GpuCommandIrRasterVertexBinding binding = getVertexBinding(bindingIndex);
            const GpuCommandIrDetail::RasterVertexCacheEntry& cached = state.vertexBuffers[bindingIndex];
            if(
                cached.slot != binding.slot
                || cached.resourceGeneration != binding.resource.generation
                || cached.resourceIndex != binding.resource.index
            ){
                matchesCachedState = false;
                break;
            }
        }
    }

    if(matchesCachedState){
        if(verifyWireFields){
            if(!ViewportIsValid(viewport))
                return GpuCommandIrReplayError::InvalidRasterState;
            if(hasScissor ? !scissor.validScissor() : !ImplicitScissorIsValid(viewport))
                return GpuCommandIrReplayError::InvalidRasterState;
            if(!IsFinite(blendConstantColor.r) || !IsFinite(blendConstantColor.g) || !IsFinite(blendConstantColor.b) || !IsFinite(blendConstantColor.a))
                return GpuCommandIrReplayError::InvalidRasterState;
        }
        else if(!hasScissor && !ImplicitScissorIsValid(viewport))
            return GpuCommandIrReplayError::InvalidRasterState;
    }
    else{
        if(!graph.validPipeline(pipeline))
            return GpuCommandIrReplayError::InvalidRasterState;
        if(graph.pipelineAt(pipeline.index).type != GpuGraphPipelineType::Graphics)
            return GpuCommandIrReplayError::InvalidRasterState;
        if(!graph.validResource(colorAttachment))
            return GpuCommandIrReplayError::InvalidResource;
        if(graph.resourceAt(colorAttachment.index).type != GpuGraphResourceType::Texture)
            return GpuCommandIrReplayError::ResourceTypeMismatch;
        if(!HasResourceUse(task, colorAttachment, ResourceStates::RenderTarget, GpuTaskResourceAccess::Write))
            return GpuCommandIrReplayError::ResourceUseMismatch;
        if(verifyWireFields){
            if(!ViewportIsValid(viewport))
                return GpuCommandIrReplayError::InvalidRasterState;
            if(hasScissor ? !scissor.validScissor() : !ImplicitScissorIsValid(viewport))
                return GpuCommandIrReplayError::InvalidRasterState;
            if(!IsFinite(blendConstantColor.r) || !IsFinite(blendConstantColor.g) || !IsFinite(blendConstantColor.b) || !IsFinite(blendConstantColor.a))
                return GpuCommandIrReplayError::InvalidRasterState;
        }
        else if(!hasScissor && !ImplicitScissorIsValid(viewport))
            return GpuCommandIrReplayError::InvalidRasterState;

        if(vertexBindingCount > s_MaxVertexAttributes)
            return GpuCommandIrReplayError::InvalidRasterState;
        u32 usedSlotMask = 0u;
        for(u8 bindingIndex = 0u; bindingIndex < vertexBindingCount; ++bindingIndex){
            const GpuCommandIrRasterVertexBinding binding = getVertexBinding(bindingIndex);
            if(binding.slot >= s_MaxVertexAttributes)
                return GpuCommandIrReplayError::InvalidRasterState;
            if(!graph.validResource(binding.resource))
                return GpuCommandIrReplayError::InvalidResource;
            if(graph.resourceAt(binding.resource.index).type != GpuGraphResourceType::Buffer)
                return GpuCommandIrReplayError::ResourceTypeMismatch;
            if(!HasResourceUse(task, binding.resource, ResourceStates::VertexBuffer, GpuTaskResourceAccess::Read))
                return GpuCommandIrReplayError::ResourceUseMismatch;
            if(verifyWireFields){
                const u32 slotBit = 1u << binding.slot;
                if((usedSlotMask & slotBit) != 0u)
                    return GpuCommandIrReplayError::InvalidRasterState;
                usedSlotMask |= slotBit;
            }
        }

        if(!indexResource.valid()){
            if(indexFormat != Format::UNKNOWN || indexOffset != 0u)
                return GpuCommandIrReplayError::InvalidRasterState;
        }
        else{
            if(!graph.validResource(indexResource))
                return GpuCommandIrReplayError::InvalidResource;
            if(graph.resourceAt(indexResource.index).type != GpuGraphResourceType::Buffer)
                return GpuCommandIrReplayError::ResourceTypeMismatch;
            if(!HasResourceUse(task, indexResource, ResourceStates::IndexBuffer, GpuTaskResourceAccess::Read))
                return GpuCommandIrReplayError::ResourceUseMismatch;
            const u32 indexBytes = indexFormat == Format::R16_UINT ? sizeof(u16) : (indexFormat == Format::R32_UINT ? sizeof(u32) : 0u);
            if(indexBytes == 0u || indexOffset % indexBytes != 0u)
                return GpuCommandIrReplayError::InvalidRasterState;
        }
    }

    state.task = taskId;
    state.pipeline = pipeline;
    state.colorAttachment = colorAttachment;
    state.indexResource = indexResource;
    state.framebufferOwnerIndex = framebufferOwnerIndex;
    state.indexOffset = indexOffset;
    state.indexFormat = indexFormat;
    state.vertexBindingCount = vertexBindingCount;
    for(u8 bindingIndex = 0u; bindingIndex < vertexBindingCount; ++bindingIndex){
        const GpuCommandIrRasterVertexBinding binding = getVertexBinding(bindingIndex);
        state.vertexBuffers[bindingIndex] = GpuCommandIrDetail::RasterVertexCacheEntry{
            .resourceGeneration = binding.resource.generation,
            .resourceIndex = binding.resource.index,
            .slot = binding.slot,
        };
    }
    state.active = true;
    state.heapBound = false;
    state.pushConstantsSet = false;
    return GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuCommandIrDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GpuCommandIrReplayError::Enum ValidateRasterGraphOperation(const GpuCommandIrRasterTaskRecord& record, const GpuTaskGraphDeclarationReadView& graph, const GpuTaskGraphTaskView& task, RasterReplayState& state)noexcept{
    if(state.active && state.task != record.task)
        return GpuCommandIrReplayError::InvalidRasterState;

    switch(record.opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const auto getBinding = [&record](const u8 index)noexcept -> GpuCommandIrRasterVertexBinding{
            return record.vertexBuffers[index];
        };
        if(record.vertexBuffers.size() > s_MaxVertexAttributes)
            return GpuCommandIrReplayError::InvalidRasterState;
        return __hidden_gpu_command_ir_raster_preflight::ValidateSetGraphicsStateGraph(
            record.task,
            record.pipeline,
            record.colorAttachment,
            record.framebufferOwnerIndex,
            record.indexResource,
            record.indexFormat,
            record.indexOffset,
            record.viewport,
            record.scissor,
            record.blendConstantColor,
            static_cast<u8>(record.vertexBuffers.size()),
            record.hasScissor,
            true,
            getBinding,
            graph,
            task,
            state
        );
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:
        if(!state.active || record.pipeline != state.pipeline)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.heapBound = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::SetPushConstants:
        if(!state.active || record.blobSizeBytes == 0u || record.blobSizeBytes > s_MaxPushConstantSize || (record.blobSizeBytes & (s_PushConstantSizeAlignment - 1u)) != 0u)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:
        if(!state.active || !state.heapBound || !state.pushConstantsSet || record.drawArguments.vertexCount == 0u || record.drawArguments.instanceCount == 0u)
            return GpuCommandIrReplayError::InvalidRasterDraw;
        if(record.opcode == GpuCommandIrWireOpcode::DrawIndexed && (!state.indexResource.valid() || record.drawArguments.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max)))
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

GpuCommandIrReplayError::Enum ValidateRasterWireGraphOperation(
    const GpuCommandIrWireOpcode::Enum opcode,
    const WireRecordStorage& record,
    const GpuTaskId taskId,
    const BinaryByteView blobBytes,
    const GpuTaskGraphDeclarationReadView& graph,
    const GpuTaskGraphTaskView& task,
    RasterReplayState& state
)noexcept{
    if(state.active && state.task != taskId)
        return GpuCommandIrReplayError::InvalidRasterState;

    const u64 generation = taskId.generation;
    switch(opcode){
    case GpuCommandIrWireOpcode::SetGraphicsState:{
        const GpuCommandIrSetGraphicsStateRecord& wire = record.setGraphicsState;
        const u8* const vertexPtr = wire.vertexBindingCount != 0u ? blobBytes.data() + static_cast<usize>(wire.blobOffsetBytes) : nullptr;
        const auto getBinding = [generation, vertexPtr](const u8 index)noexcept -> GpuCommandIrRasterVertexBinding{
            GpuCommandIrRasterVertexWire vertexWire;
            FOUNDATION_MEMCPY(&vertexWire, sizeof(vertexWire), vertexPtr + index * sizeof(GpuCommandIrRasterVertexWire), sizeof(GpuCommandIrRasterVertexWire));
            return GpuCommandIrRasterVertexBinding{
                .resource = GpuGraphResourceId{ .generation = generation, .index = vertexWire.resourceIndex },
                .slot = vertexWire.slot,
                .offset = vertexWire.offset,
            };
        };
        const GpuGraphPipelineId pipeline{ .generation = generation, .index = wire.pipelineIndex };
        const GpuGraphResourceId colorAttachment{ .generation = generation, .index = wire.colorAttachmentIndex };
        const GpuGraphResourceId indexResource = wire.indexResourceIndex != Limit<u32>::s_Max
            ? GpuGraphResourceId{ .generation = generation, .index = wire.indexResourceIndex }
            : GpuGraphResourceId{}
        ;
        const Viewport viewport{ wire.viewportMinX, wire.viewportMaxX, wire.viewportMinY, wire.viewportMaxY, wire.viewportMinZ, wire.viewportMaxZ };
        const Rect scissor{ wire.scissor.minX, wire.scissor.maxX, wire.scissor.minY, wire.scissor.maxY };
        const Color blendConstantColor{ wire.blendConstantColor.r, wire.blendConstantColor.g, wire.blendConstantColor.b, wire.blendConstantColor.a };
        return __hidden_gpu_command_ir_raster_preflight::ValidateSetGraphicsStateGraph(
            taskId,
            pipeline,
            colorAttachment,
            wire.framebufferOwnerIndex,
            indexResource,
            static_cast<Format::Enum>(wire.indexFormat),
            wire.indexOffset,
            viewport,
            scissor,
            blendConstantColor,
            wire.vertexBindingCount,
            wire.hasScissor != 0u,
            false,
            getBinding,
            graph,
            task,
            state
        );
    }
    case GpuCommandIrWireOpcode::BindGraphicsHeap:
        if(!state.active || GpuGraphPipelineId{ .generation = generation, .index = record.bindGraphicsHeap.pipelineIndex } != state.pipeline)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.heapBound = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::SetPushConstants:
        if(!state.active || record.setPushConstants.blobSizeBytes > s_MaxPushConstantSize)
            return GpuCommandIrReplayError::InvalidRasterState;
        state.pushConstantsSet = true;
        return GpuCommandIrReplayError::None;
    case GpuCommandIrWireOpcode::Draw:
    case GpuCommandIrWireOpcode::DrawIndexed:
        if(!state.active || !state.heapBound || !state.pushConstantsSet || record.draw.vertexCount == 0u || record.draw.instanceCount == 0u)
            return GpuCommandIrReplayError::InvalidRasterDraw;
        if(opcode == GpuCommandIrWireOpcode::DrawIndexed && (!state.indexResource.valid() || record.draw.startVertexLocation > static_cast<u32>(Limit<i32>::s_Max)))
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

GpuCommandIrReplayError::Enum ValidateRasterBuiltinBoundary(const GpuTaskId&, const RasterReplayState& state)noexcept{
    return state.active ? GpuCommandIrReplayError::InvalidRasterState : GpuCommandIrReplayError::None;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

