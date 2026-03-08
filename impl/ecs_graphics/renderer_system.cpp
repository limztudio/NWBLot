// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include <core/graphics/shader_archive.h>
#include <logger/client/logger.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_asset.h>
#include <impl/assets_graphics/shader_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
static constexpr u32 s_PositionColorVertexStride = sizeof(f32) * 6u;

static const Name& StageNameFromShaderType(const Core::ShaderType::Mask shaderType){
    switch(shaderType){
        case Core::ShaderType::Vertex: { static const Name s("vs"); return s; }
        case Core::ShaderType::Hull: { static const Name s("hs"); return s; }
        case Core::ShaderType::Domain: { static const Name s("ds"); return s; }
        case Core::ShaderType::Geometry: { static const Name s("gs"); return s; }
        case Core::ShaderType::Pixel: { static const Name s("ps"); return s; }
        case Core::ShaderType::Compute: { static const Name s("cs"); return s; }
        case Core::ShaderType::Amplification: { static const Name s("task"); return s; }
        case Core::ShaderType::Mesh: { static const Name s("mesh"); return s; }
        case Core::ShaderType::RayGeneration: { static const Name s("rgen"); return s; }
        case Core::ShaderType::AnyHit: { static const Name s("rahit"); return s; }
        case Core::ShaderType::ClosestHit: { static const Name s("rchit"); return s; }
        case Core::ShaderType::Miss: { static const Name s("rmiss"); return s; }
        case Core::ShaderType::Intersection: { static const Name s("rint"); return s; }
        case Core::ShaderType::Callable: { static const Name s("rcall"); return s; }
        default: return NAME_NONE;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    seed ^= static_cast<usize>(key.framebufferInfo.depthFormat) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= Hasher<u32>{}(key.framebufferInfo.sampleCount) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= Hasher<u32>{}(key.framebufferInfo.sampleQuality) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        seed ^= static_cast<usize>(format) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material
        && lhs.framebufferInfo == rhs.framebufferInfo;
}


RendererSystem::RendererSystem(
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::IRenderPass(*graphics.getDeviceManager())
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
{
    readAccess<RendererComponent>();
}
RendererSystem::~RendererSystem()
{}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    (void)world;
    (void)delta;
}

void RendererSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();

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

    for(auto&& [entity, renderer] : rendererView){
        (void)entity;

        if(!renderer.visible)
            continue;

        Core::Graphics::MeshResource mesh;
        if(!ensureGeometryLoaded(renderer.geometry, mesh))
            continue;
        if(!mesh.valid())
            continue;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!ensureRendererPipeline(renderer, framebuffer, pipelineResources))
            continue;
        if(!pipelineResources || !pipelineResources->pipeline)
            continue;

        Core::GraphicsState state;
        state.setPipeline(pipelineResources->pipeline.get());
        state.setFramebuffer(framebuffer);
        state.setViewport(viewportState);
        state.addVertexBuffer(Core::VertexBufferBinding().setBuffer(mesh.vertexBuffer.get()).setSlot(0).setOffset(0));
        state.setIndexBuffer(Core::IndexBufferBinding().setBuffer(mesh.indexBuffer.get()).setOffset(0).setFormat(mesh.indexFormat));

        commandList->setGraphicsState(state);

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(mesh.indexCount);
        commandList->drawIndexed(drawArgs);
    }

    commandList->close();
    device->executeCommandList(commandList.get());
}

void RendererSystem::backBufferResizing(){
    m_materialPipelines.clear();
}

void RendererSystem::backBufferResized(u32 width, u32 height, u32 sampleCount){
    (void)width;
    (void)height;
    (void)sampleCount;

    m_materialPipelines.clear();
}


