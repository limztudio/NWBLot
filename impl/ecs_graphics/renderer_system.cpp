// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include <core/graphics/shader_archive.h>
#include <logger/client/logger.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_asset.h>
#include <impl/assets_graphics/shader_asset.h>
#include <impl/assets_graphics/shader_stage_names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
static constexpr u32 s_PositionColorVertexStride = sizeof(f32) * 6u;
static constexpr u32 s_EmulatedVertexStride = sizeof(f32) * 8u;
static constexpr u32 s_TrianglesPerWorkgroup = 32u;
static constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


struct ShaderDrivenPushConstants{
    u32 triangleCount = 0;
    u32 scissorCullEnabled = 0;
    u32 padding0 = 0;
    u32 padding1 = 0;
    f32 viewportRect[4] = {};
    f32 scissorRect[4] = {};
};

struct EmulatedVertex{
    f32 position[4];
    f32 color[3];
    f32 padding = 0.f;
};

static_assert(sizeof(ShaderDrivenPushConstants) == 48, "ShaderDrivenPushConstants layout must stay stable");
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");


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

static const Name& MeshEmulationVertexShaderName(){
    static const Name s("engine/graphics/mesh_emulation_vs");
    return s;
}

static const Name& DeferredCompositeVertexShaderName(){
    static const Name s("engine/graphics/deferred_composite_vs");
    return s;
}

static const Name& DeferredCompositePixelShaderName(){
    static const Name s("engine/graphics/deferred_composite_ps");
    return s;
}

static bool SupportsFormat(Core::IDevice& device, const Core::Format::Enum format, const Core::FormatSupport::Mask requiredSupport){
    return (device.queryFormatSupport(format) & requiredSupport) == requiredSupport;
}

static Core::Format::Enum SelectGBufferAlbedoFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
        Core::Format::BGRA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    for(const Core::Format::Enum format : candidates){
        if(SupportsFormat(device, format, requiredSupport))
            return format;
    }

    return Core::Format::UNKNOWN;
}

static Core::Format::Enum SelectGBufferDepthFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::D32,
        Core::Format::D24S8,
        Core::Format::D16,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    for(const Core::Format::Enum format : candidates){
        if(SupportsFormat(device, format, requiredSupport))
            return format;
    }

    return Core::Format::UNKNOWN;
}

static Core::RenderState BuildGeometryRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .enableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip();
    return renderState;
}

static Core::RenderState BuildCompositeRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}

static bool TryFindShaderForStage(const Material& material, const Core::ShaderType::Mask shaderType, Core::Assets::AssetRef<Shader>& outShaderAsset){
    outShaderAsset.reset();

    const Name& stageName = StageNameFromShaderType(shaderType);
    if(!stageName)
        return false;

    return material.findShaderForStage(stageName, outShaderAsset);
}

static u32 ComputeDispatchGroupCount(const u32 triangleCount){
    return triangleCount == 0
        ? 0
        : (triangleCount + s_TrianglesPerWorkgroup - 1u) / s_TrianglesPerWorkgroup
    ;
}

static ShaderDrivenPushConstants BuildShaderDrivenPushConstants(const u32 triangleCount, const Core::ViewportState& viewportState){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.triangleCount = triangleCount;

    if(viewportState.viewports.empty())
        return pushConstants;

    const Core::Viewport& viewport = viewportState.viewports[0];
    pushConstants.scissorCullEnabled = 1;
    pushConstants.viewportRect[0] = viewport.minX;
    pushConstants.viewportRect[1] = viewport.minY;
    pushConstants.viewportRect[2] = viewport.maxX;
    pushConstants.viewportRect[3] = viewport.maxY;

    Core::Rect scissorRect(viewport);
    if(!viewportState.scissorRects.empty())
        scissorRect = viewportState.scissorRects[0];

    pushConstants.scissorRect[0] = static_cast<f32>(scissorRect.minX);
    pushConstants.scissorRect[1] = static_cast<f32>(scissorRect.minY);
    pushConstants.scissorRect[2] = static_cast<f32>(scissorRect.maxX);
    pushConstants.scissorRect[3] = static_cast<f32>(scissorRect.maxY);
    return pushConstants;
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
    return lhs.material == rhs.material && lhs.framebufferInfo == rhs.framebufferInfo;
}


