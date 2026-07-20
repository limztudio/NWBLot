#include "system.h"

#include <core/graphics/backend_selection.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <impl/assets/graphics/imgui/names.h>
#include <impl/assets_shader/loader.h>
#include <core/common/log.h>

#include <global/algorithm.h>

#include <cstddef>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr usize s_DefaultVertexCapacity = 4096u;
static constexpr usize s_DefaultIndexCapacity = 8192u;
static constexpr Name s_UiVertexBufferName("ecs_ui/imgui_vertices");
static constexpr Name s_UiIndexBufferName("ecs_ui/imgui_indices");

static Core::RenderState BuildUiRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().enableScissor().setCullNone();
    renderState.blendState.targets[NWB_IMGUI_COLOR_TARGET_LOCATION]
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::SrcAlpha)
        .setDestBlend(Core::BlendFactor::InvSrcAlpha)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::InvSrcAlpha)
        .setBlendOpAlpha(Core::BlendOp::Add)
    ;
    return renderState;
}

static bool LoadShader(
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    UiSystem::ShaderPathResolveCallback& shaderPathResolver,
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName
){
    return ShaderAssetLoader::Load(
        outShader,
        shaderName,
        Core::ShaderArchive::s_DefaultVariant,
        shaderType,
        debugName,
        graphics,
        assetManager,
        shaderPathResolver,
        NWB_TEXT("UiSystem")
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiSystem::ensureRenderResources(Core::Framebuffer* framebuffer){
    if(!framebuffer)
        return false;

    auto* device = m_graphics.getDevice();
    if(!m_bindingLayout){
        static_assert(sizeof(UiPushConstants) <= Core::s_MaxPushConstantSize, "Ui push constants must fit the portable push constant budget");

        Core::BindingLayoutDesc bindingLayoutDesc(m_arena);
        bindingLayoutDesc.setVisibility(Core::ShaderType::AllGraphics);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(NWB_IMGUI_BINDING_TEXTURE, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(NWB_IMGUI_BINDING_SAMPLER, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(UiPushConstants)));

        m_bindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_bindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create binding layout"));
            return false;
        }
    }

    if(!m_sampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(true)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_sampler = device->createSampler(samplerDesc);
        if(!m_sampler){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create sampler"));
            return false;
        }
    }

    if(!ensureShadersLoaded() || !ensureInputLayout())
        return false;

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();
    if(m_pipeline && m_pipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setInputLayout(m_inputLayout)
        .setVertexShader(m_vertexShader)
        .setPixelShader(m_pixelShader)
        .setRenderState(__hidden_ui::BuildUiRenderState())
        .addBindingLayout(m_bindingLayout)
    ;

    m_pipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_pipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create graphics pipeline"));
        return false;
    }

    return true;
}

bool UiSystem::ensureShadersLoaded(){
    if(!m_vertexShader){
        if(!__hidden_ui::LoadShader(
            m_graphics,
            m_assetManager,
            m_shaderPathResolver,
            m_vertexShader,
            AssetsGraphicsImGui::s_VertexShaderName,
            Core::ShaderType::Vertex,
            Name("ECSUI_ImGuiVS")
        ))
            return false;
    }

    if(!m_pixelShader){
        if(!__hidden_ui::LoadShader(
            m_graphics,
            m_assetManager,
            m_shaderPathResolver,
            m_pixelShader,
            AssetsGraphicsImGui::s_PixelShaderName,
            Core::ShaderType::Pixel,
            Name("ECSUI_ImGuiPS")
        ))
            return false;
    }

    return true;
}

bool UiSystem::ensureInputLayout(){
    if(m_inputLayout)
        return true;
    if(!m_vertexShader)
        return false;

    Core::VertexAttributeDesc attributes[NWB_IMGUI_VERTEX_ATTRIBUTE_COUNT];
    attributes[NWB_IMGUI_VERTEX_POSITION_LOCATION]
        .setFormat(Core::Format::RG32_FLOAT)
        .setBufferIndex(NWB_IMGUI_VERTEX_BUFFER_INDEX)
        .setOffset(offsetof(ImDrawVert, pos))
        .setElementStride(sizeof(ImDrawVert))
        .setName("POSITION")
    ;
    attributes[NWB_IMGUI_VERTEX_UV_LOCATION]
        .setFormat(Core::Format::RG32_FLOAT)
        .setBufferIndex(NWB_IMGUI_VERTEX_BUFFER_INDEX)
        .setOffset(offsetof(ImDrawVert, uv))
        .setElementStride(sizeof(ImDrawVert))
        .setName("TEXCOORD")
    ;
    attributes[NWB_IMGUI_VERTEX_COLOR_LOCATION]
        .setFormat(Core::Format::RGBA8_UNORM)
        .setBufferIndex(NWB_IMGUI_VERTEX_BUFFER_INDEX)
        .setOffset(offsetof(ImDrawVert, col))
        .setElementStride(sizeof(ImDrawVert))
        .setName("COLOR")
    ;

    m_inputLayout = m_graphics.getDevice()->createInputLayout(
        attributes,
        NWB_IMGUI_VERTEX_ATTRIBUTE_COUNT,
        m_vertexShader.get()
    );
    if(!m_inputLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create input layout"));
        return false;
    }

    return true;
}

bool UiSystem::ensureBuffers(const usize vertexCount, const usize indexCount){
    if(vertexCount == 0 || indexCount == 0)
        return true;
#if defined(NWB_DEBUG)
    if(vertexCount > Limit<usize>::s_Max / sizeof(ImDrawVert) || indexCount > Limit<usize>::s_Max / sizeof(ImDrawIdx)){
        NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: draw buffer request overflows addressable memory"));
        return false;
    }
#endif

    auto* device = m_graphics.getDevice();
    if(!m_vertexBuffer || m_vertexBufferCapacity < vertexCount){
        const usize capacity = ::NextGrowingCapacity(
            m_vertexBufferCapacity,
            vertexCount,
            __hidden_ui::s_DefaultVertexCapacity
        );

        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(static_cast<u64>(capacity * sizeof(ImDrawVert)))
            .setIsVertexBuffer(true)
            .setDebugName(__hidden_ui::s_UiVertexBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;

        m_vertexBuffer = device->createBuffer(bufferDesc);
        if(!m_vertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create vertex buffer"));
            return false;
        }
        m_vertexBufferCapacity = capacity;
    }

    if(!m_indexBuffer || m_indexBufferCapacity < indexCount){
        const usize capacity = ::NextGrowingCapacity(
            m_indexBufferCapacity,
            indexCount,
            __hidden_ui::s_DefaultIndexCapacity
        );

        Core::BufferDesc bufferDesc;
        bufferDesc
            .setByteSize(static_cast<u64>(capacity * sizeof(ImDrawIdx)))
            .setIsIndexBuffer(true)
            .setDebugName(__hidden_ui::s_UiIndexBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;

        m_indexBuffer = device->createBuffer(bufferDesc);
        if(!m_indexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("UiSystem: failed to create index buffer"));
            return false;
        }
        m_indexBufferCapacity = capacity;
    }

    return true;
}

bool UiSystem::drawBuffersReady(const usize vertexCount, const usize indexCount)const{
    if(vertexCount == 0 || indexCount == 0)
        return true;

    return
        m_vertexBuffer
        && m_indexBuffer
        && m_vertexBufferCapacity >= vertexCount
        && m_indexBufferCapacity >= indexCount
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

