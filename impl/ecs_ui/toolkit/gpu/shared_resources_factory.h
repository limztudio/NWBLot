// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_internal.h"

#include <impl/assets_shader/loader.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_gpu_shared{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Core::RenderState PaintRenderState()noexcept{
    Core::RenderState state;
    state.depthStencilState.disableDepthTest().disableDepthWrite();
    state.rasterState.enableDepthClip().setCullNone();
    state.blendState.targets[NWB_UI_COLOR_TARGET_LOCATION]
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::InvSrcAlpha)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::InvSrcAlpha)
        .setBlendOpAlpha(Core::BlendOp::Add)
    ;
    return state;
}

template<typename TShader>
[[nodiscard]] inline b8 LoadShader(
    const Core::Assets::AssetRef<TShader> UiSubsystem::* const member,
    const Name& identity,
    GpuRendererState& state,
    Core::ShaderHandle& outShader
){
    if(!state.m_activeSceneSubsystems)
        return false;
    const UiSubsystem* const uiSubsystem = state.m_activeSceneSubsystems->findSubsystem<UiSubsystem>();
    if(!uiSubsystem)
        return false;
    const Core::Assets::AssetRef<TShader>& resolved = uiSubsystem->*member;
    if(!resolved.valid())
        return false;
    return ShaderAssetLoader::Load<TShader>(resolved.name(), Core::ShaderArchive::s_DefaultVariant, identity, s_GpuRendererContext, state.m_graphics, state.m_assets, state.m_resolver, nullptr, outShader);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GpuSharedResourcesFactory final{
public:
    [[nodiscard]] b8 create(GpuRendererState& state)const{
        if(state.m_resources)
            return true;
        Core::Device& device = state.m_graphics.getDevice();
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        if(!heap.isInitialized())
            return false;
        auto resourcesResult = MakeGpuVersion<GpuSharedResources>(state.m_arena, state.m_graphics);
        if(!resourcesResult)
            return false;
        GpuVersion<GpuSharedResources> resources = Move(*resourcesResult);
        if(
            !__hidden_ui_gpu_shared::LoadShader<VertexShader>(&UiSubsystem::vs, s_GpuVertexShaderName, state, resources->m_vertexShader)
            || !__hidden_ui_gpu_shared::LoadShader<PixelShader>(&UiSubsystem::ps, s_GpuPixelShaderName, state, resources->m_pixelShader)
            || !__hidden_ui_gpu_shared::LoadShader<VertexShader>(&UiSubsystem::outputVs, s_GpuOutputVertexShaderName, state, resources->m_outputVertexShader)
            || !__hidden_ui_gpu_shared::LoadShader<PixelShader>(&UiSubsystem::outputPs, s_GpuOutputPixelShaderName, state, resources->m_outputPixelShader)
        )
            return false;
        Array<Core::VertexAttributeDesc, NWB_UI_VERTEX_ATTRIBUTE_COUNT> attributes{};
        attributes[NWB_UI_VERTEX_POSITION_LOCATION].setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_POSITION_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(s_GpuVertexPositionSemantic);
        attributes[NWB_UI_VERTEX_UV_LOCATION].setFormat(Core::Format::RG32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_UV_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(s_GpuVertexTexCoordSemantic);
        attributes[NWB_UI_VERTEX_COLOR_LOCATION].setFormat(Core::Format::RGBA32_FLOAT).setBufferIndex(NWB_UI_VERTEX_BUFFER_INDEX).setOffset(NWB_UI_VERTEX_COLOR_BYTE_OFFSET).setElementStride(sizeof(Vertex)).setName(s_GpuVertexColorSemantic);
        auto inputLayoutResult = device.createInputLayout(attributes.data(), NWB_UI_VERTEX_ATTRIBUTE_COUNT, resources->m_vertexShader.get());
        if(inputLayoutResult)
            resources->m_inputLayout = Move(*inputLayoutResult);
        else
            resources->m_inputLayout.reset();
        if(!inputLayoutResult)
            return false;
        Core::BindingLayoutDesc paintLayout(state.m_arena);
        paintLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuPaintPushConstants)));
        auto layoutResult = device.createBindingLayout(paintLayout);
        if(layoutResult)
            resources->m_layout = Move(*layoutResult);
        else
            resources->m_layout.reset();
        Core::BindingLayoutDesc outputLayout(state.m_arena);
        outputLayout.setVisibility(Core::ShaderType::AllGraphics).addItem(Core::BindingLayoutItem::PushConstants(0u, sizeof(GpuOutputPushConstants)));
        auto outputLayoutResult = device.createBindingLayout(outputLayout);
        if(outputLayoutResult)
            resources->m_outputLayout = Move(*outputLayoutResult);
        else
            resources->m_outputLayout.reset();
        Core::SamplerDesc sampler;
        sampler.setAllFilters(true).setAllAddressModes(Core::SamplerAddressMode::Clamp);
        auto samplerResult = device.createSampler(sampler);
        if(samplerResult)
            resources->m_sampler = Move(*samplerResult);
        else
            resources->m_sampler.reset();
        if(!layoutResult || !outputLayoutResult || !samplerResult)
            return false;
        const Expected<Core::GpuDescriptorHandle> samplerDescriptor = heap.allocate(Core::GpuDescriptorClass::Sampler);
        if(!samplerDescriptor)
            return false;
        resources->m_samplerDescriptor = *samplerDescriptor;
        if(!heap.write(resources->m_samplerDescriptor, Core::DescriptorWriteItem::Sampler(0u, resources->m_sampler.get())))
            return false;
        Core::GraphicsPipelineDesc pipeline;
        pipeline
            .setInputLayout(resources->m_inputLayout)
            .setVertexShader(resources->m_vertexShader)
            .setPixelShader(resources->m_pixelShader)
            .setRenderState(__hidden_ui_gpu_shared::PaintRenderState())
            .addBindingLayout(resources->m_layout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        auto pipelineResult = device.createGraphicsPipeline(pipeline, Core::FramebufferInfo().addColorFormat(Core::Format::RGBA16_FLOAT));
        if(pipelineResult)
            resources->m_pipeline = Move(*pipelineResult);
        else
            resources->m_pipeline.reset();
        if(!pipelineResult)
            return false;
        state.m_resources = Move(resources);
        return true;
    }

    [[nodiscard]] b8 prepareOutputPipeline(GpuRendererState& state, const Core::FramebufferInfo& info)const{
        if(state.m_resources->m_outputPipeline && state.m_resources->m_outputPipeline->getFramebufferInfo() == info)
            return true;
        Core::Device& device = state.m_graphics.getDevice();
        Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
        Core::RenderState renderState;
        renderState.depthStencilState.disableDepthTest().disableDepthWrite();
        renderState.rasterState.setCullNone();
        Core::GraphicsPipelineDesc description;
        description
            .setVertexShader(state.m_resources->m_outputVertexShader)
            .setPixelShader(state.m_resources->m_outputPixelShader)
            .setRenderState(renderState)
            .addBindingLayout(state.m_resources->m_outputLayout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        auto outputPipelineResult = device.createGraphicsPipeline(description, info);
        if(outputPipelineResult)
            state.m_resources->m_outputPipeline = Move(*outputPipelineResult);
        else
            state.m_resources->m_outputPipeline.reset();
        return static_cast<b8>(outputPipelineResult);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