RendererSystem::RendererSystem(
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::IRenderPass(graphics)
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

    DeferredFrameTargets* deferredTargets = nullptr;
    if(!ensureDeferredFrameTargets(framebuffer, deferredTargets))
        return;
    if(!deferredTargets || !deferredTargets->valid())
        return;

    Core::IDevice* device = m_graphics.getDevice();
    Core::CommandListHandle commandList = device->createCommandList();
    if(!commandList){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create render command list"));
        return;
    }

    commandList->open();

    clearDeferredTargets(*commandList, *deferredTargets);
    renderGeometryPass(*commandList, deferredTargets->framebuffer.get());
    commandList->endRenderPass();
    commandList->setResourceStatesForBindingSet(deferredTargets->compositeBindingSet.get());
    commandList->commitBarriers();
    if(!renderDeferredComposite(*commandList, *deferredTargets, framebuffer)){
        commandList->close();
        return;
    }

    commandList->close();
    device->executeCommandList(commandList.get());
}

void RendererSystem::backBufferResizing(){
    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

void RendererSystem::backBufferResized(u32 width, u32 height, u32 sampleCount){
    (void)width;
    (void)height;
    (void)sampleCount;

    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

bool RendererSystem::ensureDeferredFrameTargets(Core::IFramebuffer* presentationFramebuffer, DeferredFrameTargets*& outTargets){
    outTargets = nullptr;

    if(!presentationFramebuffer)
        return false;

    const Core::FramebufferInfoEx& presentationInfo = presentationFramebuffer->getFramebufferInfo();
    if(presentationInfo.width == 0 || presentationInfo.height == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: presentation framebuffer has invalid dimensions"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::Format::Enum albedoFormat = __hidden_ecs_graphics::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = __hidden_ecs_graphics::SelectGBufferDepthFormat(*device);
    if(albedoFormat == Core::Format::UNKNOWN || depthFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }

    if(m_deferredTargets.valid()
        && m_deferredTargets.width == presentationInfo.width
        && m_deferredTargets.height == presentationInfo.height
        && m_deferredTargets.albedoFormat == albedoFormat
        && m_deferredTargets.depthFormat == depthFormat)
    {
        outTargets = &m_deferredTargets;
        return true;
    }

    if(!ensureDeferredCompositeResources())
        return false;

    resetDeferredFrameTargets();
    m_materialPipelines.clear();

    DeferredFrameTargets createdTargets;
    createdTargets.width = presentationInfo.width;
    createdTargets.height = presentationInfo.height;
    createdTargets.albedoFormat = albedoFormat;
    createdTargets.depthFormat = depthFormat;

    Core::TextureDesc albedoDesc;
    albedoDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.albedoFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/gbuffer_albedo")
        .setClearValue(__hidden_ecs_graphics::s_ClearColor)
    ;
    createdTargets.albedo = m_graphics.createTexture(albedoDesc);
    if(!createdTargets.albedo){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred albedo target"));
        return false;
    }

    Core::TextureDesc depthDesc;
    depthDesc
        .setWidth(createdTargets.width)
        .setHeight(createdTargets.height)
        .setFormat(createdTargets.depthFormat)
        .setInRenderTarget(true)
        .setName("engine/deferred/depth")
    ;
    createdTargets.depth = m_graphics.createTexture(depthDesc);
    if(!createdTargets.depth){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred depth target"));
        return false;
    }

    Core::FramebufferDesc framebufferDesc;
    framebufferDesc
        .addColorAttachment(createdTargets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .setDepthAttachment(createdTargets.depth.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
    ;
    createdTargets.framebuffer = device->createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, m_deferredCompositeBindingLayout.get());
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    m_deferredTargets = Move(createdTargets);
    outTargets = &m_deferredTargets;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, depth {})"),
        m_deferredTargets.width,
        m_deferredTargets.height,
        StringConvert(Core::GetFormatInfo(m_deferredTargets.albedoFormat).name),
        StringConvert(Core::GetFormatInfo(m_deferredTargets.depthFormat).name)
    );
    return true;
}

bool RendererSystem::ensureDeferredCompositeResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_deferredCompositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));

        m_deferredCompositeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_deferredCompositeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding layout"));
            return false;
        }
    }

    if(!m_deferredSampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(false)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_deferredSampler = device->createSampler(samplerDesc);
        if(!m_deferredSampler){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite sampler"));
            return false;
        }
    }

    if(!ensureShaderLoaded(
        m_deferredCompositeVertexShader,
        __hidden_ecs_graphics::DeferredCompositeVertexShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Vertex,
        "ECSGraphics_DeferredCompositeVS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_deferredCompositePixelShader,
        __hidden_ecs_graphics::DeferredCompositePixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_DeferredCompositePS"
    ))
        return false;

    return true;
}

