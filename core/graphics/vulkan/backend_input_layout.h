// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "backend_forward.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputLayout final : public RefCounter<GraphicsResource>, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    InputLayout(const VulkanContext& context)noexcept;
    ~InputLayout()noexcept = default;


public:
    [[nodiscard]] const VertexAttributeDesc* getAttributeDescription(u32 index)const noexcept{
        if(index >= m_attributes.size())
            return nullptr;
        return &m_attributes[index];
    }

    [[nodiscard]] u32 getNumAttributes()const noexcept{ return static_cast<u32>(m_attributes.size()); }

    [[nodiscard]] u32 getNumBindings()const noexcept{ return static_cast<u32>(m_bindings.size()); }

    [[nodiscard]] const VkVertexInputBindingDescription* getBindingDescription(u32 index)const noexcept{
        return index < m_bindings.size() ? &m_bindings[index] : nullptr;
    }


private:
    Vector<VertexAttributeDesc, Alloc::GlobalArena> m_attributes;
    Vector<VkVertexInputBindingDescription, Alloc::GlobalArena> m_bindings;
    Vector<VkVertexInputAttributeDescription, Alloc::GlobalArena> m_vkAttributes;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

