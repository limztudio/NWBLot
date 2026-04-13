// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include <core/graphics/shader_archive.h>
#include <logger/client/logger.h>
#include <impl/assets_graphics/geometry_asset.h>
#include <impl/assets_graphics/material_asset.h>
#include <impl/assets_graphics/shader_asset.h>
#include <impl/assets_graphics/shader_stage_names.h>

#include <cmath>
#include <cstdlib>


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
static constexpr u32 s_AvboitDownsample = 8u;
static constexpr u32 s_AvboitVirtualSlices = 128u;
static constexpr u32 s_AvboitPhysicalSlices = 64u;
static constexpr f32 s_AvboitExtinctionFixedScale = 4096.f;
static constexpr f32 s_AvboitSelfOcclusionSliceBias = 2.f;
static constexpr usize s_AvboitControlWordCount = 8u;
static constexpr f32 s_DefaultMeshViewRotationY = 0.82f;
static constexpr f32 s_DefaultMeshViewRotationX = 0.94f;
static constexpr f32 s_DefaultMeshViewDepthOffset = 2.2f;


struct ShaderDrivenPushConstants{
    u32 triangleCount = 0;
    u32 scissorCullEnabled = 0;
    u32 padding0 = 0;
    u32 padding1 = 0;
    f32 viewportRect[4] = {};
    f32 scissorRect[4] = {};
    f32 viewParams[4] = {};
};

struct AvboitPushConstants{
    u32 frame[4] = {};
    u32 volume[4] = {};
    f32 params[4] = {};
};

struct TransparentDrawPushConstants{
    ShaderDrivenPushConstants mesh;
    AvboitPushConstants avboit;
};

struct EmulatedVertex{
    f32 position[4];
    f32 color[3];
    f32 padding = 0.f;
};

static_assert(sizeof(ShaderDrivenPushConstants) == 64, "ShaderDrivenPushConstants layout must stay stable");
static_assert(sizeof(AvboitPushConstants) == 48, "AvboitPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) == 112, "TransparentDrawPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize, "Transparent draw push constants must fit the portable push constant budget");
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

static const Name& AvboitOccupancyPixelShaderName(){
    static const Name s("engine/graphics/avboit_occupancy_ps");
    return s;
}

static const Name& AvboitDepthWarpComputeShaderName(){
    static const Name s("engine/graphics/avboit_depth_warp_cs");
    return s;
}

static const Name& AvboitExtinctionPixelShaderName(){
    static const Name s("engine/graphics/avboit_extinction_ps");
    return s;
}

static const Name& AvboitIntegrateComputeShaderName(){
    static const Name s("engine/graphics/avboit_integrate_cs");
    return s;
}

static const Name& AvboitAccumulatePixelShaderName(){
    static const Name s("engine/graphics/avboit_accumulate_ps");
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

static Core::Format::Enum SelectAvboitAccumColorFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    for(const Core::Format::Enum format : candidates){
        if(SupportsFormat(device, format, requiredSupport))
            return format;
    }

    return Core::Format::UNKNOWN;
}

static Core::Format::Enum SelectAvboitAccumExtinctionFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R16_FLOAT,
        Core::Format::R32_FLOAT,
        Core::Format::RGBA16_FLOAT,
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget | Core::FormatSupport::Blendable;

    for(const Core::Format::Enum format : candidates){
        if(SupportsFormat(device, format, requiredSupport))
            return format;
    }

    return Core::Format::UNKNOWN;
}

static Core::Format::Enum SelectAvboitLowRasterFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::R8_UNORM,
        Core::Format::RGBA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

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

static Core::BlendState::RenderTarget BuildAdditiveBlendTarget(const Core::ColorMask::Mask colorWriteMask = Core::ColorMask::All){
    Core::BlendState::RenderTarget target;
    target
        .enableBlend()
        .setSrcBlend(Core::BlendFactor::One)
        .setDestBlend(Core::BlendFactor::One)
        .setBlendOp(Core::BlendOp::Add)
        .setSrcBlendAlpha(Core::BlendFactor::One)
        .setDestBlendAlpha(Core::BlendFactor::One)
        .setBlendOpAlpha(Core::BlendOp::Add)
        .setColorWriteMask(colorWriteMask)
    ;
    return target;
}

static Core::RenderState BuildAvboitVoxelRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    renderState.blendState.targets[0].setColorWriteMask(Core::ColorMask::None);
    return renderState;
}

static Core::RenderState BuildAvboitAccumulateRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .disableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip().setCullNone();
    renderState.blendState
        .setRenderTarget(0, BuildAdditiveBlendTarget())
        .setRenderTarget(1, BuildAdditiveBlendTarget(Core::ColorMask::Red))
    ;
    return renderState;
}

static Core::RenderState BuildRenderStateForPass(const RendererSystem::MaterialPipelinePass pass){
    switch(pass){
    case RendererSystem::MaterialPipelinePass::Opaque:
        return BuildGeometryRenderState();
    case RendererSystem::MaterialPipelinePass::AvboitOccupancy:
    case RendererSystem::MaterialPipelinePass::AvboitExtinction:
        return BuildAvboitVoxelRenderState();
    case RendererSystem::MaterialPipelinePass::AvboitAccumulate:
        return BuildAvboitAccumulateRenderState();
    default:
        return BuildGeometryRenderState();
    }
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
        : 1u + ((triangleCount - 1u) / s_TrianglesPerWorkgroup)
    ;
}

