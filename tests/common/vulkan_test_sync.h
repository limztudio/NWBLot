// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/global.h>
#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/rhi/resource_state_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core::GraphicsBackend{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BackendTestDispatchAccess final{
public:
    [[nodiscard]] static usize InitialResourceSelectionBucketCount()noexcept{
        return CommandListResourceSelection::s_InlineCapacity * 4u;
    }
    [[nodiscard]] static usize InitialResourceSelectionBucket(Buffer* buffer)noexcept{
        return CommandListResourceSelection::HashIdentity(buffer, false) & (InitialResourceSelectionBucketCount() - 1u);
    }
    static void ValidateStateHandoff(CommandListResourceStateHandoff& states, const u16 deviceGeneration)noexcept{
        states.m_deviceGeneration = deviceGeneration;
        states.m_valid = true;
    }
    [[nodiscard]] static auto& StateHandoffTextures(CommandListResourceStateHandoff& states)noexcept{
        return states.m_textureStates;
    }
    [[nodiscard]] static const auto& StateHandoffTextures(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_textureStates;
    }
    [[nodiscard]] static auto& StateHandoffBuffers(CommandListResourceStateHandoff& states)noexcept{
        return states.m_bufferStates;
    }
    [[nodiscard]] static const auto& StateHandoffBuffers(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_bufferStates;
    }
    [[nodiscard]] static auto& StateHandoffPermanentTextures(CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentTextureStates;
    }
    [[nodiscard]] static const auto& StateHandoffPermanentTextures(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentTextureStates;
    }
    [[nodiscard]] static auto& StateHandoffPermanentBuffers(CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentBufferStates;
    }
    [[nodiscard]] static const auto& StateHandoffPermanentBuffers(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentBufferStates;
    }
    [[nodiscard]] static const auto& RetainedResources(const CommandBufferResourceReferences& references)noexcept{
        return references.m_resources;
    }
    [[nodiscard]] static const auto& RetainedBuffers(const CommandBufferResourceReferences& references)noexcept{
        return references.m_buffers;
    }
    [[nodiscard]] static const auto& RetainedTextures(const CommandBufferResourceReferences& references)noexcept{
        return references.m_textures;
    }
    [[nodiscard]] static const auto& RetainedBufferStateCommits(const CommandBufferResourceReferences& references)noexcept{
        return references.m_bufferStateCommits;
    }
    [[nodiscard]] static bool HasResourceReferenceIndex(const CommandBufferResourceReferences& references)noexcept{
        return references.m_membership.has_value();
    }
    [[nodiscard]] static usize ResourceReferenceIndexSize(const CommandBufferResourceReferences& references)noexcept{
        return references.m_membership ? references.m_membership->size() : 0u;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