bool RendererSystem::ensureDeferredCompositePipeline(Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;

    if(!ensureDeferredCompositeResources())
        return false;

    const Core::FramebufferInfo& framebufferInfo = presentationFramebuffer->getFramebufferInfo();
    if(m_deferredCompositePipeline && m_deferredCompositePipeline->getFramebufferInfo() == framebufferInfo)
        return true;

    Core::GraphicsPipelineDesc pipelineDesc;
    pipelineDesc
        .setVertexShader(m_deferredCompositeVertexShader.get())
        .setPixelShader(m_deferredCompositePixelShader.get())
        .setRenderState(__hidden_ecs_graphics::BuildCompositeRenderState())
        .addBindingLayout(m_deferredCompositeBindingLayout.get())
    ;

    Core::IDevice* device = m_graphics.getDevice();
    m_deferredCompositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredCompositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

void RendererSystem::resetDeferredFrameTargets(){
    m_deferredTargets = DeferredFrameTargets{};
}

void RendererSystem::clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo){
        commandList.setTextureState(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
        commandList.clearTextureFloat(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, __hidden_ecs_graphics::s_ClearColor);
    }

    if(targets.depth){
        commandList.setTextureState(targets.depth.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
        commandList.clearDepthStencilTexture(
            targets.depth.get(),
            __hidden_ecs_graphics::s_FramebufferSubresources,
            true,
            Core::s_DepthClearValue,
            false,
            0
        );
    }
}

void RendererSystem::renderGeometryPass(Core::ICommandList& commandList, Core::IFramebuffer* gBufferFramebuffer){
    if(!gBufferFramebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(gBufferFramebuffer->getFramebufferInfo().getViewport());

    for(auto&& [entity, renderer] : rendererView){
        (void)entity;

        if(!renderer.visible)
            continue;

        GeometryResources* geometry = nullptr;
        if(!ensureGeometryLoaded(renderer.geometry, geometry))
            continue;
        if(!geometry || !geometry->valid())
            continue;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!ensureRendererPipeline(renderer, gBufferFramebuffer, pipelineResources))
            continue;
        if(!pipelineResources)
            continue;

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            if(!pipelineResources->meshletPipeline)
                continue;
            if(!ensureMeshBindingSet(*geometry))
                continue;

            commandList.setBufferState(geometry->shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(geometry->shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);

            Core::MeshletState meshletState;
            meshletState.setPipeline(pipelineResources->meshletPipeline.get());
            meshletState.setFramebuffer(gBufferFramebuffer);
            meshletState.setViewport(viewportState);
            meshletState.addBindingSet(geometry->meshBindingSet.get());

            commandList.setMeshletState(meshletState);

            const __hidden_ecs_graphics::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_graphics::BuildShaderDrivenPushConstants(geometry->triangleCount, viewportState);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatchMesh(geometry->dispatchGroupCount);
            break;
        }
        case RenderPath::ComputeEmulation:{
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                continue;
            if(!ensureComputeBindingSet(*geometry))
                continue;

            commandList.setBufferState(geometry->shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(geometry->shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
            commandList.setBufferState(geometry->emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

            Core::ComputeState computeState;
            computeState.setPipeline(pipelineResources->computePipeline.get());
            computeState.addBindingSet(geometry->computeBindingSet.get());

            commandList.setComputeState(computeState);

            const __hidden_ecs_graphics::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_graphics::BuildShaderDrivenPushConstants(geometry->triangleCount, viewportState);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
            commandList.dispatch(geometry->dispatchGroupCount);

            commandList.setBufferState(geometry->emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

            Core::GraphicsState graphicsState;
            graphicsState.setPipeline(pipelineResources->emulationPipeline.get());
            graphicsState.setFramebuffer(gBufferFramebuffer);
            graphicsState.setViewport(viewportState);
            graphicsState.addVertexBuffer(
                Core::VertexBufferBinding()
                    .setBuffer(geometry->emulationVertexBuffer.get())
                    .setSlot(0)
                    .setOffset(0)
            );

            commandList.setGraphicsState(graphicsState);

            Core::DrawArguments drawArgs;
            drawArgs.setVertexCount(geometry->indexCount);
            commandList.draw(drawArgs);
            break;
        }
        default:{
            break;
        }
        }
    }
}

bool RendererSystem::renderDeferredComposite(Core::ICommandList& commandList, DeferredFrameTargets& targets, Core::IFramebuffer* presentationFramebuffer){
    if(!presentationFramebuffer)
        return false;
    if(!targets.compositeBindingSet)
        return false;
    if(!ensureDeferredCompositePipeline(presentationFramebuffer))
        return false;

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(presentationFramebuffer->getFramebufferInfo().getViewport());

    Core::GraphicsState graphicsState;
    graphicsState.setPipeline(m_deferredCompositePipeline.get());
    graphicsState.setFramebuffer(presentationFramebuffer);
    graphicsState.setViewport(viewportState);
    graphicsState.addBindingSet(targets.compositeBindingSet.get());

    commandList.setGraphicsState(graphicsState);

    Core::DrawArguments drawArgs;
    drawArgs.setVertexCount(3);
    commandList.draw(drawArgs);
    return true;
}


bool RendererSystem::ensureGeometryLoaded(const Core::Assets::AssetRef<Geometry>& geometryAsset, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    const Name geometryPath = geometryAsset.name();
    if(!geometryPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer geometry is empty"));
        return false;
    }

    const auto foundGeometry = m_geometryMeshes.find(geometryPath);
    if(foundGeometry != m_geometryMeshes.end()){
        outGeometry = &foundGeometry.value();
        return outGeometry->valid();
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

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryPath;

    const usize indexStride = geometry.use32BitIndices() ? sizeof(u32) : sizeof(u16);
    if(geometry.indexData().size() == 0 || (geometry.indexData().size() % indexStride) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' has malformed index payload"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    createdGeometry.indexCount = static_cast<u32>(geometry.indexData().size() / indexStride);
    if(createdGeometry.indexCount == 0 || (createdGeometry.indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' index count {} is incompatible with triangle-based mesh rendering"),
            StringConvert(geometryPath.c_str()),
            createdGeometry.indexCount
        );
        return false;
    }

    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = __hidden_ecs_graphics::ComputeDispatchGroupCount(createdGeometry.triangleCount);
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' produced no dispatch groups"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    const Name shaderVertexBufferName = DeriveName(geometryPath, AStringView(":shader_vb"));
    const Name shaderIndexBufferName = DeriveName(geometryPath, AStringView(":shader_ib"));
    if(!shaderVertexBufferName || !shaderIndexBufferName){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to derive shader-driven buffer names for geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    Core::Graphics::BufferSetupDesc shaderVertexSetup;
    shaderVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(geometry.vertexData().size()))
        .setStructStride(geometry.vertexStride())
        .setDebugName(shaderVertexBufferName)
    ;
    shaderVertexSetup.data = geometry.vertexData().data();
    shaderVertexSetup.dataSize = geometry.vertexData().size();
    createdGeometry.shaderVertexBuffer = m_graphics.setupBuffer(shaderVertexSetup);
    if(!createdGeometry.shaderVertexBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create shader vertex buffer for geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    Vector<u32> expandedIndices;
    expandedIndices.resize(createdGeometry.indexCount);
    if(geometry.use32BitIndices()){
        const u32* indexData = reinterpret_cast<const u32*>(geometry.indexData().data());
        for(u32 i = 0; i < createdGeometry.indexCount; ++i)
            expandedIndices[i] = indexData[i];
    }
    else{
        const u16* indexData = reinterpret_cast<const u16*>(geometry.indexData().data());
        for(u32 i = 0; i < createdGeometry.indexCount; ++i)
            expandedIndices[i] = static_cast<u32>(indexData[i]);
    }

    Core::Graphics::BufferSetupDesc shaderIndexSetup;
    shaderIndexSetup.bufferDesc
        .setByteSize(static_cast<u64>(expandedIndices.size() * sizeof(u32)))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderIndexBufferName)
    ;
    shaderIndexSetup.data = expandedIndices.data();
    shaderIndexSetup.dataSize = expandedIndices.size() * sizeof(u32);
    createdGeometry.shaderIndexBuffer = m_graphics.setupBuffer(shaderIndexSetup);
    if(!createdGeometry.shaderIndexBuffer){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create shader index buffer for geometry '{}'"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    auto [it, inserted] = m_geometryMeshes.emplace(geometryPath, GeometryResources{});
    (void)inserted;
    it.value() = Move(createdGeometry);

    outGeometry = &it.value();
    return outGeometry->valid();
}

bool RendererSystem::ensureMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::ShaderDrivenPushConstants)));

    Core::IDevice* device = m_graphics.getDevice();
    m_meshBindingLayout = device->createBindingLayout(bindingLayoutDesc);
    if(!m_meshBindingLayout){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding layout"));
        return false;
    }

    return true;
}

bool RendererSystem::ensureComputeEmulationResources(){
    if(!m_computeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::ShaderDrivenPushConstants)));

        Core::IDevice* device = m_graphics.getDevice();
        m_computeBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_computeBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding layout"));
            return false;
        }
    }

    if(!m_emulationVertexShader){
        if(!ensureShaderLoaded(
            m_emulationVertexShader,
            __hidden_ecs_graphics::MeshEmulationVertexShaderName(),
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSGraphics_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[2];
        attributes[0]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(0)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("POSITION")
        ;
        attributes[1]
            .setFormat(Core::Format::RGB32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 4u)
            .setElementStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setName("COLOR")
        ;

        Core::IDevice* device = m_graphics.getDevice();
        m_emulationInputLayout = device->createInputLayout(attributes, 2, m_emulationVertexShader.get());
        if(!m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::ensureMeshBindingSet(GeometryResources& geometry){
    if(geometry.meshBindingSet)
        return true;
    if(!ensureMeshShaderResources())
        return false;

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.meshBindingSet = device->createBindingSet(bindingSetDesc, m_meshBindingLayout.get());
    if(!geometry.meshBindingSet){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create mesh shader binding set for geometry '{}'"),
            StringConvert(geometry.geometryName.c_str())
        );
        return false;
    }

    return true;
}

bool RendererSystem::ensureComputeBindingSet(GeometryResources& geometry){
    if(geometry.computeBindingSet)
        return true;
    if(!ensureComputeEmulationResources())
        return false;

    if(!geometry.emulationVertexBuffer){
        const Name emulationVertexBufferName = DeriveName(geometry.geometryName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for geometry '{}'"),
                StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(static_cast<u64>(geometry.indexCount) * __hidden_ecs_graphics::s_EmulatedVertexStride)
            .setStructStride(__hidden_ecs_graphics::s_EmulatedVertexStride)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setDebugName(emulationVertexBufferName)
        ;
        geometry.emulationVertexBuffer = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(!geometry.emulationVertexBuffer){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for geometry '{}'"),
                StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, geometry.emulationVertexBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout.get());
    if(!geometry.computeBindingSet){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for geometry '{}'"),
            StringConvert(geometry.geometryName.c_str())
        );
        return false;
    }

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
    switch(resources.renderPath){
    case RenderPath::MeshShader:
        if(resources.meshletPipeline){
            outResources = &resources;
            return true;
        }
        break;
    case RenderPath::ComputeEmulation:
        if(resources.computePipeline && resources.emulationPipeline){
            outResources = &resources;
            return true;
        }
        break;
    default:
        break;
    }

    auto removeFailedEntry = [&](){
        if(inserted)
            m_materialPipelines.erase(pipelineKey);
    };

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Material::AssetTypeName(), materialKey, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to load material '{}'"),
            StringConvert(materialKey.c_str())
        );
        removeFailedEntry();
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: asset '{}' is not a material"),
            StringConvert(materialKey.c_str())
        );
        removeFailedEntry();
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);
    const AStringView shaderVariant = material.shaderVariant().empty() ? AStringView(Core::ShaderArchive::s_DefaultVariant) : AStringView(material.shaderVariant());

    Core::Assets::AssetRef<Shader> pixelShaderAsset;
    Core::Assets::AssetRef<Shader> meshShaderAsset;

    const bool hasPixelShader = __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Pixel, pixelShaderAsset);
    const bool hasMeshShader = __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Mesh, meshShaderAsset);

    Core::IDevice* device = m_graphics.getDevice();
    const Core::RenderState geometryRenderState = __hidden_ecs_graphics::BuildGeometryRenderState();

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!hasMeshShader || !hasPixelShader)
            return false;
        if(!ensureMeshShaderResources())
            return false;
        if(!ensureShaderLoaded(resources.meshShader, meshShaderAsset.name(), shaderVariant, Core::ShaderType::Mesh, "ECSGraphics_RendererMesh"))
            return false;
        if(!ensureShaderLoaded(resources.pixelShader, pixelShaderAsset.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
            return false;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader.get());
        pipelineDesc.setPixelShader(resources.pixelShader.get());
        pipelineDesc.setRenderState(geometryRenderState);
        pipelineDesc.addBindingLayout(m_meshBindingLayout.get());

        resources.meshletPipeline = device->createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"),
                StringConvert(materialKey.c_str())
            );
            return false;
        }

        resources.renderPath = RenderPath::MeshShader;
        return true;
    };

    auto tryBuildComputePipeline = [&]() -> bool{
        if(!hasMeshShader || !hasPixelShader)
            return false;
        if(!ensureComputeEmulationResources())
            return false;
        const Name& meshComputeArchiveStageName = ShaderStageNames::MeshComputeArchiveStageName();
        if(!ensureShaderLoaded(
            resources.computeShader,
            meshShaderAsset.name(),
            shaderVariant,
            Core::ShaderType::Compute,
            "ECSGraphics_RendererCS",
            &meshComputeArchiveStageName))
        {
            return false;
        }
        if(!ensureShaderLoaded(resources.pixelShader, pixelShaderAsset.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
            return false;

        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader.get());
        computeDesc.addBindingLayout(m_computeBindingLayout.get());
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"),
                StringConvert(materialKey.c_str())
            );
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_emulationInputLayout.get());
        emulationDesc.setVertexShader(m_emulationVertexShader.get());
        emulationDesc.setPixelShader(resources.pixelShader.get());
        emulationDesc.setRenderState(geometryRenderState);
        resources.emulationPipeline = device->createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"),
                StringConvert(materialKey.c_str())
            );
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        return true;
    };

    const bool meshSupported = device->queryFeatureSupport(Core::Feature::Meshlets);
    if(!hasPixelShader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: material '{}' requires a pixel shader"),
            StringConvert(materialKey.c_str())
        );
        removeFailedEntry();
        return false;
    }

    if(!hasMeshShader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: material '{}' requires a mesh shader; compute emulation is derived internally from that mesh shader"),
            StringConvert(materialKey.c_str())
        );
        removeFailedEntry();
        return false;
    }

    if(meshSupported && hasMeshShader){
        if(!tryBuildMeshPipeline()){
            NWB_LOGGER_ERROR(
                NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device"),
                StringConvert(materialKey.c_str())
            );
            removeFailedEntry();
            return false;
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        outResources = &resources;
        return true;
    }

    if(!tryBuildComputePipeline()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader"),
            StringConvert(materialKey.c_str())
        );
        removeFailedEntry();
        return false;
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    outResources = &resources;
    return true;
}

void RendererSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath renderPath, const bool meshSupported){
    const auto foundLoggedPath = m_loggedMaterialPaths.find(materialKey);
    if(foundLoggedPath != m_loggedMaterialPaths.end() && foundLoggedPath.value() == renderPath)
        return;

    auto [it, inserted] = m_loggedMaterialPaths.emplace(materialKey, renderPath);
    if(!inserted)
        it.value() = renderPath;

    switch(renderPath){
    case RenderPath::MeshShader:{
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("RendererSystem: material '{}' selected MeshShader + PS on this device"),
            StringConvert(materialKey.c_str())
        );
        break;
    }
    case RenderPath::ComputeEmulation:{
        if(!meshSupported){
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS by compiling its mesh shader for compute emulation because this device does not support mesh shaders"),
                StringConvert(materialKey.c_str())
            );
        }
        else{
            NWB_LOGGER_ESSENTIAL_INFO(
                NWB_TEXT("RendererSystem: material '{}' selected CS + PS through compute emulation"),
                StringConvert(materialKey.c_str())
            );
        }
        break;
    }
    default:{
        break;
    }
    }
}

bool RendererSystem::ensureShaderLoaded(
    Core::ShaderHandle& outShader,
    const Name& shaderName,
    const AStringView variantName,
    const Core::ShaderType::Mask shaderType,
    const Name& debugName,
    const Name* archiveStageName
)
{
    if(outShader)
        return true;
    if(!shaderName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shader name is empty"));
        return false;
    }

    const Name& stageName = archiveStageName
        ? *archiveStageName
        : __hidden_ecs_graphics::StageNameFromShaderType(shaderType)
    ;
    if(!stageName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: unsupported shader stage {}"), static_cast<u32>(shaderType));
        return false;
    }

    const AStringView resolvedVariantName = variantName.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : variantName
    ;
    if(!m_shaderPathResolver){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: shader path resolver is null"));
        return false;
    }

    Name shaderVirtualPath = NAME_NONE;
    if(!m_shaderPathResolver(shaderName, resolvedVariantName, stageName, shaderVirtualPath)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to resolve shader '{}' variant '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }
    if(!shaderVirtualPath){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: shader resolver returned an empty path for shader '{}' variant '{}' stage '{}'"),
            StringConvert(shaderName.c_str()),
            StringConvert(resolvedVariantName),
            StringConvert(stageName.c_str())
        );
        return false;
    }

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
    if(!outShader){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to create shader '{}' from asset '{}'"),
            StringConvert(debugName.c_str()),
            StringConvert(shaderVirtualPath.c_str())
        );
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
