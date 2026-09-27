// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <global/global.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/rhi/resource_state_selection.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Core::GraphicsBackend{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class VulkanTestDispatchAccess final{
public:
    [[nodiscard]] static usize initialResourceSelectionBucketCount()noexcept{
        return CommandListResourceSelection::s_InlineCapacity * 4u;
    }
    [[nodiscard]] static usize initialResourceSelectionBucket(Buffer* buffer)noexcept{
        return CommandListResourceSelection::hashIdentity(buffer, false) & (initialResourceSelectionBucketCount() - 1u);
    }
    static void validateStateHandoff(CommandListResourceStateHandoff& states, const u16 deviceGeneration)noexcept{
        states.m_deviceGeneration = deviceGeneration;
        states.m_valid = true;
    }
    [[nodiscard]] static auto& stateHandoffTextures(CommandListResourceStateHandoff& states)noexcept{
        return states.m_textureStates;
    }
    [[nodiscard]] static const auto& stateHandoffTextures(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_textureStates;
    }
    [[nodiscard]] static auto& stateHandoffBuffers(CommandListResourceStateHandoff& states)noexcept{
        return states.m_bufferStates;
    }
    [[nodiscard]] static const auto& stateHandoffBuffers(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_bufferStates;
    }
    [[nodiscard]] static auto& stateHandoffPermanentTextures(CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentTextureStates;
    }
    [[nodiscard]] static const auto& stateHandoffPermanentTextures(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentTextureStates;
    }
    [[nodiscard]] static auto& stateHandoffPermanentBuffers(CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentBufferStates;
    }
    [[nodiscard]] static const auto& stateHandoffPermanentBuffers(const CommandListResourceStateHandoff& states)noexcept{
        return states.m_permanentBufferStates;
    }
    [[nodiscard]] static const auto& retainedResources(const CommandBufferResourceReferences& references)noexcept{
        return references.m_resources;
    }
    [[nodiscard]] static const auto& retainedBuffers(const CommandBufferResourceReferences& references)noexcept{
        return references.m_buffers;
    }
    [[nodiscard]] static const auto& retainedTextures(const CommandBufferResourceReferences& references)noexcept{
        return references.m_textures;
    }
    [[nodiscard]] static const auto& retainedBufferStateCommits(const CommandBufferResourceReferences& references)noexcept{
        return references.m_bufferStateCommits;
    }
    [[nodiscard]] static bool hasResourceReferenceIndex(const CommandBufferResourceReferences& references)noexcept{
        return references.m_membership.has_value();
    }
    [[nodiscard]] static usize resourceReferenceIndexSize(const CommandBufferResourceReferences& references)noexcept{
        return references.m_membership ? references.m_membership->size() : 0u;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