static ShaderDrivenPushConstants BuildShaderDrivenPushConstants(const u32 triangleCount, const Core::ViewportState& viewportState){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.triangleCount = triangleCount;
    pushConstants.viewParams[0] = s_DefaultMeshViewRotationY;
    pushConstants.viewParams[1] = s_DefaultMeshViewRotationX;
    pushConstants.viewParams[2] = s_DefaultMeshViewDepthOffset;

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

static bool IsTransparentText(const AStringView text){
    const AString normalized = CanonicalizeText(AString(text));
    return normalized == "transparent"
        || normalized == "translucent"
        || normalized == "blend"
        || normalized == "alpha"
        || normalized == "avboit"
        || normalized == "true"
        || normalized == "1"
    ;
}

static bool ParseAlphaValue(const AStringView text, f32& outAlpha){
    AString temp(text);
    char* end = nullptr;
    const f32 parsed = std::strtof(temp.c_str(), &end);
    if(end == temp.c_str())
        return false;
    if(!std::isfinite(parsed))
        return false;

    while(end && *end != '\0'){
        if(!IsAsciiSpace(*end))
            return false;
        ++end;
    }

    outAlpha = Max(0.f, Min(1.f, parsed));
    return true;
}

static bool FindMaterialParameter(const Material& material, const AStringView keyText, CompactString& outValue){
    CompactString key;
    if(!key.assign(keyText))
        return false;

    const auto found = material.parameters().find(key);
    if(found == material.parameters().end())
        return false;

    outValue = found.value();
    return true;
}

static AvboitPushConstants BuildAvboitPushConstants(const RendererSystem::AvboitFrameTargets& targets, const f32 alpha){
    AvboitPushConstants pushConstants;
    pushConstants.frame[0] = targets.fullWidth;
    pushConstants.frame[1] = targets.fullHeight;
    pushConstants.frame[2] = targets.lowWidth;
    pushConstants.frame[3] = targets.lowHeight;
    pushConstants.volume[0] = targets.virtualSliceCount;
    pushConstants.volume[1] = targets.physicalSliceCount;
    pushConstants.volume[2] = targets.lowWidth * targets.lowHeight * targets.physicalSliceCount;
    pushConstants.volume[3] = (targets.virtualSliceCount + 31u) / 32u;
    pushConstants.params[0] = alpha;
    pushConstants.params[1] = s_AvboitExtinctionFixedScale;
    pushConstants.params[2] = s_AvboitSelfOcclusionSliceBias;
    pushConstants.params[3] = 0.f;
    return pushConstants;
}

static TransparentDrawPushConstants BuildTransparentDrawPushConstants(
    const u32 triangleCount,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const f32 alpha
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(triangleCount, viewportState);
    pushConstants.avboit = BuildAvboitPushConstants(targets, alpha);
    return pushConstants;
}

static u32 DispatchGroupCount1D(const u32 itemCount, const u32 groupSize){
    return itemCount == 0 || groupSize == 0
        ? 0
        : ((itemCount - 1u) / groupSize) + 1u
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    seed ^= Hasher<u32>{}(static_cast<u32>(key.pass)) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= static_cast<usize>(key.framebufferInfo.depthFormat) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= Hasher<u32>{}(key.framebufferInfo.sampleCount) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    seed ^= Hasher<u32>{}(key.framebufferInfo.sampleQuality) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        seed ^= static_cast<usize>(format) + static_cast<usize>(0x9e3779b97f4a7c15ull) + (seed << 6) + (seed >> 2);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material && lhs.pass == rhs.pass && lhs.framebufferInfo == rhs.framebufferInfo;
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
    renderMaterialPass(
        *commandList,
        deferredTargets->framebuffer.get(),
        MaterialPipelinePass::Opaque,
        false,
        nullptr,
        nullptr
    );
    commandList->endRenderPass();

    clearAvboitTargets(*commandList, deferredTargets->avboit);
    if(hasTransparentRenderers())
        renderAvboitPasses(*commandList, *deferredTargets);

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
    const Core::Format::Enum avboitLowRasterFormat = __hidden_ecs_graphics::SelectAvboitLowRasterFormat(*device);
    const Core::Format::Enum avboitAccumColorFormat = __hidden_ecs_graphics::SelectAvboitAccumColorFormat(*device);
    const Core::Format::Enum avboitAccumExtinctionFormat = __hidden_ecs_graphics::SelectAvboitAccumExtinctionFormat(*device);
    if(albedoFormat == Core::Format::UNKNOWN || depthFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }
    if(avboitLowRasterFormat == Core::Format::UNKNOWN || avboitAccumColorFormat == Core::Format::UNKNOWN || avboitAccumExtinctionFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    if(m_deferredTargets.valid()
        && m_deferredTargets.width == presentationInfo.width
        && m_deferredTargets.height == presentationInfo.height
        && m_deferredTargets.albedoFormat == albedoFormat
        && m_deferredTargets.depthFormat == depthFormat
        && m_deferredTargets.avboit.lowRasterFormat == avboitLowRasterFormat
        && m_deferredTargets.avboit.accumColorFormat == avboitAccumColorFormat
        && m_deferredTargets.avboit.accumExtinctionFormat == avboitAccumExtinctionFormat)
    {
        outTargets = &m_deferredTargets;
        return true;
    }

    if(!ensureDeferredCompositeResources())
        return false;
    if(!ensureAvboitResources())
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

    AvboitFrameTargets avboitTargets;
    avboitTargets.fullWidth = createdTargets.width;
    avboitTargets.fullHeight = createdTargets.height;
    const u64 lowWidth = Max(
        1ull,
        (static_cast<u64>(createdTargets.width) + __hidden_ecs_graphics::s_AvboitDownsample - 1ull) / __hidden_ecs_graphics::s_AvboitDownsample
    );
    const u64 lowHeight = Max(
        1ull,
        (static_cast<u64>(createdTargets.height) + __hidden_ecs_graphics::s_AvboitDownsample - 1ull) / __hidden_ecs_graphics::s_AvboitDownsample
    );
    if(lowWidth > Limit<u32>::s_Max || lowHeight > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution dimensions exceed u32 limits"));
        return false;
    }
    avboitTargets.lowWidth = static_cast<u32>(lowWidth);
    avboitTargets.lowHeight = static_cast<u32>(lowHeight);
    avboitTargets.virtualSliceCount = __hidden_ecs_graphics::s_AvboitVirtualSlices;
    avboitTargets.physicalSliceCount = __hidden_ecs_graphics::s_AvboitPhysicalSlices;
    avboitTargets.lowRasterFormat = avboitLowRasterFormat;
    avboitTargets.accumColorFormat = avboitAccumColorFormat;
    avboitTargets.accumExtinctionFormat = avboitAccumExtinctionFormat;

    Core::TextureDesc lowRasterDesc;
    lowRasterDesc
        .setWidth(avboitTargets.lowWidth)
        .setHeight(avboitTargets.lowHeight)
        .setFormat(avboitTargets.lowRasterFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/low_raster")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.lowRasterTarget = m_graphics.createTexture(lowRasterDesc);
    if(!avboitTargets.lowRasterTarget){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution raster target"));
        return false;
    }

    Core::TextureDesc accumColorDesc;
    accumColorDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumColorFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_color")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumColor = m_graphics.createTexture(accumColorDesc);
    if(!avboitTargets.accumColor){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated color target"));
        return false;
    }

    Core::TextureDesc accumExtinctionDesc;
    accumExtinctionDesc
        .setWidth(avboitTargets.fullWidth)
        .setHeight(avboitTargets.fullHeight)
        .setFormat(avboitTargets.accumExtinctionFormat)
        .setInRenderTarget(true)
        .setName("engine/avboit/accum_extinction")
        .setClearValue(Core::Color(0.f, 0.f, 0.f, 0.f))
    ;
    avboitTargets.accumExtinction = m_graphics.createTexture(accumExtinctionDesc);
    if(!avboitTargets.accumExtinction){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulated extinction target"));
        return false;
    }

    Core::FramebufferDesc lowFramebufferDesc;
    lowFramebufferDesc.addColorAttachment(avboitTargets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources);
    avboitTargets.lowFramebuffer = device->createFramebuffer(lowFramebufferDesc);
    if(!avboitTargets.lowFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT low-resolution framebuffer"));
        return false;
    }

    Core::FramebufferDesc accumulationFramebufferDesc;
    accumulationFramebufferDesc
        .addColorAttachment(avboitTargets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .addColorAttachment(avboitTargets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources)
        .setDepthAttachment(
            Core::FramebufferAttachment()
                .setTexture(createdTargets.depth.get())
                .setSubresources(__hidden_ecs_graphics::s_FramebufferSubresources)
                .setReadOnly(true)
        )
    ;
    avboitTargets.accumulationFramebuffer = device->createFramebuffer(accumulationFramebufferDesc);
    if(!avboitTargets.accumulationFramebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation framebuffer"));
        return false;
    }

    const u32 coverageWordCount = (avboitTargets.virtualSliceCount + 31u) / 32u;
    const u64 coverageBytes = static_cast<u64>(coverageWordCount) * sizeof(u32);
    const u64 depthWarpBytes = static_cast<u64>(avboitTargets.virtualSliceCount) * sizeof(u32);
    const u64 lowPixelCount = static_cast<u64>(avboitTargets.lowWidth) * avboitTargets.lowHeight;
    if(lowPixelCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT low-resolution pixel count exceeds u32 limits"));
        return false;
    }
    if(avboitTargets.physicalSliceCount == 0 || lowPixelCount > static_cast<u64>(Limit<u32>::s_Max) / avboitTargets.physicalSliceCount){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: AVBOIT volume voxel count exceeds u32 limits"));
        return false;
    }
    const u64 volumeVoxelCount = lowPixelCount * avboitTargets.physicalSliceCount;
    const u64 volumeBytes = volumeVoxelCount * sizeof(u32);

    Core::BufferDesc coverageDesc;
    coverageDesc
        .setByteSize(coverageBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_coverage")
    ;
    avboitTargets.coverageBuffer = m_graphics.createBuffer(coverageDesc);
    if(!avboitTargets.coverageBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT coverage buffer"));
        return false;
    }

    Core::BufferDesc depthWarpDesc;
    depthWarpDesc
        .setByteSize(depthWarpBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/depth_warp_lut")
    ;
    avboitTargets.depthWarpBuffer = m_graphics.createBuffer(depthWarpDesc);
    if(!avboitTargets.depthWarpBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth warp buffer"));
        return false;
    }

    Core::BufferDesc controlDesc;
    controlDesc
        .setByteSize(static_cast<u64>(__hidden_ecs_graphics::s_AvboitControlWordCount) * sizeof(u32))
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/control")
    ;
    avboitTargets.controlBuffer = m_graphics.createBuffer(controlDesc);
    if(!avboitTargets.controlBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT control buffer"));
        return false;
    }

    Core::BufferDesc extinctionDesc;
    extinctionDesc
        .setByteSize(volumeBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/extinction_volume")
    ;
    avboitTargets.extinctionBuffer = m_graphics.createBuffer(extinctionDesc);
    if(!avboitTargets.extinctionBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction volume"));
        return false;
    }

    Core::BufferDesc transmittanceDesc;
    transmittanceDesc
        .setByteSize(volumeBytes)
        .setStructStride(sizeof(u32))
        .setCanHaveUAVs(true)
        .setDebugName("engine/avboit/transmittance_volume")
    ;
    avboitTargets.transmittanceBuffer = m_graphics.createBuffer(transmittanceDesc);
    if(!avboitTargets.transmittanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT transmittance volume"));
        return false;
    }

    Core::BindingSetDesc occupancyBindingSetDesc;
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    occupancyBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.coverageBuffer.get()));
    avboitTargets.occupancyBindingSet = device->createBindingSet(occupancyBindingSetDesc, m_avboitOccupancyBindingLayout.get());
    if(!avboitTargets.occupancyBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding set"));
        return false;
    }

    Core::BindingSetDesc depthWarpBindingSetDesc;
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.coverageBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, avboitTargets.depthWarpBuffer.get()));
    depthWarpBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, avboitTargets.controlBuffer.get()));
    avboitTargets.depthWarpBindingSet = device->createBindingSet(depthWarpBindingSetDesc, m_avboitDepthWarpBindingLayout.get());
    if(!avboitTargets.depthWarpBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding set"));
        return false;
    }

    Core::BindingSetDesc extinctionBindingSetDesc;
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.depth.get(),
        createdTargets.depthFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::Sampler(1, m_deferredSampler.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.depthWarpBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, avboitTargets.controlBuffer.get()));
    extinctionBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(4, avboitTargets.extinctionBuffer.get()));
    avboitTargets.extinctionBindingSet = device->createBindingSet(extinctionBindingSetDesc, m_avboitExtinctionBindingLayout.get());
    if(!avboitTargets.extinctionBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding set"));
        return false;
    }

    Core::BindingSetDesc integrateBindingSetDesc;
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.extinctionBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(1, avboitTargets.transmittanceBuffer.get()));
    integrateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    avboitTargets.integrateBindingSet = device->createBindingSet(integrateBindingSetDesc, m_avboitIntegrateBindingLayout.get());
    if(!avboitTargets.integrateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding set"));
        return false;
    }

    Core::BindingSetDesc accumulateBindingSetDesc;
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, avboitTargets.depthWarpBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, avboitTargets.transmittanceBuffer.get()));
    accumulateBindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(2, avboitTargets.controlBuffer.get()));
    avboitTargets.accumulateBindingSet = device->createBindingSet(accumulateBindingSetDesc, m_avboitAccumulateBindingLayout.get());
    if(!avboitTargets.accumulateBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding set"));
        return false;
    }

    createdTargets.avboit = Move(avboitTargets);

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        createdTargets.avboit.accumColor.get(),
        createdTargets.avboit.accumColorFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        2,
        createdTargets.avboit.accumExtinction.get(),
        createdTargets.avboit.accumExtinctionFormat,
        __hidden_ecs_graphics::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_deferredSampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, m_deferredCompositeBindingLayout.get());
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    m_deferredTargets = Move(createdTargets);
    outTargets = &m_deferredTargets;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, depth {}, AVBOIT color {}, extinction {})"),
        m_deferredTargets.width,
        m_deferredTargets.height,
        StringConvert(Core::GetFormatInfo(m_deferredTargets.albedoFormat).name),
        StringConvert(Core::GetFormatInfo(m_deferredTargets.depthFormat).name),
        StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumColorFormat).name),
        StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumExtinctionFormat).name)
    );
    return true;
}

