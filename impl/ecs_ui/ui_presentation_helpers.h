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