bool RendererSystem::ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, Core::Graphics::MeshResource& outMesh){
    outMesh = {};

    const Name geometryPath = geometryAsset.name();
    if(!geometryPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer geometry is empty"));
        return false;
    }

    const auto foundMesh = m_geometryMeshes.find(geometryPath);
    if(foundMesh != m_geometryMeshes.end()){
        outMesh = foundMesh.value();
        return outMesh.valid();
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Geometry::AssetTypeName(), geometryPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to load geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Geometry::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: asset '{}' is not geometry"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(*loadedAsset);
    if(geometry.vertexStride() != __hidden_ecs_graphics::s_PositionColorVertexStride){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' has unsupported vertex stride {}"),
            StringConvert(geometryPath.c_str()),
            geometry.vertexStride()
        );
        return false;
    }

    Core::Graphics::MeshSetupDesc meshDesc;
    meshDesc.vertexData = geometry.vertexData().data();
    meshDesc.vertexDataSize = geometry.vertexData().size();
    meshDesc.vertexStride = geometry.vertexStride();
    meshDesc.vertexBufferName = DeriveName(geometryPath, AStringView(":vb"));
    meshDesc.indexData = geometry.indexData().data();
    meshDesc.indexDataSize = geometry.indexData().size();
    meshDesc.use32BitIndices = geometry.use32BitIndices();
    meshDesc.indexBufferName = DeriveName(geometryPath, AStringView(":ib"));
    if(!meshDesc.vertexBufferName || !meshDesc.indexBufferName){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to derive mesh buffer names for geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    Core::Graphics::MeshResource createdMesh = m_graphics.setupMesh(meshDesc);
    if(!createdMesh.valid()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to upload geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    m_geometryMeshes[geometryPath] = createdMesh;
    outMesh = createdMesh;
    return true;
}


bool RendererSystem::ensureRendererPipeline(const RendererComponent& renderer, Core::IFramebuffer* framebuffer, MaterialPipelineResources*& outResources){
    outResources = nullptr;

    if(!framebuffer)
        return false;

    const Name materialKey = renderer.material.name();
    if(!materialKey){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    MaterialPipelineKey pipelineKey;
    pipelineKey.material = materialKey;
    pipelineKey.framebufferInfo = framebuffer->getFramebufferInfo();

    auto [it, inserted] = m_materialPipelines.emplace(pipelineKey, MaterialPipelineResources{});
    MaterialPipelineResources& resources = it.value();
    if(resources.pipeline){
        outResources = &resources;
        return true;
    }

    Core::Assets::AssetRef<Shader> vertexShaderAsset;
    Core::Assets::AssetRef<Shader> pixelShaderAsset;
    CompactString shaderVariant(Core::ShaderArchive::s_DefaultVariant);
    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Material::AssetTypeName(), materialKey, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to load material '{}'"),
            StringConvert(materialKey.c_str())
        );
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: asset '{}' is not a material"),
            StringConvert(materialKey.c_str())
        );
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);
    shaderVariant = material.shaderVariant().empty()
        ? CompactString(Core::ShaderArchive::s_DefaultVariant)
        : material.shaderVariant();
    const Name& vertexStageName = __hidden_ecs_graphics::StageNameFromShaderType(Core::ShaderType::Vertex);
    const Name& pixelStageName = __hidden_ecs_graphics::StageNameFromShaderType(Core::ShaderType::Pixel);
    if(!material.findShaderForStage(vertexStageName, vertexShaderAsset) || !material.findShaderForStage(pixelStageName, pixelShaderAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: material '{}' must provide both 'vs' and 'ps' shaders"),
            StringConvert(materialKey.c_str())
        );
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }

    if(!ensureShaderLoaded(resources.vertexShader, vertexShaderAsset.name(), shaderVariant, Core::ShaderType::Vertex, "ECSGraphics_RendererVS"))
    {
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }

    if(!ensureShaderLoaded(resources.pixelShader, pixelShaderAsset.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
    {
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();

    if(!resources.inputLayout){
        Core::VertexAttributeDesc attributes[2];
        attributes[0].setFormat(Core::Format::RGB32_FLOAT).setBufferIndex(0).setOffset(0).setElementStride(__hidden_ecs_graphics::s_PositionColorVertexStride).setName("POSITION");
        attributes[1].setFormat(Core::Format::RGB32_FLOAT).setBufferIndex(0).setOffset(sizeof(f32) * 3).setElementStride(__hidden_ecs_graphics::s_PositionColorVertexStride).setName("COLOR");

        resources.inputLayout = device->createInputLayout(attributes, 2, resources.vertexShader.get());
        if(!resources.inputLayout){
            if(inserted)
                m_materialPipelines.erase(pipelineKey);
            return false;
        }
    }

    Core::GraphicsPipelineDesc desc;
    desc.setInputLayout(resources.inputLayout.get());
    desc.setVertexShader(resources.vertexShader.get());
    desc.setPixelShader(resources.pixelShader.get());

    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.setCullNone();
    desc.setRenderState(renderState);

    resources.pipeline = device->createGraphicsPipeline(desc, framebuffer->getFramebufferInfo());
    if(!resources.pipeline){
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
        return false;
    }

    outResources = &resources;
    return true;
}

bool RendererSystem::ensureShaderLoaded(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const CompactString& variantName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName
){
    if(outShader)
        return true;
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shader name is empty"));
        return false;
    }

    const Name& stageName = __hidden_ecs_graphics::StageNameFromShaderType(shaderType);
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: unsupported shader stage {}"), static_cast<u32>(shaderType));
        return false;
    }

    const CompactString resolvedVariantName = variantName.empty()
        ? CompactString(Core::ShaderArchive::s_DefaultVariant)
        : variantName;
    if(!m_shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shader path resolver is null"));
        return false;
    }

    Name shaderVirtualPath = NAME_NONE;
    if(!m_shaderPathResolver(shaderName, resolvedVariantName, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to resolve shader '{}' variant '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName.c_str()),
            StringConvert(stageName.c_str())
        );
        return false;
    }
    if(!shaderVirtualPath)
        return false;

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Shader::AssetTypeName(), shaderVirtualPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to load shader '{}'"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }
    if(!loadedAsset){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: shader asset '{}' is null"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }
    if(loadedAsset->assetType() != Shader::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: asset '{}' is not a shader"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    const Shader& shaderAsset = static_cast<const Shader&>(*loadedAsset);
    const Vector<u8>& shaderBinary = shaderAsset.bytecode();
    if(shaderBinary.empty() || (shaderBinary.size() & 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: shader '{}' has invalid bytecode"),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    Core::ShaderDesc shaderDesc;
    shaderDesc.setShaderType(shaderType);
    shaderDesc.setDebugName(debugName);

    Core::IDevice* device = m_graphics.getDevice();
    outShader = device->createShader(
        shaderDesc,
        shaderBinary.data(),
        shaderBinary.size()
    );
    return static_cast<bool>(outShader);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