bool RendererSystem::ensureDeferredCompositeResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_deferredCompositeBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(3, 1));

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

bool RendererSystem::ensureAvboitResources(){
    Core::IDevice* device = m_graphics.getDevice();

    if(!m_deferredSampler){
        Core::SamplerDesc samplerDesc;
        samplerDesc
            .setAllFilters(false)
            .setAllAddressModes(Core::SamplerAddressMode::Clamp)
        ;
        m_deferredSampler = device->createSampler(samplerDesc);
        if(!m_deferredSampler){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shared point sampler for AVBOIT"));
            return false;
        }
    }

    if(!m_avboitEmptyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);

        m_avboitEmptyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitEmptyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT empty binding layout"));
            return false;
        }
    }

    if(!m_avboitOccupancyBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitOccupancyBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitOccupancyBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT occupancy binding layout"));
            return false;
        }
    }

    if(!m_avboitDepthWarpBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::AvboitPushConstants)));

        m_avboitDepthWarpBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitDepthWarpBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp binding layout"));
            return false;
        }
    }

    if(!m_avboitExtinctionBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Texture_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::Sampler(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitExtinctionBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitExtinctionBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT extinction binding layout"));
            return false;
        }
    }

    if(!m_avboitIntegrateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Compute);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_UAV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::AvboitPushConstants)));

        m_avboitIntegrateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitIntegrateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration binding layout"));
            return false;
        }
    }

    if(!m_avboitAccumulateBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

        m_avboitAccumulateBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_avboitAccumulateBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT accumulation binding layout"));
            return false;
        }
    }

    if(!ensureShaderLoaded(
        m_avboitOccupancyPixelShader,
        __hidden_ecs_graphics::AvboitOccupancyPixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitOccupancyPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitDepthWarpComputeShader,
        __hidden_ecs_graphics::AvboitDepthWarpComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSGraphics_AvboitDepthWarpCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitExtinctionPixelShader,
        __hidden_ecs_graphics::AvboitExtinctionPixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitExtinctionPS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitIntegrateComputeShader,
        __hidden_ecs_graphics::AvboitIntegrateComputeShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Compute,
        "ECSGraphics_AvboitIntegrateCS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_avboitAccumulatePixelShader,
        __hidden_ecs_graphics::AvboitAccumulatePixelShaderName(),
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSGraphics_AvboitAccumulatePS"
    ))
        return false;

    return true;
}

