// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "backend.h"
#include "arena_names.h"
#include "command_validation.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace VulkanDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr usize s_MaxGraphicsPipelineShaderStageCount = 5u;




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


GraphicsPipeline::GraphicsPipeline(const VulkanContext& context)
    : RefCounter<GraphicsResource>(context.cpuScheduler)
    , m_context(context)
{}
GraphicsPipeline::~GraphicsPipeline(){
    VulkanDetail::DestroyPipelineResource(m_context, *this, m_pipeline);
}
Object GraphicsPipeline::getNativeHandle(ObjectType objectType){
    return VulkanDetail::GetPipelineNativeHandle(m_pipeline, objectType);
}



FramebufferHandle Device::createFramebuffer(const FramebufferDesc& desc){
    for(u32 i = 0u; i < static_cast<u32>(desc.colorAttachments.size()); ++i){
        Texture* const texture = desc.colorAttachments[i].texture;
        if(texture && &texture->m_context != &m_context){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Failed to create framebuffer: color attachment {} belongs to another device."),
                i
            );
            return nullptr;
        }
    }
    if(desc.depthAttachment.texture && &desc.depthAttachment.texture->m_context != &m_context){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create framebuffer: depth attachment belongs to another device."));
        return nullptr;
    }
    if(desc.shadingRateAttachment.texture && &desc.shadingRateAttachment.texture->m_context != &m_context){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Failed to create framebuffer: shading-rate attachment belongs to another device.")
        );
        return nullptr;
    }

    auto* fb = NewArenaObject<Framebuffer>(m_context.objectArena, m_context);
    fb->m_desc = desc;
    fb->m_framebufferInfo = FramebufferInfoEx(desc);

    constexpr u32 s_MaxColorAttachments = s_MaxRenderTargets;
    const u32 colorAttachmentCount = Min<u32>(static_cast<u32>(desc.colorAttachments.size()), s_MaxColorAttachments);
    if(desc.colorAttachments.size() > s_MaxColorAttachments)
        NWB_LOGGER_WARNING(NWB_TEXT("Vulkan: Framebuffer has more than {} color attachments; truncating to {}."), s_MaxColorAttachments, s_MaxColorAttachments);

    fb->m_resources.reserve(
        static_cast<usize>(colorAttachmentCount)
        + (desc.depthAttachment.texture ? 1u : 0u)
        + (desc.shadingRateAttachment.texture ? 1u : 0u)
    );
    for(u32 i = 0u; i < colorAttachmentCount; ++i){
        if(desc.colorAttachments[i].texture)
            fb->m_resources.emplace_back(desc.colorAttachments[i].texture, TextureHandle::deleter_type(&m_context.objectArena));
    }

    if(desc.depthAttachment.texture)
        fb->m_resources.emplace_back(desc.depthAttachment.texture, TextureHandle::deleter_type(&m_context.objectArena));
    if(desc.shadingRateAttachment.texture)
        fb->m_resources.emplace_back(desc.shadingRateAttachment.texture, TextureHandle::deleter_type(&m_context.objectArena));

    return FramebufferHandle(fb, FramebufferHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


GraphicsPipelineHandle Device::createGraphicsPipeline(const GraphicsPipelineDesc& desc, FramebufferInfo const& fbinfo){
    if(!m_context.extensions.KHR_dynamic_rendering){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Dynamic rendering extension is required to create graphics pipelines."));
        return nullptr;
    }
    if(fbinfo.colorFormats.size() > m_context.physicalDeviceProperties.limits.maxColorAttachments){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline color count exceeds the device limit."));
        return nullptr;
    }
    const VkPrimitiveTopology primitiveTopology = VulkanDetail::GetPrimitiveTopology(desc.primType);
    if(primitiveTopology == VK_PRIMITIVE_TOPOLOGY_MAX_ENUM){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline primitive topology is invalid."));
        return nullptr;
    }
    if(desc.renderState.rasterState.depthBiasClamp != 0.0f){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Vulkan: Graphics pipeline depthBiasClamp requires an unsupported logical-device feature.")
        );
        return nullptr;
    }
    if(desc.shadingRateState.enabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline variable-rate shading is not implemented."));
        return nullptr;
    }
    if(desc.renderState.singlePassStereo.enabled){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline single-pass stereo is not implemented."));
        return nullptr;
    }
    if(!desc.VS){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: vertex shader is required"));
        return nullptr;
    }

    const auto validateShader = [this](
        Shader* const shader,
        const ShaderType::Mask expectedType,
        const TStringView stageName
    ){
        if(!shader)
            return true;
        if(&shader->m_context != &m_context){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline {} shader belongs to another device."), stageName);
            return false;
        }
        if(shader->m_shaderModule == VK_NULL_HANDLE || shader->m_desc.shaderType != expectedType){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Vulkan: Graphics pipeline {} shader has an invalid module or stage."),
                stageName
            );
            return false;
        }
        return true;
    };
    if(
        !validateShader(desc.VS.get(), ShaderType::Vertex, NWB_TEXT("vertex"))
        || !validateShader(desc.HS.get(), ShaderType::Hull, NWB_TEXT("hull"))
        || !validateShader(desc.DS.get(), ShaderType::Domain, NWB_TEXT("domain"))
        || !validateShader(desc.GS.get(), ShaderType::Geometry, NWB_TEXT("geometry"))
        || !validateShader(desc.PS.get(), ShaderType::Pixel, NWB_TEXT("pixel"))
    )
        return nullptr;
    if(desc.inputLayout && &desc.inputLayout->m_context != &m_context){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Graphics pipeline input layout belongs to another device."));
        return nullptr;
    }

    Alloc::ScratchArena scratchArena(VulkanArenaScope::s_GraphicsPipelineArena, s_GraphicsPipelineScratchArenaBytes);

    auto* pso = NewArenaObject<GraphicsPipeline>(m_context.objectArena, m_context);
    pso->m_desc = desc;
    pso->m_framebufferInfo = fbinfo;

    const bool hasTessellationControlShader = static_cast<bool>(desc.HS);
    const bool hasTessellationEvaluationShader = static_cast<bool>(desc.DS);
    const bool usesTessellation = hasTessellationControlShader || hasTessellationEvaluationShader || desc.primType == PrimitiveType::PatchList || desc.patchControlPoints > 0;

    if(hasTessellationControlShader != hasTessellationEvaluationShader){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation control and evaluation shaders must both be provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }
    if(usesTessellation){
        if(!hasTessellationControlShader || !hasTessellationEvaluationShader){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: patch topology requires tessellation shaders"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.primType != PrimitiveType::PatchList){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation shaders require patch-list topology"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.patchControlPoints == 0){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: tessellation patch control point count is zero"));
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
        if(desc.patchControlPoints > m_context.physicalDeviceProperties.limits.maxTessellationPatchSize){
            NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: patch control point count {} exceeds device limit {}")
                , desc.patchControlPoints
                , m_context.physicalDeviceProperties.limits.maxTessellationPatchSize
            );
            DestroyArenaObject(m_context.objectArena, pso);
            return nullptr;
        }
    }

    PipelineShaderStageVector shaderStages{ scratchArena };
    PipelineSpecializationInfoVector specInfos{ scratchArena };
    shaderStages.reserve(VulkanDetail::s_MaxGraphicsPipelineShaderStageCount);
    specInfos.reserve(VulkanDetail::s_MaxGraphicsPipelineShaderStageCount);

    if(desc.VS)
        appendPipelineShaderStage(*desc.VS, VK_SHADER_STAGE_VERTEX_BIT, specInfos, shaderStages);
    if(desc.HS)
        appendPipelineShaderStage(*desc.HS, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, specInfos, shaderStages);
    if(desc.DS)
        appendPipelineShaderStage(*desc.DS, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, specInfos, shaderStages);
    if(desc.GS)
        appendPipelineShaderStage(*desc.GS, VK_SHADER_STAGE_GEOMETRY_BIT, specInfos, shaderStages);
    if(desc.PS)
        appendPipelineShaderStage(*desc.PS, VK_SHADER_STAGE_FRAGMENT_BIT, specInfos, shaderStages);

    if(shaderStages.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Vulkan: Failed to create graphics pipeline: no shader stages provided"));
        DestroyArenaObject(m_context.objectArena, pso);
        return nullptr;
    }

    if(!configurePipelineBindingsOrDestroy(
        desc.bindingLayouts,
        NWB_TEXT("graphics pipeline"),
        *pso,
        scratchArena
    ))
        return nullptr;

    auto vertexInputInfo = VulkanDetail::MakeVkStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    if(desc.inputLayout){
        auto* layout = desc.inputLayout.get();

        vertexInputInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(layout->m_bindings.size());
        vertexInputInfo.pVertexBindingDescriptions = layout->m_bindings.data();
        vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(layout->m_vkAttributes.size());
        vertexInputInfo.pVertexAttributeDescriptions = layout->m_vkAttributes.data();
    }

    auto inputAssembly = VulkanDetail::MakeVkStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    inputAssembly.topology = primitiveTopology;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    auto tessellationState = VulkanDetail::MakeVkStruct<VkPipelineTessellationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_TESSELLATION_STATE_CREATE_INFO);
    tessellationState.patchControlPoints = desc.patchControlPoints;

    const RasterState& rasterState = desc.renderState.rasterState;
    auto rasterizer = VulkanDetail::BuildPipelineRasterizationState(
        rasterState,
        VulkanDetail::ConvertFillMode(rasterState.fillMode),
        rasterState.depthClipEnable ? VK_FALSE : VK_TRUE
    );

    VkDynamicState dynamicStates[] = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_LINE_WIDTH,
        VK_DYNAMIC_STATE_DEPTH_BIAS,
        VK_DYNAMIC_STATE_BLEND_CONSTANTS,
        VK_DYNAMIC_STATE_DEPTH_BOUNDS,
        VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
        VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
        VK_DYNAMIC_STATE_STENCIL_REFERENCE,
    };
    VulkanDetail::GraphicsPipelineFixedState fixedState{ scratchArena };
    if(!buildGraphicsPipelineFixedStateOrDestroy(
        fbinfo,
        desc.renderState,
        VulkanDetail::PipelineStencilFaceMode::IncludeStencilFaces,
        dynamicStates,
        static_cast<u32>(LengthOf(dynamicStates)),
        NWB_TEXT("graphics pipeline"),
        *pso,
        fixedState
    ))
        return nullptr;

    auto pipelineInfo = VulkanDetail::MakeVkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    VulkanDetail::AttachPipelineBindingState(pipelineInfo, *pso, &fixedState.renderingInfo);
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pTessellationState = (desc.patchControlPoints > 0) ? &tessellationState : nullptr;
    VulkanDetail::AttachGraphicsPipelineFixedState(pipelineInfo, rasterizer, fixedState);
    pipelineInfo.renderPass = VK_NULL_HANDLE;
    pipelineInfo.subpass = 0;

    if(!createPipelineOrDestroy(NWB_TEXT("graphics pipeline"), *pso, pipelineInfo))
        return nullptr;

    return GraphicsPipelineHandle(pso, GraphicsPipelineHandle::deleter_type(&m_context.objectArena), AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_VULKAN_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

