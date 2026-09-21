// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "ui_internal.h"

#include <core/graphics/backend_selection.h>
#include <core/task/gpu/task_graph.h>

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_TaskGraphDeclarationArena("impl/ecs_ui/task_graph");

[[nodiscard]] inline bool HasPendingTextureUploads(const ImDrawData& drawData){
#if defined(IMGUI_HAS_TEXTURES)
    if(!drawData.Textures)
        return false;

    for(i32 i = 0; i < drawData.Textures->Size; ++i){
        const ImTextureData* const textureData = drawData.Textures->Data[i];
        if(
            textureData
            && (
                textureData->Status == ImTextureStatus_WantCreate
                || textureData->Status == ImTextureStatus_WantUpdates
            )
        )
            return true;
    }
#else
    static_cast<void>(drawData);
#endif

    return false;
}

[[nodiscard]] inline Core::GpuTaskResourceUse ReadTextureUse(const Core::GpuGraphResourceId resource){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = Core::ResourceStates::ShaderResource,
        .access = Core::GpuTaskResourceAccess::Read,
    };
}

inline void AppendTextureReadUse(
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>& resourceUses,
    const Core::GpuGraphResourceId texture){
    for(const Core::GpuTaskResourceUse& use : resourceUses){
        if(use.resource == texture)
            return;
    }
    resourceUses.push_back(ReadTextureUse(texture));
}

// Shared ImGui upload-completion declare: build the deduped texture-read uses plus the tiny Graphics completion
// scheduling. The caller owns identities, queue choice, dependencies, and the final addTask call.
struct UploadCompletionDeclare{
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses;
    Core::GpuTaskSchedulingHint scheduling;
    Core::GpuTaskDesc desc;
};

[[nodiscard]] inline UploadCompletionDeclare MakeUploadCompletionDeclare(
    Core::Alloc::ScratchArena& scratchArena,
    const Vector<Core::GpuGraphResourceId, Core::Alloc::ScratchArena>& uploadedTextures,
    const Name& identity,
    const char* markerLabel
){
    UploadCompletionDeclare result{ Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena>(scratchArena) };
    result.resourceUses.reserve(uploadedTextures.size());
    for(const Core::GpuGraphResourceId texture : uploadedTextures)
        AppendTextureReadUse(result.resourceUses, texture);
    result.scheduling.cost = Core::GpuTaskCostHint::Tiny;
    result.scheduling.avoidQueueCrossing = true;
    result.scheduling.forceSubmissionBoundary = true;
    result.scheduling.allowPacketMerge = false;
    result.desc
        .setIdentity(identity)
        .setMarkerLabel(markerLabel)
        .setQueue(Core::GpuQueueRequest{
            Core::GpuQueueCapability::Graphics,
            Core::GpuQueuePreference::Graphics,
            false,
            false,
        })
        .setScheduling(result.scheduling)
    ;
    return result;
}

[[nodiscard]] inline bool ValidAcquiredPresentationFrame(const Core::AcquiredPresentationFrame& frame){
    if(!frame.valid())
        return false;

    const Core::FramebufferDesc& framebufferDesc = frame.framebuffer->getDescription();
    return framebufferDesc.colorAttachments.size() == 1u
        && framebufferDesc.colorAttachments[0].texture == frame.backBuffer.texture.get()
        && !framebufferDesc.depthAttachment.valid()
        && !framebufferDesc.shadingRateAttachment.valid()
    ;
}

[[nodiscard]] inline bool GraphBindsAcquiredPresentationTexture(
    const Core::GpuTaskGraph::DeclarationReadView& graph,
    const Core::AcquiredPresentationFrame& frame,
    const Core::GpuGraphResourceId backbuffer
){
    return backbuffer.valid() && graph.textureForResource(backbuffer) == frame.backBuffer.texture.get();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