bool RendererSystem::ensureAvboitPipelines(AvboitFrameTargets& targets){
    if(!ensureAvboitResources())
        return false;

    Core::IDevice* device = m_graphics.getDevice();

    if(!m_avboitDepthWarpPipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitDepthWarpComputeShader.get())
            .addBindingLayout(m_avboitDepthWarpBindingLayout.get())
        ;
        m_avboitDepthWarpPipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitDepthWarpPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT depth-warp pipeline"));
            return false;
        }
    }

    if(!m_avboitIntegratePipeline){
        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(m_avboitIntegrateComputeShader.get())
            .addBindingLayout(m_avboitIntegrateBindingLayout.get())
        ;
        m_avboitIntegratePipeline = device->createComputePipeline(pipelineDesc);
        if(!m_avboitIntegratePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create AVBOIT integration pipeline"));
            return false;
        }
    }

    return targets.valid();
}

void RendererSystem::resetDeferredFrameTargets(){
    m_deferredTargets = DeferredFrameTargets{};
}

void RendererSystem::clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo){
        commandList.setTextureState(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.depth){
        commandList.setTextureState(targets.depth.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.albedo){
        commandList.clearTextureFloat(targets.albedo.get(), __hidden_ecs_graphics::s_FramebufferSubresources, __hidden_ecs_graphics::s_ClearColor);
    }

    if(targets.depth){
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

void RendererSystem::clearAvboitTargets(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(targets.lowRasterTarget){
        commandList.setTextureState(targets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumColor){
        commandList.setTextureState(targets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.accumExtinction){
        commandList.setTextureState(targets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.coverageBuffer){
        commandList.setBufferState(targets.coverageBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.depthWarpBuffer){
        commandList.setBufferState(targets.depthWarpBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.controlBuffer){
        commandList.setBufferState(targets.controlBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.extinctionBuffer){
        commandList.setBufferState(targets.extinctionBuffer.get(), Core::ResourceStates::CopyDest);
    }

    if(targets.transmittanceBuffer){
        commandList.setBufferState(targets.transmittanceBuffer.get(), Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.lowRasterTarget){
        commandList.clearTextureFloat(targets.lowRasterTarget.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumColor){
        commandList.clearTextureFloat(targets.accumColor.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.accumExtinction){
        commandList.clearTextureFloat(targets.accumExtinction.get(), __hidden_ecs_graphics::s_FramebufferSubresources, Core::Color(0.f, 0.f, 0.f, 0.f));
    }

    if(targets.coverageBuffer){
        commandList.clearBufferUInt(targets.coverageBuffer.get(), 0u);
    }

    if(targets.depthWarpBuffer){
        commandList.clearBufferUInt(targets.depthWarpBuffer.get(), 0u);
    }

    if(targets.controlBuffer){
        commandList.clearBufferUInt(targets.controlBuffer.get(), 0u);
    }

    if(targets.extinctionBuffer){
        commandList.clearBufferUInt(targets.extinctionBuffer.get(), 0u);
    }

    if(targets.transmittanceBuffer){
        commandList.clearBufferUInt(targets.transmittanceBuffer.get(), 0u);
    }
}

void RendererSystem::renderMaterialPass(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass pass,
    const bool transparent,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return;
    if(pass != MaterialPipelinePass::Opaque && (!passBindingSet || !avboitTargets || !avboitTargets->valid()))
        return;

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector meshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector computeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    gatherMaterialPassDrawItems(framebuffer, pass, transparent, meshDrawItems, computeDrawItems);
    renderMeshMaterialPassDrawItems(commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState, meshDrawItems);
    renderComputeMaterialPassDrawItems(commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState, computeDrawItems);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass pass,
    const bool transparent,
    MaterialPassDrawItemVector& meshDrawItems,
    MaterialPassDrawItemVector& computeDrawItems
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();

    for(auto&& [entity, renderer] : rendererView){
        (void)entity;

        if(!renderer.visible)
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(!materialInfo || !materialInfo->valid || materialInfo->transparent != transparent)
            continue;

        GeometryResources* geometry = nullptr;
        if(!ensureGeometryLoaded(renderer.geometry, geometry))
            continue;
        if(!geometry || !geometry->valid())
            continue;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!ensureRendererPipeline(renderer, framebuffer, pass, pipelineResources))
            continue;
        if(!pipelineResources)
            continue;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = renderer.material.name();
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            if(!pipelineResources->meshletPipeline)
                continue;
            if(!ensureMeshBindingSet(*geometry))
                continue;
            MaterialPassDrawItem drawItem;
            drawItem.geometryKey = geometry->geometryName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.alpha = materialInfo->alpha;
            meshDrawItems.push_back(drawItem);
            break;
        }
        case RenderPath::ComputeEmulation:{
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                continue;
            if(!ensureComputeBindingSet(*geometry))
                continue;
            MaterialPassDrawItem drawItem;
            drawItem.geometryKey = geometry->geometryName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.alpha = materialInfo->alpha;
            computeDrawItems.push_back(drawItem);
            break;
        }
        default:{
            break;
        }
        }
    }
}

bool RendererSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    GeometryResources*& outGeometry,
    MaterialPipelineResources*& outPipelineResources)
{
    outGeometry = nullptr;
    outPipelineResources = nullptr;

    const auto foundGeometry = m_geometryMeshes.find(drawItem.geometryKey);
    if(foundGeometry == m_geometryMeshes.end())
        return false;

    const auto foundPipeline = m_materialPipelines.find(drawItem.pipelineKey);
    if(foundPipeline == m_materialPipelines.end())
        return false;

    outGeometry = &foundGeometry.value();
    outPipelineResources = &foundPipeline.value();
    return true;
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass pass,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets,
    const Core::ViewportState& viewportState,
    const MaterialPassDrawItemVector& drawItems
){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        GeometryResources* geometry = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, geometry, pipelineResources))
            continue;

        if(!geometry->valid() || !geometry->meshBindingSet || !pipelineResources->meshletPipeline)
            continue;

        commandList.setBufferState(geometry->shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
        commandList.setBufferState(geometry->shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources->meshletPipeline.get());
        meshletState.setFramebuffer(framebuffer);
        meshletState.setViewport(viewportState);
        meshletState.addBindingSet(geometry->meshBindingSet.get());
        if(passBindingSet)
            meshletState.addBindingSet(passBindingSet);

        commandList.setMeshletState(meshletState);

        if(pass == MaterialPipelinePass::Opaque){
            const __hidden_ecs_graphics::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_graphics::BuildShaderDrivenPushConstants(geometry->triangleCount, viewportState);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        else{
            const __hidden_ecs_graphics::TransparentDrawPushConstants pushConstants =
                __hidden_ecs_graphics::BuildTransparentDrawPushConstants(geometry->triangleCount, viewportState, *avboitTargets, drawItem.alpha);
            commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        commandList.dispatchMesh(geometry->dispatchGroupCount);
    }
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass pass,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets,
    const Core::ViewportState& viewportState,
    const MaterialPassDrawItemVector& drawItems
){
    for(const MaterialPassDrawItem& drawItem : drawItems){
        GeometryResources* geometry = nullptr;
        MaterialPipelineResources* pipelineResources = nullptr;
        if(!findMaterialPassDrawItemResources(drawItem, geometry, pipelineResources))
            continue;

        if(!geometry->valid() || !geometry->computeBindingSet || !geometry->emulationVertexBuffer || !pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
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
        graphicsState.setFramebuffer(framebuffer);
        graphicsState.setViewport(viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(geometry->emulationVertexBuffer.get())
                .setSlot(0)
                .setOffset(0)
        );
        if(passBindingSet){
            graphicsState.addBindingSet(nullptr);
            graphicsState.addBindingSet(passBindingSet);
        }

        commandList.setGraphicsState(graphicsState);

        if(pass != MaterialPipelinePass::Opaque){
            const __hidden_ecs_graphics::TransparentDrawPushConstants transparentPushConstants =
                __hidden_ecs_graphics::BuildTransparentDrawPushConstants(geometry->triangleCount, viewportState, *avboitTargets, drawItem.alpha);
            commandList.setPushConstants(&transparentPushConstants, sizeof(transparentPushConstants));
        }

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(geometry->indexCount);
        commandList.draw(drawArgs);
    }
}

void RendererSystem::renderAvboitPasses(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    AvboitFrameTargets& avboitTargets = targets.avboit;
    if(!avboitTargets.valid())
        return;
    if(!ensureAvboitPipelines(avboitTargets))
        return;

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitOccupancy,
        true,
        avboitTargets.occupancyBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitDepthWarp(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.lowFramebuffer.get(),
        MaterialPipelinePass::AvboitExtinction,
        true,
        avboitTargets.extinctionBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();

    dispatchAvboitIntegration(commandList, avboitTargets);

    renderMaterialPass(
        commandList,
        avboitTargets.accumulationFramebuffer.get(),
        MaterialPipelinePass::AvboitAccumulate,
        true,
        avboitTargets.accumulateBindingSet.get(),
        &avboitTargets
    );
    commandList.endRenderPass();
}

void RendererSystem::dispatchAvboitDepthWarp(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitDepthWarpPipeline || !targets.depthWarpBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.depthWarpBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitDepthWarpPipeline.get());
    computeState.addBindingSet(targets.depthWarpBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_ecs_graphics::AvboitPushConstants pushConstants =
        __hidden_ecs_graphics::BuildAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
    commandList.dispatch(1, 1, 1);
}

void RendererSystem::dispatchAvboitIntegration(Core::ICommandList& commandList, AvboitFrameTargets& targets){
    if(!m_avboitIntegratePipeline || !targets.integrateBindingSet)
        return;

    commandList.setResourceStatesForBindingSet(targets.integrateBindingSet.get());
    commandList.commitBarriers();

    Core::ComputeState computeState;
    computeState.setPipeline(m_avboitIntegratePipeline.get());
    computeState.addBindingSet(targets.integrateBindingSet.get());
    commandList.setComputeState(computeState);

    const __hidden_ecs_graphics::AvboitPushConstants pushConstants =
        __hidden_ecs_graphics::BuildAvboitPushConstants(targets, 1.f);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));

    const u32 pixelCount = targets.lowWidth * targets.lowHeight;
    commandList.dispatch(__hidden_ecs_graphics::DispatchGroupCount1D(pixelCount, 64u), 1, 1);
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

    const usize indexCount = geometry.indexData().size() / indexStride;
    if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' index count exceeds u32 limits"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }

    createdGeometry.indexCount = static_cast<u32>(indexCount);
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

    const usize expandedIndexCount = static_cast<usize>(createdGeometry.indexCount);
    if(expandedIndexCount > Limit<usize>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: geometry '{}' expanded index buffer size overflows"),
            StringConvert(geometryPath.c_str())
        );
        return false;
    }
    const usize expandedIndexBytes = expandedIndexCount * sizeof(u32);

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<u32, Core::Alloc::ScratchAllocator<u32>> expandedIndices{Core::Alloc::ScratchAllocator<u32>(scratchArena)};
    expandedIndices.resize(expandedIndexCount);
    const u8* indexBytes = geometry.indexData().data();
    if(geometry.use32BitIndices()){
        for(u32 i = 0; i < createdGeometry.indexCount; ++i){
            u32 indexValue = 0;
            NWB_MEMCPY(&indexValue, sizeof(indexValue), indexBytes + static_cast<usize>(i) * sizeof(indexValue), sizeof(indexValue));
            expandedIndices[i] = indexValue;
        }
    }
    else{
        for(u32 i = 0; i < createdGeometry.indexCount; ++i){
            u16 indexValue = 0;
            NWB_MEMCPY(&indexValue, sizeof(indexValue), indexBytes + static_cast<usize>(i) * sizeof(indexValue), sizeof(indexValue));
            expandedIndices[i] = static_cast<u32>(indexValue);
        }
    }

    Core::Graphics::BufferSetupDesc shaderIndexSetup;
    shaderIndexSetup.bufferDesc
        .setByteSize(static_cast<u64>(expandedIndexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderIndexBufferName)
    ;
    shaderIndexSetup.data = expandedIndices.data();
    shaderIndexSetup.dataSize = expandedIndexBytes;
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

bool RendererSystem::ensureMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset, MaterialSurfaceInfo*& outInfo){
    outInfo = nullptr;

    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    const auto foundInfo = m_materialSurfaceInfos.find(materialPath);
    if(foundInfo != m_materialSurfaceInfos.end()){
        outInfo = &foundInfo.value();
        return outInfo->valid;
    }

    UniquePtr<Core::Assets::IAsset> loadedAsset;
    if(!m_assetManager.loadSync(Material::AssetTypeName(), materialPath, loadedAsset)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: failed to load material '{}'"),
            StringConvert(materialPath.c_str())
        );
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("RendererSystem: asset '{}' is not a material"),
            StringConvert(materialPath.c_str())
        );
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);

    MaterialSurfaceInfo createdInfo;
    createdInfo.materialName = materialPath;
    createdInfo.shaderVariant = material.shaderVariant().empty()
        ? AString(Core::ShaderArchive::s_DefaultVariant)
        : material.shaderVariant()
    ;
    createdInfo.valid = true;

    __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Pixel, createdInfo.pixelShader);
    __hidden_ecs_graphics::TryFindShaderForStage(material, Core::ShaderType::Mesh, createdInfo.meshShader);

    CompactString alphaText;
    if(__hidden_ecs_graphics::FindMaterialParameter(material, AStringView("alpha"), alphaText)
        || __hidden_ecs_graphics::FindMaterialParameter(material, AStringView("opacity"), alphaText))
    {
        f32 parsedAlpha = 1.f;
        if(__hidden_ecs_graphics::ParseAlphaValue(AStringView(alphaText.c_str()), parsedAlpha))
            createdInfo.alpha = parsedAlpha;
        else{
            NWB_LOGGER_WARNING(
                NWB_TEXT("RendererSystem: material '{}' has invalid alpha '{}'; using 1.0"),
                StringConvert(materialPath.c_str()),
                StringConvert(alphaText.c_str())
            );
        }
    }

    CompactString modeText;
    if(__hidden_ecs_graphics::FindMaterialParameter(material, AStringView("render_mode"), modeText)
        || __hidden_ecs_graphics::FindMaterialParameter(material, AStringView("alpha_mode"), modeText)
        || __hidden_ecs_graphics::FindMaterialParameter(material, AStringView("transparency"), modeText))
    {
        createdInfo.transparent = __hidden_ecs_graphics::IsTransparentText(AStringView(modeText.c_str()));
    }
    if(createdInfo.alpha < 0.999f)
        createdInfo.transparent = true;

    auto [it, inserted] = m_materialSurfaceInfos.emplace(materialPath, MaterialSurfaceInfo{});
    (void)inserted;
    it.value() = Move(createdInfo);
    outInfo = &it.value();
    return outInfo->valid;
}

bool RendererSystem::ensureMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_graphics::TransparentDrawPushConstants)));

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


bool RendererSystem::ensureRendererPipeline(const RendererComponent& renderer, Core::IFramebuffer* framebuffer, const MaterialPipelinePass pass, MaterialPipelineResources*& outResources){
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
    pipelineKey.pass = pass;

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

    MaterialSurfaceInfo* materialInfo = nullptr;
    if(!ensureMaterialSurfaceInfo(renderer.material, materialInfo)){
        removeFailedEntry();
        return false;
    }
    if(!materialInfo || !materialInfo->valid){
        removeFailedEntry();
        return false;
    }

    const AStringView shaderVariant = materialInfo->shaderVariant.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : AStringView(materialInfo->shaderVariant)
    ;

    const bool hasPixelShader = materialInfo->pixelShader.valid();
    const bool hasMeshShader = materialInfo->meshShader.valid();
    Core::ShaderHandle passPixelShader;

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::AvboitOccupancy:
        if(!ensureAvboitResources()){
            removeFailedEntry();
            return false;
        }
        passPixelShader = m_avboitOccupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!ensureAvboitResources()){
            removeFailedEntry();
            return false;
        }
        passPixelShader = m_avboitExtinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!ensureAvboitResources()){
            removeFailedEntry();
            return false;
        }
        passPixelShader = m_avboitAccumulatePixelShader;
        break;
    default:
        break;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::RenderState renderState = __hidden_ecs_graphics::BuildRenderStateForPass(pass);

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!ensureMeshShaderResources())
            return false;
        if(!ensureShaderLoaded(resources.meshShader, materialInfo->meshShader.name(), shaderVariant, Core::ShaderType::Mesh, "ECSGraphics_RendererMesh"))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo->pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader.get());
        pipelineDesc.setPixelShader(resources.pixelShader.get());
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_meshBindingLayout.get());
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            pipelineDesc.addBindingLayout(m_avboitOccupancyBindingLayout.get());
            break;
        case MaterialPipelinePass::AvboitExtinction:
            pipelineDesc.addBindingLayout(m_avboitExtinctionBindingLayout.get());
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            pipelineDesc.addBindingLayout(m_avboitAccumulateBindingLayout.get());
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }

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
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!ensureComputeEmulationResources())
            return false;
        const Name& meshComputeArchiveStageName = ShaderStageNames::MeshComputeArchiveStageName();
        if(!ensureShaderLoaded(
            resources.computeShader,
            materialInfo->meshShader.name(),
            shaderVariant,
            Core::ShaderType::Compute,
            "ECSGraphics_RendererCS",
            &meshComputeArchiveStageName))
        {
            return false;
        }
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo->pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSGraphics_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }

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
        emulationDesc.setRenderState(renderState);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout.get());
            emulationDesc.addBindingLayout(m_avboitOccupancyBindingLayout.get());
            break;
        case MaterialPipelinePass::AvboitExtinction:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout.get());
            emulationDesc.addBindingLayout(m_avboitExtinctionBindingLayout.get());
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout.get());
            emulationDesc.addBindingLayout(m_avboitAccumulateBindingLayout.get());
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }
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
    if(pass == MaterialPipelinePass::Opaque && !hasPixelShader){
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

bool RendererSystem::hasTransparentRenderers(){
    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        (void)entity;
        if(!renderer.visible)
            continue;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(renderer.material, materialInfo))
            continue;
        if(materialInfo && materialInfo->valid && materialInfo->transparent)
            return true;
    }

    return false;
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
