// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "backend_forward.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PipelineShaderStageVector = Vector<VkPipelineShaderStageCreateInfo, Alloc::ScratchArena>;
using PipelineSpecializationInfoVector = Vector<VkSpecializationInfo, Alloc::ScratchArena>;

struct CreatedPipelineLayout{
    VkPipelineLayout layout = VK_NULL_HANDLE;
    u32 pushConstantByteSize = 0u;
    b8 ownsLayout = false;
};

struct PipelineBindingState{
    BindingLayoutVector m_bindingLayoutsAtCreation;
    Array<u32, s_MaxBindingLayouts> m_bindingLayoutSetIndicesAtCreation{};
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    u32 m_pushConstantByteSize = 0;
    b8 m_ownsPipelineLayout = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void DestroyPipelineResource(const VulkanContext& context, PipelineBindingState& state, VkPipeline& pipeline){
    DestroyPipelineAndOwnedLayout(context, pipeline, state.m_pipelineLayout, state.m_ownsPipelineLayout);
}

inline Object GetPipelineNativeHandle(const VkPipeline pipeline, const ObjectType objectType)noexcept{
    if(objectType == ObjectTypes::s_Pipeline)
        return Object(pipeline);
    return Object(nullptr);
}

inline void AttachPipelineBindingState(VkComputePipelineCreateInfo& pipelineInfo, const PipelineBindingState& bindingState, const void* next = nullptr)noexcept{
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}

inline void AttachPipelineBindingState(VkGraphicsPipelineCreateInfo& pipelineInfo, const PipelineBindingState& bindingState, const void* next = nullptr)noexcept{
    pipelineInfo.pNext = next;
    pipelineInfo.layout = bindingState.m_pipelineLayout;
    pipelineInfo.flags |= VK_PIPELINE_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    GraphicsPipeline(const VulkanContext& context)noexcept;
    ~GraphicsPipeline()noexcept;


public:
    [[nodiscard]] const GraphicsPipelineDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }

    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const noexcept{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    GraphicsPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComputePipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    ComputePipeline(const VulkanContext& context)noexcept;
    ~ComputePipeline()noexcept;


public:
    [[nodiscard]] const ComputePipelineDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    ComputePipelineDesc m_desc;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshletPipeline final : public RefCounter<GraphicsResource>, public PipelineBindingState, NoCopy{
    friend class Device;
    friend class CommandList;


public:
    MeshletPipeline(const VulkanContext& context)noexcept;
    ~MeshletPipeline()noexcept;


public:
    [[nodiscard]] const MeshletPipelineDesc& getDescription()const noexcept{ return m_desc; }

    [[nodiscard]] u16 getDeviceGeneration()const noexcept{ return m_context.deviceGeneration; }

    [[nodiscard]] const FramebufferInfo& getFramebufferInfo()const noexcept{ return m_framebufferInfo; }
    Object getNativeHandle(ObjectType objectType)noexcept;


private:
    MeshletPipelineDesc m_desc;
    FramebufferInfo m_framebufferInfo;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    const VulkanContext& m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

