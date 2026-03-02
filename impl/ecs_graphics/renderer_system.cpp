// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CubeVertex{
    f32 px;
    f32 py;
    f32 pz;
    f32 r;
    f32 g;
    f32 b;
};

static constexpr CubeVertex s_CubeVertices[8] = {
    { -0.5f, -0.5f, -0.5f, 0.9f, 0.2f, 0.2f },
    { +0.5f, -0.5f, -0.5f, 0.2f, 0.9f, 0.2f },
    { +0.5f, +0.5f, -0.5f, 0.2f, 0.2f, 0.9f },
    { -0.5f, +0.5f, -0.5f, 0.9f, 0.9f, 0.2f },
    { -0.5f, -0.5f, +0.5f, 0.9f, 0.2f, 0.9f },
    { +0.5f, -0.5f, +0.5f, 0.2f, 0.9f, 0.9f },
    { +0.5f, +0.5f, +0.5f, 0.95f, 0.95f, 0.95f },
    { -0.5f, +0.5f, +0.5f, 0.4f, 0.4f, 0.4f },
};

static constexpr u16 s_CubeIndices[36] = {
    0, 2, 1, 0, 3, 2,
    4, 5, 6, 4, 6, 7,
    0, 1, 5, 0, 5, 4,
    2, 3, 7, 2, 7, 6,
    0, 4, 7, 0, 7, 3,
    1, 2, 6, 1, 6, 5,
};

static constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
static constexpr AStringView s_CubeVertexShaderName = "engine/ecs_graphics/cube.vs";
static constexpr AStringView s_CubePixelShaderName = "engine/ecs_graphics/cube.ps";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RendererSystem::RendererSystem(Core::ECS::World& world, Core::Graphics& graphics, ShaderBinaryLookupCallback shaderBinaryLookup)
    : Core::IRenderPass(*graphics.getDeviceManager())
    , m_world(world)
    , m_graphics(graphics)
    , m_shaderBinaryLookup(Move(shaderBinaryLookup))
{
    readAccess<CubeComponent>();
    writeAccess<RendererComponent>();
}
RendererSystem::~RendererSystem()
{}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    (void)delta;

    auto cubeView = world.view<CubeComponent, RendererComponent>();
    for(auto [entity, cube, renderer] : cubeView){
        (void)entity;

        if(renderer.mesh.isValid())
            continue;

        renderer.mesh = createCubeMesh(cube);
    }
}

void RendererSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    if(!ensurePipeline(framebuffer))
        return;

    auto cubeView = m_world.view<CubeComponent, RendererComponent>();

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList)
        return;

    commandList->open();

    Core::ITexture* backBuffer = getDeviceManager().getCurrentBackBuffer();
    if(backBuffer)
        commandList->clearTextureFloat(backBuffer, Core::s_AllSubresources, __hidden_ecs_graphics::s_ClearColor);

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    for(auto [entity, cube, renderer] : cubeView){
        (void)entity;
        (void)cube;

        if(!renderer.visible || !renderer.mesh.isValid())
            continue;

        Core::GraphicsState state;
        state.setPipeline(m_pipeline.get());
        state.setFramebuffer(framebuffer);
        state.setViewport(viewportState);
        state.addVertexBuffer(Core::VertexBufferBinding().setBuffer(renderer.mesh.vertexBuffer.get()).setSlot(0).setOffset(0));
        state.setIndexBuffer(Core::IndexBufferBinding().setBuffer(renderer.mesh.indexBuffer.get()).setOffset(0).setFormat(renderer.mesh.indexFormat));

        commandList->setGraphicsState(state);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(renderer.mesh.indexCount);
        commandList->drawIndexed(drawArgs);
    }

    commandList->close();
    device->executeCommandList(commandList.get());
}

void RendererSystem::backBufferResizing(){
    m_pipeline.reset();
}

void RendererSystem::backBufferResized(u32 width, u32 height, u32 sampleCount){
    (void)width;
    (void)height;
    (void)sampleCount;

    m_pipeline.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RendererSystem::ensurePipeline(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return false;

    if(!ensureShaderLoaded(m_vertexShader, __hidden_ecs_graphics::s_CubeVertexShaderName, Core::ShaderType::Vertex, "ECSGraphics_CubeVS"))
        return false;

    if(!ensureShaderLoaded(m_pixelShader, __hidden_ecs_graphics::s_CubePixelShaderName, Core::ShaderType::Pixel, "ECSGraphics_CubePS"))
        return false;

    Core::IDevice* device = m_graphics.getDevice();

    if(!m_inputLayout){
        Core::VertexAttributeDesc attributes[2];
        attributes[0].setFormat(Core::Format::RGB32_FLOAT).setBufferIndex(0).setOffset(0).setElementStride(sizeof(__hidden_ecs_graphics::CubeVertex)).setName("POSITION");
        attributes[1].setFormat(Core::Format::RGB32_FLOAT).setBufferIndex(0).setOffset(sizeof(f32) * 3).setElementStride(sizeof(__hidden_ecs_graphics::CubeVertex)).setName("COLOR");

        m_inputLayout = device->createInputLayout(attributes, 2, m_vertexShader.get());
        if(!m_inputLayout)
            return false;
    }

    if(m_pipeline)
        return true;

    Core::GraphicsPipelineDesc desc;
    desc.setInputLayout(m_inputLayout.get());
    desc.setVertexShader(m_vertexShader.get());
    desc.setPixelShader(m_pixelShader.get());

    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.setCullNone();
    desc.setRenderState(renderState);

    m_pipeline = device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
    return m_pipeline != nullptr;
}

bool RendererSystem::ensureShaderLoaded(
    Core::ShaderHandle& outShader,
    const AStringView shaderName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName
){
    if(outShader)
        return true;

    if(!m_shaderBinaryLookup)
        return false;

    Vector<u8> shaderBinary;
    if(!m_shaderBinaryLookup(shaderName, Core::ShaderArchive::s_DefaultVariant, shaderBinary))
        return false;

    if(shaderBinary.empty() || (shaderBinary.size() & 3u) != 0u)
        return false;

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType);
    shaderDesc.setDebugName(debugName);

    Core::IDevice* device = m_graphics.getDevice();
    outShader = device->createShader(
        shaderDesc,
        shaderBinary.data(),
        shaderBinary.size()
    );
    if(!outShader)
        return false;

    return true;
}

Core::Graphics::MeshResource RendererSystem::createCubeMesh(const CubeComponent& cube)const{
    Array<__hidden_ecs_graphics::CubeVertex, 8> vertices{};
    for(usize i = 0; i < vertices.size(); ++i){
        vertices[i] = __hidden_ecs_graphics::s_CubeVertices[i];
        vertices[i].px *= cube.size;
        vertices[i].py *= cube.size;
        vertices[i].pz *= cube.size;
        vertices[i].r *= cube.color.r;
        vertices[i].g *= cube.color.g;
        vertices[i].b *= cube.color.b;
    }

    Core::Graphics::MeshSetupDesc desc;
    desc.vertexData = vertices.data();
    desc.vertexDataSize = sizeof(vertices);
    desc.vertexStride = sizeof(__hidden_ecs_graphics::CubeVertex);
    desc.vertexBufferName = "ECSGraphics_CubeVB";
    desc.indexData = __hidden_ecs_graphics::s_CubeIndices;
    desc.indexDataSize = sizeof(__hidden_ecs_graphics::s_CubeIndices);
    desc.use32BitIndices = false;
    desc.indexBufferName = "ECSGraphics_CubeIB";

    return m_graphics.setupMesh(desc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

