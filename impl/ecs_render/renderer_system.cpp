// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "renderer_system.h"

#include "renderer_avboit.h"
#include "shader_asset_loader.h"

#include <core/assets/asset_manager.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_material/material_asset.h>
#include <impl/assets_material/material_shader_stage_names.h>
#include <impl/assets_shader/shader_asset.h>
#include <impl/ecs_camera/ecs_camera.h>
#include <impl/ecs_geometry/ecs_geometry.h>
#include <impl/ecs_lighting/ecs_lighting.h>
#include <impl/ecs_scene/ecs_scene.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_render{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
static constexpr u32 s_StaticGeometryVertexStride = sizeof(GeometryVertex);
static constexpr u32 s_EmulatedVertexStride = sizeof(f32) * 24u;
static constexpr u32 s_TrianglesPerWorkgroup = 32u;
static constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


struct ShaderDrivenPushConstants{
    u32 triangleCount = 0;
    u32 scissorCullEnabled = 0;
    u32 instanceIndex = 0;
    u32 sourceVertexLayout = 0;
    Float4 viewportRect = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 scissorRect = Float4(0.f, 0.f, 0.f, 0.f);
};

struct TransparentDrawPushConstants{
    ShaderDrivenPushConstants mesh;
    RendererAvboitPushConstants avboit;
};

struct EmulatedVertex{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float4 uv0;
    Float4 color;
    Float4 worldPosition;
};

struct MeshViewGpuData{
    Float4 worldToClip[4] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, 0.f, 1.f),
    };
    Float4 directionalLightDirection = Float4(0.f, 0.f, -1.f, 0.f);
    Float4 directionalLightColorIntensity = Float4(1.f, 1.f, 1.f, 1.f);
    Float4 cameraPosition = Float4(0.f, 0.f, 0.f, 1.f);
};

using MeshViewState = MeshViewGpuData;
using MeshViewBasis = NWB::Impl::SceneViewBasis;

struct MaterialParameterBlock{
    u32 offset = 0;
    u32 count = 0;
};

static_assert(sizeof(ShaderDrivenPushConstants) == 48, "ShaderDrivenPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) == s_RendererAvboitTransparentDrawPushConstantSize, "TransparentDrawPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize, "Transparent draw push constants must fit the portable push constant budget");
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");
static_assert(alignof(EmulatedVertex) >= alignof(Float4), "EmulatedVertex must stay SIMD-aligned");
static_assert(sizeof(MeshViewGpuData) == sizeof(f32) * 28u, "MeshViewGpuData layout must match the mesh shaders");
static_assert(alignof(MeshViewGpuData) >= alignof(Float4), "MeshViewGpuData must stay SIMD-aligned");


static constexpr Name s_MeshEmulationVertexShaderName("engine/graphics/mesh_emulation_vs");
static constexpr Name s_InstanceBufferName("ecs_render/instance_data");
static constexpr Name s_MaterialParameterBufferName("ecs_render/material_parameter_data");
static constexpr Name s_MeshViewBufferName("ecs_render/mesh_view_data");
static constexpr Name s_DeferredCompositeVertexShaderName("engine/graphics/deferred_composite_vs");
static constexpr Name s_DeferredCompositePixelShaderName("engine/graphics/deferred_composite_ps");
static constexpr Name s_WireframeOverlayPixelShaderName("engine/graphics/wireframe_overlay_ps");


template<usize N>
static Core::Format::Enum SelectSupportedFormat(
    Core::IDevice& device,
    const Core::Format::Enum (&candidates)[N],
    const Core::FormatSupport::Mask requiredSupport
){
    for(const Core::Format::Enum format : candidates){
        if((device.queryFormatSupport(format) & requiredSupport) == requiredSupport)
            return format;
    }

    return Core::Format::UNKNOWN;
}

static bool EnsurePointClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(false)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

static Core::Format::Enum SelectGBufferAlbedoFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
        Core::Format::RGBA8_UNORM,
        Core::Format::BGRA8_UNORM,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

static Core::Format::Enum SelectGBufferDepthFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::D32,
        Core::Format::D24S8,
        Core::Format::D16,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    return SelectSupportedFormat(device, candidates, requiredSupport);
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

static Core::RenderState BuildWireframeOverlayRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .disableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState
        .enableDepthClip()
        .setCullNone()
        .setFillWireframe()
        .setDepthBias(-1)
        .setSlopeScaleDepthBias(-1.0f)
    ;
    return renderState;
}

static Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::Opaque:
        return BuildGeometryRenderState();
    case MaterialPipelinePass::WireframeOverlay:
        return BuildWireframeOverlayRenderState();
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return BuildRendererAvboitVoxelRenderState();
    case MaterialPipelinePass::AvboitAccumulate:
        return BuildRendererAvboitAccumulateRenderState();
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

static usize NextGrowingCapacity(const usize currentCapacity, const usize requiredCapacity){
    usize capacity = Max<usize>(currentCapacity, 1u);
    while(capacity < requiredCapacity){
        if(capacity > (Limit<usize>::s_Max / 2u))
            return requiredCapacity;
        capacity *= 2u;
    }
    return capacity;
}

static InstanceGpuData BuildInstanceGpuData(
    const NWB::Impl::TransformComponent* transform,
    const u32 materialParameterOffset,
    const u32 materialParameterCount
){
    InstanceGpuData data;
    data.materialParameters.x = materialParameterOffset;
    data.materialParameters.y = materialParameterCount;
    if(!transform)
        return data;

    data.rotation = transform->rotation;
    data.translation = transform->position;
    data.scale = transform->scale;
    return data;
}

static void ApplyDirectionalLightMeshViewState(
    MeshViewState& state,
    const NWB::Impl::SceneDirectionalLight& light
){
    state.directionalLightDirection = light.direction;
    state.directionalLightColorIntensity = light.colorIntensity;
}

static void StoreProjectedViewColumn(
    Float4 (&outWorldToClip)[4],
    const usize columnIndex,
    const f32 viewX,
    const f32 viewY,
    const f32 viewZ,
    const f32 viewW,
    const SIMDVector projection
){
    SIMDVector column = VectorMultiply(VectorSet(viewX, viewY, viewZ, viewZ), projection);
    column = VectorMultiplyAdd(VectorSet(0.0f, 0.0f, viewW, 0.0f), VectorSplatW(projection), column);
    column = VectorSetW(column, viewZ);
    StoreFloat(column, &outWorldToClip[columnIndex]);
}

static void StoreWorldToClipMatrix(Float4 (&outWorldToClip)[4], const MeshViewBasis& basis, const Float4& projectionParams){
    const SIMDVector positionDepthBias = LoadFloat(basis.positionDepthBias);
    const SIMDVector right = LoadFloat(basis.right);
    const SIMDVector up = LoadFloat(basis.up);
    const SIMDVector forward = LoadFloat(basis.forward);
    const SIMDVector projection = LoadFloat(projectionParams);
    const f32 translationX = -VectorGetX(Vector3Dot(positionDepthBias, right));
    const f32 translationY = -VectorGetX(Vector3Dot(positionDepthBias, up));
    const f32 translationZ = -VectorGetX(Vector3Dot(positionDepthBias, forward)) + basis.positionDepthBias.w;

    StoreProjectedViewColumn(
        outWorldToClip,
        0u,
        basis.right.x,
        basis.up.x,
        basis.forward.x,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        1u,
        basis.right.y,
        basis.up.y,
        basis.forward.y,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        2u,
        basis.right.z,
        basis.up.z,
        basis.forward.z,
        0.0f,
        projection
    );
    StoreProjectedViewColumn(
        outWorldToClip,
        3u,
        translationX,
        translationY,
        translationZ,
        1.0f,
        projection
    );
}

static MeshViewState ResolveMeshViewState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    MeshViewState state;
    const MeshViewBasis defaultBasis = NWB::Impl::BuildDefaultSceneViewBasis();

    const NWB::Impl::SceneCameraView cameraView = NWB::Impl::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreWorldToClipMatrix(
            state.worldToClip,
            NWB::Impl::BuildSceneViewBasis(*cameraView.transform),
            cameraView.projectionData.projectionParams
        );
        StoreFloat(VectorSetW(LoadFloat(cameraView.transform->position), 1.0f), &state.cameraPosition);
    }
    else{
        StoreWorldToClipMatrix(
            state.worldToClip,
            defaultBasis,
            NWB::Impl::BuildDefaultCameraProjectionParams(fallbackAspectRatio)
        );
        state.cameraPosition = Float4(
            defaultBasis.positionDepthBias.x,
            defaultBasis.positionDepthBias.y,
            defaultBasis.positionDepthBias.z,
            1.0f
        );
    }

    __hidden_ecs_render::ApplyDirectionalLightMeshViewState(
        state,
        NWB::Impl::ResolveSceneDirectionalLight(world, defaultBasis)
    );

    return state;
}

static ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 triangleCount,
    const u32 instanceIndex,
    const u32 sourceVertexLayout,
    const Core::ViewportState& viewportState
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.triangleCount = triangleCount;
    pushConstants.instanceIndex = instanceIndex;
    pushConstants.sourceVertexLayout = sourceVertexLayout;

    if(viewportState.viewports.empty())
        return pushConstants;

    const Core::Viewport& viewport = viewportState.viewports[0];
    pushConstants.scissorCullEnabled = 1;
    pushConstants.viewportRect = Float4(viewport.minX, viewport.minY, viewport.maxX, viewport.maxY);

    Core::Rect scissorRect(viewport);
    if(!viewportState.scissorRects.empty())
        scissorRect = viewportState.scissorRects[0];

    pushConstants.scissorRect = Float4(
        static_cast<f32>(scissorRect.minX),
        static_cast<f32>(scissorRect.minY),
        static_cast<f32>(scissorRect.maxX),
        static_cast<f32>(scissorRect.maxY)
    );
    return pushConstants;
}

static TransparentDrawPushConstants BuildTransparentDrawPushConstants(
    const u32 triangleCount,
    const u32 instanceIndex,
    const u32 sourceVertexLayout,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const f32 alpha
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(triangleCount, instanceIndex, sourceVertexLayout, viewportState);
    pushConstants.avboit = BuildRendererAvboitPushConstants(targets, alpha);
    return pushConstants;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


usize RendererSystem::MaterialPipelineKeyHasher::operator()(const MaterialPipelineKey& key)const{
    usize seed = Hasher<Name>{}(key.material);
    Core::CoreDetail::HashCombine(seed, static_cast<u32>(key.pass));
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.depthFormat);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleCount);
    Core::CoreDetail::HashCombine(seed, key.framebufferInfo.sampleQuality);
    for(const Core::Format::Enum format : key.framebufferInfo.colorFormats)
        Core::CoreDetail::HashCombine(seed, format);

    return seed;
}

bool RendererSystem::MaterialPipelineKeyEqualTo::operator()(const MaterialPipelineKey& lhs, const MaterialPipelineKey& rhs)const{
    return lhs.material == rhs.material && lhs.pass == rhs.pass && lhs.framebufferInfo == rhs.framebufferInfo;
}


RendererSystem::RendererSystem(
    Core::Alloc::CustomArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_geometryMeshes(0, Hasher<Name>(), EqualTo<Name>(), GeometryResourcesMapAllocator(arena))
    , m_materialSurfaceInfos(0, Hasher<Name>(), EqualTo<Name>(), MaterialSurfaceInfoMapAllocator(arena))
    , m_materialPipelines(0, MaterialPipelineKeyHasher(), MaterialPipelineKeyEqualTo(), MaterialPipelineMapAllocator(arena))
    , m_loggedMaterialPaths(0, Hasher<Name>(), EqualTo<Name>(), LoggedMaterialPathMapAllocator(arena))
    , m_runtimeGeometryProviders(RuntimeGeometryProviderAllocator(arena))
{
    readAccess<NWB::Impl::SceneComponent>();
    readAccess<NWB::Impl::TransformComponent>();
    readAccess<NWB::Impl::CameraComponent>();
    readAccess<NWB::Impl::GeometryComponent>();
    readAccess<RendererComponent>();
}
RendererSystem::~RendererSystem(){}


void RendererSystem::update(Core::ECS::World& world, f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

void RendererSystem::render(Core::IFramebuffer* framebuffer){
    if(!framebuffer)
        return;

    pruneRuntimeGeometryResources();

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

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector opaqueMeshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector opaqueComputeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector wireframeMeshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector wireframeComputeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    InstanceGpuDataVector instanceData{Core::Alloc::ScratchAllocator<InstanceGpuData>(scratchArena)};
    MaterialParameterGpuDataVector materialParameters{Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)};

    Core::ViewportState deferredViewportState;
    deferredViewportState.addViewportAndScissorRect(deferredTargets->framebuffer->getFramebufferInfo().getViewport());

    const Core::FramebufferInfoEx& meshViewFramebufferInfo = deferredTargets->framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = 1.0f;
    if(meshViewFramebufferInfo.width != 0 && meshViewFramebufferInfo.height != 0)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
    const bool meshViewReady = ensureMeshViewBuffer(*commandList, meshViewAspectRatio);
    if(meshViewReady){
        gatherMaterialPassDrawItems(
            deferredTargets->framebuffer.get(),
            MaterialPipelinePass::Opaque,
            false,
            opaqueMeshDrawItems,
            opaqueComputeDrawItems,
            instanceData,
            materialParameters
        );
        if(m_wireframeOverlayEnabled){
            gatherMaterialPassDrawItems(
                deferredTargets->framebuffer.get(),
                MaterialPipelinePass::WireframeOverlay,
                false,
                wireframeMeshDrawItems,
                wireframeComputeDrawItems,
                instanceData,
                materialParameters
            );
            gatherMaterialPassDrawItems(
                deferredTargets->framebuffer.get(),
                MaterialPipelinePass::WireframeOverlay,
                true,
                wireframeMeshDrawItems,
                wireframeComputeDrawItems,
                instanceData,
                materialParameters
            );
        }
    }

    const bool hasDeferredDrawItems =
        !opaqueMeshDrawItems.empty()
        || !opaqueComputeDrawItems.empty()
        || !wireframeMeshDrawItems.empty()
        || !wireframeComputeDrawItems.empty()
    ;
    const bool deferredUploadReady =
        hasDeferredDrawItems
        && uploadInstanceBuffer(*commandList, instanceData)
        && uploadMaterialParameterBuffer(*commandList, materialParameters)
    ;
    if(deferredUploadReady){
        const MaterialPassDrawContext opaqueDrawContext{
            *commandList,
            deferredTargets->framebuffer.get(),
            MaterialPipelinePass::Opaque,
            nullptr,
            nullptr,
            deferredViewportState
        };
        renderMeshMaterialPassDrawItems(opaqueDrawContext, opaqueMeshDrawItems);
        renderComputeMaterialPassDrawItems(opaqueDrawContext, opaqueComputeDrawItems);

        if(m_wireframeOverlayEnabled){
            const MaterialPassDrawContext wireframeDrawContext{
                *commandList,
                deferredTargets->framebuffer.get(),
                MaterialPipelinePass::WireframeOverlay,
                nullptr,
                nullptr,
                deferredViewportState
            };
            renderMeshMaterialPassDrawItems(wireframeDrawContext, wireframeMeshDrawItems);
            renderComputeMaterialPassDrawItems(wireframeDrawContext, wireframeComputeDrawItems);
        }
    }
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
    Core::ICommandList* commandLists[] = { commandList.get() };
    device->executeCommandLists(commandLists, 1);
}

void RendererSystem::backBufferResizing(){
    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

void RendererSystem::backBufferResized(u32 width, u32 height, u32 sampleCount){
    static_cast<void>(width);
    static_cast<void>(height);
    static_cast<void>(sampleCount);

    m_materialPipelines.clear();
    m_deferredCompositePipeline.reset();
    resetDeferredFrameTargets();
}

void RendererSystem::registerRuntimeGeometryProvider(IRuntimeGeometryProvider& provider){
    if(FindIf(
        m_runtimeGeometryProviders.begin(),
        m_runtimeGeometryProviders.end(),
        [&provider](IRuntimeGeometryProvider* item){ return item == &provider; }
    ) != m_runtimeGeometryProviders.end())
        return;

    m_runtimeGeometryProviders.push_back(&provider);
}

void RendererSystem::unregisterRuntimeGeometryProvider(IRuntimeGeometryProvider& provider){
    const auto found = FindIf(
        m_runtimeGeometryProviders.begin(),
        m_runtimeGeometryProviders.end(),
        [&provider](IRuntimeGeometryProvider* item){ return item == &provider; }
    );
    if(found == m_runtimeGeometryProviders.end())
        return;

    m_runtimeGeometryProviders.erase(found);
    pruneRuntimeGeometryResources();
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
    const Core::Format::Enum albedoFormat = __hidden_ecs_render::SelectGBufferAlbedoFormat(*device);
    const Core::Format::Enum depthFormat = __hidden_ecs_render::SelectGBufferDepthFormat(*device);
    const Core::Format::Enum avboitLowRasterFormat = SelectRendererAvboitLowRasterFormat(*device);
    const Core::Format::Enum avboitAccumColorFormat = SelectRendererAvboitAccumColorFormat(*device);
    const Core::Format::Enum avboitAccumExtinctionFormat = SelectRendererAvboitAccumExtinctionFormat(*device);
    const Core::Format::Enum avboitTransmittanceFormat = SelectRendererAvboitTransmittanceFormat(*device);
    if(albedoFormat == Core::Format::UNKNOWN || depthFormat == Core::Format::UNKNOWN){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported deferred framebuffer formats"));
        return false;
    }
    if(
        avboitLowRasterFormat == Core::Format::UNKNOWN
        || avboitAccumColorFormat == Core::Format::UNKNOWN
        || avboitAccumExtinctionFormat == Core::Format::UNKNOWN
        || avboitTransmittanceFormat == Core::Format::UNKNOWN
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to find supported AVBOIT framebuffer formats"));
        return false;
    }

    if(
        m_deferredTargets.valid()
        && m_deferredTargets.width == presentationInfo.width
        && m_deferredTargets.height == presentationInfo.height
        && m_deferredTargets.albedoFormat == albedoFormat
        && m_deferredTargets.depthFormat == depthFormat
        && m_deferredTargets.avboit.lowRasterFormat == avboitLowRasterFormat
        && m_deferredTargets.avboit.accumColorFormat == avboitAccumColorFormat
        && m_deferredTargets.avboit.accumExtinctionFormat == avboitAccumExtinctionFormat
        && m_deferredTargets.avboit.transmittanceFormat == avboitTransmittanceFormat
    ){
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
        .setClearValue(__hidden_ecs_render::s_ClearColor)
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
        .addColorAttachment(createdTargets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources)
        .setDepthAttachment(createdTargets.depth.get(), __hidden_ecs_render::s_FramebufferSubresources)
    ;
    createdTargets.framebuffer = device->createFramebuffer(framebufferDesc);
    if(!createdTargets.framebuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred framebuffer"));
        return false;
    }

    if(!createAvboitFrameTargets(
        createdTargets,
        avboitLowRasterFormat,
        avboitAccumColorFormat,
        avboitAccumExtinctionFormat,
        avboitTransmittanceFormat
    ))
        return false;

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        0,
        createdTargets.albedo.get(),
        createdTargets.albedoFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        1,
        createdTargets.avboit.accumColor.get(),
        createdTargets.avboit.accumColorFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Texture_SRV(
        2,
        createdTargets.avboit.accumExtinction.get(),
        createdTargets.avboit.accumExtinctionFormat,
        __hidden_ecs_render::s_FramebufferSubresources,
        Core::TextureDimension::Texture2D
    ));
    bindingSetDesc.addItem(Core::BindingSetItem::Sampler(3, m_deferredSampler.get()));
    createdTargets.compositeBindingSet = device->createBindingSet(bindingSetDesc, m_deferredCompositeBindingLayout);
    if(!createdTargets.compositeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite binding set"));
        return false;
    }

    m_deferredTargets = Move(createdTargets);
    outTargets = &m_deferredTargets;

    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: deferred rendering targets ready ({}x{}, albedo {}, depth {}, AVBOIT color {}, extinction {}, transmittance {})")
        , m_deferredTargets.width
        , m_deferredTargets.height
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.albedoFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.depthFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumColorFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.accumExtinctionFormat).name)
        , StringConvert(Core::GetFormatInfo(m_deferredTargets.avboit.transmittanceFormat).name)
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

    if(!__hidden_ecs_render::EnsurePointClampSampler(*device, m_deferredSampler, NWB_TEXT("RendererSystem: failed to create deferred composite sampler")))
        return false;

    if(!ensureShaderLoaded(
        m_deferredCompositeVertexShader,
        __hidden_ecs_render::s_DeferredCompositeVertexShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Vertex,
        "ECSRender_DeferredCompositeVS"
    ))
        return false;

    if(!ensureShaderLoaded(
        m_deferredCompositePixelShader,
        __hidden_ecs_render::s_DeferredCompositePixelShaderName,
        Core::ShaderArchive::s_DefaultVariant,
        Core::ShaderType::Pixel,
        "ECSRender_DeferredCompositePS"
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
        .setVertexShader(m_deferredCompositeVertexShader)
        .setPixelShader(m_deferredCompositePixelShader)
        .setRenderState(__hidden_ecs_render::BuildCompositeRenderState())
        .addBindingLayout(m_deferredCompositeBindingLayout)
    ;

    Core::IDevice* device = m_graphics.getDevice();
    m_deferredCompositePipeline = device->createGraphicsPipeline(pipelineDesc, framebufferInfo);
    if(!m_deferredCompositePipeline){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create deferred composite pipeline"));
        return false;
    }

    return true;
}

void RendererSystem::clearDeferredTargets(Core::ICommandList& commandList, DeferredFrameTargets& targets){
    if(targets.albedo){
        commandList.setTextureState(targets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    if(targets.depth){
        commandList.setTextureState(targets.depth.get(), __hidden_ecs_render::s_FramebufferSubresources, Core::ResourceStates::CopyDest);
    }

    commandList.commitBarriers();

    if(targets.albedo){
        commandList.clearTextureFloat(targets.albedo.get(), __hidden_ecs_render::s_FramebufferSubresources, __hidden_ecs_render::s_ClearColor);
    }

    if(targets.depth){
        commandList.clearDepthStencilTexture(
            targets.depth.get(),
            __hidden_ecs_render::s_FramebufferSubresources,
            true,
            Core::s_DepthClearValue,
            false,
            0
        );
    }
}

void RendererSystem::renderMaterialPass(
    Core::ICommandList& commandList,
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    Core::IBindingSet* passBindingSet,
    const AvboitFrameTargets* avboitTargets
){
    if(!framebuffer)
        return;
    const bool usesAvboit = MaterialPipelinePassUsesRendererAvboit(pass);
    if(usesAvboit && (!passBindingSet || !avboitTargets || !avboitTargets->valid()))
        return;

    commandList.endRenderPass();

    Core::Alloc::ScratchArena<> scratchArena;
    MaterialPassDrawItemVector meshDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    MaterialPassDrawItemVector computeDrawItems{Core::Alloc::ScratchAllocator<MaterialPassDrawItem>(scratchArena)};
    InstanceGpuDataVector instanceData{Core::Alloc::ScratchAllocator<InstanceGpuData>(scratchArena)};
    MaterialParameterGpuDataVector materialParameters{Core::Alloc::ScratchAllocator<MaterialParameterGpuData>(scratchArena)};

    Core::ViewportState viewportState;
    viewportState.addViewportAndScissorRect(framebuffer->getFramebufferInfo().getViewport());

    const Core::FramebufferInfoEx& meshViewFramebufferInfo = framebuffer->getFramebufferInfo();
    f32 meshViewAspectRatio = 1.0f;
    if(meshViewFramebufferInfo.width != 0 && meshViewFramebufferInfo.height != 0)
        meshViewAspectRatio = static_cast<f32>(meshViewFramebufferInfo.width) / static_cast<f32>(meshViewFramebufferInfo.height);
    if(avboitTargets && avboitTargets->fullWidth > 0 && avboitTargets->fullHeight > 0)
        meshViewAspectRatio = static_cast<f32>(avboitTargets->fullWidth) / static_cast<f32>(avboitTargets->fullHeight);
    if(!ensureMeshViewBuffer(commandList, meshViewAspectRatio))
        return;

    gatherMaterialPassDrawItems(framebuffer, pass, transparent, meshDrawItems, computeDrawItems, instanceData, materialParameters);
    if(meshDrawItems.empty() && computeDrawItems.empty())
        return;

    if(!uploadInstanceBuffer(commandList, instanceData))
        return;
    if(!uploadMaterialParameterBuffer(commandList, materialParameters))
        return;

    if(passBindingSet){
        commandList.setResourceStatesForBindingSet(passBindingSet);
        commandList.commitBarriers();
    }

    const MaterialPassDrawContext drawContext{ commandList, framebuffer, pass, passBindingSet, avboitTargets, viewportState };
    renderMeshMaterialPassDrawItems(drawContext, meshDrawItems);
    renderComputeMaterialPassDrawItems(drawContext, computeDrawItems);
}

void RendererSystem::gatherMaterialPassDrawItems(
    Core::IFramebuffer* framebuffer,
    const MaterialPipelinePass::Enum pass,
    const bool transparent,
    MaterialPassDrawItemVector& meshDrawItems,
    MaterialPassDrawItemVector& computeDrawItems,
    InstanceGpuDataVector& instanceData,
    MaterialParameterGpuDataVector& materialParameters
){
    if(!framebuffer)
        return;

    auto rendererView = m_world.view<RendererComponent>();
    auto* geometrySystem = m_world.getSystem<NWB::Impl::GeometrySystem>();
    usize rendererCapacity = rendererView.candidateCount();
    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        const usize providerCandidateCount = provider->runtimeGeometryCandidateCount();
        if(providerCandidateCount > Limit<usize>::s_Max - rendererCapacity){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry provider candidate count overflow"));
            break;
        }
        rendererCapacity += providerCandidateCount;
    }
    meshDrawItems.reserve(rendererCapacity);
    computeDrawItems.reserve(rendererCapacity);
    instanceData.reserve(rendererCapacity);
    materialParameters.reserve(rendererCapacity);

    using MaterialParameterBlockPair = Pair<Name, __hidden_ecs_render::MaterialParameterBlock>;
    using MaterialParameterBlockMap = HashMap<
        Name,
        __hidden_ecs_render::MaterialParameterBlock,
        Hasher<Name>,
        EqualTo<Name>,
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>
    >;
    MaterialParameterBlockMap materialParameterBlocks(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        Core::Alloc::ScratchAllocator<MaterialParameterBlockPair>(materialParameters.get_allocator())
    );
    materialParameterBlocks.reserve(rendererCapacity);

    const Core::FramebufferInfo& framebufferInfo = framebuffer->getFramebufferInfo();

    auto ensureMaterialParameterBlock = [&](
        const MaterialSurfaceInfo& materialInfo,
        __hidden_ecs_render::MaterialParameterBlock& outBlock
    ) -> bool{
        const auto foundBlock = materialParameterBlocks.find(materialInfo.materialName);
        if(foundBlock != materialParameterBlocks.end()){
            outBlock = foundBlock.value();
            return true;
        }

        if(materialParameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter offset exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max)){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter count exceeds u32 limits"));
            return false;
        }
        if(materialInfo.parameters.size() > static_cast<usize>(Limit<u32>::s_Max) - materialParameters.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: gathered material parameter count exceeds u32 limits"));
            return false;
        }

        outBlock.offset = static_cast<u32>(materialParameters.size());
        outBlock.count = static_cast<u32>(materialInfo.parameters.size());
        const usize requiredParameterCapacity = materialParameters.size() + materialInfo.parameters.size();
        if(requiredParameterCapacity > materialParameters.capacity())
            materialParameters.reserve(__hidden_ecs_render::NextGrowingCapacity(
                materialParameters.capacity(),
                requiredParameterCapacity
            ));
        AppendTriviallyCopyableVector(materialParameters, materialInfo.parameters);

        materialParameterBlocks.emplace(materialInfo.materialName, outBlock);
        return true;
    };

    auto appendDrawForGeometry = [&](
        const Core::ECS::EntityID entity,
        const Core::Assets::AssetRef<Material>& material,
        GeometryResources& geometry
    ) -> bool{
        if(!geometry.valid())
            return false;

        const NWB::Impl::TransformComponent* transform =
            m_world.tryGetComponent<NWB::Impl::TransformComponent>(entity)
        ;

        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(material, materialInfo))
            return false;
        if(!materialInfo || !materialInfo->valid || materialInfo->transparent != transparent)
            return false;

        MaterialPipelineKey pipelineKey;
        pipelineKey.material = materialInfo->materialName;
        pipelineKey.framebufferInfo = framebufferInfo;
        pipelineKey.pass = pass;

        MaterialPipelineResources* pipelineResources = nullptr;
        if(!ensureRendererPipeline(*materialInfo, pipelineKey, framebuffer, pipelineResources))
            return false;
        if(!pipelineResources)
            return false;

        auto appendInstance = [&]() -> u32{
            if(instanceData.size() >= static_cast<usize>(Limit<u32>::s_Max)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer instance count exceeds u32 limits"));
                return Limit<u32>::s_Max;
            }

            __hidden_ecs_render::MaterialParameterBlock parameterBlock;
            if(!ensureMaterialParameterBlock(*materialInfo, parameterBlock))
                return Limit<u32>::s_Max;

            const u32 instanceIndex = static_cast<u32>(instanceData.size());
            instanceData.push_back(__hidden_ecs_render::BuildInstanceGpuData(
                transform,
                parameterBlock.offset,
                parameterBlock.count
            ));
            return instanceIndex;
        };

        auto appendDrawItem = [&](MaterialPassDrawItemVector& drawItems) -> bool{
            const u32 instanceIndex = appendInstance();
            if(instanceIndex == Limit<u32>::s_Max)
                return false;

            MaterialPassDrawItem drawItem;
            drawItem.geometryKey = geometry.geometryName;
            drawItem.pipelineKey = pipelineKey;
            drawItem.instanceIndex = instanceIndex;
            drawItem.alpha = materialInfo->alpha;
            drawItems.push_back(drawItem);
            return true;
        };

        switch(pipelineResources->renderPath){
        case RenderPath::MeshShader:{
            if(!pipelineResources->meshletPipeline)
                return false;
            return appendDrawItem(meshDrawItems);
        }
        case RenderPath::ComputeEmulation:{
            if(!pipelineResources->computePipeline || !pipelineResources->emulationPipeline)
                return false;
            return appendDrawItem(computeDrawItems);
        }
        default:
            return false;
        }
    };

    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(!geometrySystem){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GeometrySystem is not registered; static renderers cannot resolve geometry"));
            break;
        }

        Core::Assets::AssetRef<Geometry> geometryAsset;
        if(!geometrySystem->resolveGeometry(entity, geometryAsset))
            continue;

        GeometryResources* geometry = nullptr;
        if(!ensureGeometryLoaded(geometryAsset, geometry))
            continue;
        if(geometry)
            appendDrawForGeometry(entity, renderer.material, *geometry);
    }

    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        provider->forEachRuntimeGeometry(
            [&](const RuntimeGeometryDesc& desc){
                if(!desc.valid())
                    return;

                GeometryResources* geometry = nullptr;
                if(!ensureRuntimeGeometryResources(desc, geometry))
                    return;
                if(geometry)
                    appendDrawForGeometry(desc.entity, desc.material, *geometry);
            }
        );
    }
}

bool RendererSystem::ensureInstanceBufferCapacity(const usize instanceCount){
    if(instanceCount == 0)
        return true;
    if(instanceCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer request exceeds u32 instance-index limits"));
        return false;
    }
    if(m_instanceBuffer && m_instanceBufferCapacity >= instanceCount)
        return true;

    const usize capacity = __hidden_ecs_render::NextGrowingCapacity(m_instanceBufferCapacity, instanceCount);
    if(capacity > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc instanceBufferDesc;
    instanceBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(InstanceGpuData)))
        .setStructStride(sizeof(InstanceGpuData))
        .setDebugName(__hidden_ecs_render::s_InstanceBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle instanceBuffer = m_graphics.createBuffer(instanceBufferDesc);
    if(!instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create instance data buffer"));
        return false;
    }

    m_instanceBuffer = Move(instanceBuffer);
    m_instanceBufferCapacity = capacity;
    invalidateGeometryBindingSets();
    return true;
}

bool RendererSystem::ensureMaterialParameterBufferCapacity(const usize parameterCount){
    const usize requiredCount = Max<usize>(parameterCount, 1u);
    if(requiredCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer request exceeds u32 limits"));
        return false;
    }
    if(m_materialParameterBuffer && m_materialParameterBufferCapacity >= requiredCount)
        return true;

    const usize capacity = __hidden_ecs_render::NextGrowingCapacity(m_materialParameterBufferCapacity, requiredCount);
    if(capacity > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter buffer capacity overflows addressable memory"));
        return false;
    }

    Core::BufferDesc materialParameterBufferDesc;
    materialParameterBufferDesc
        .setByteSize(static_cast<u64>(capacity * sizeof(MaterialParameterGpuData)))
        .setStructStride(sizeof(MaterialParameterGpuData))
        .setDebugName(__hidden_ecs_render::s_MaterialParameterBufferName)
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;
    Core::BufferHandle materialParameterBuffer = m_graphics.createBuffer(materialParameterBufferDesc);
    if(!materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create material parameter buffer"));
        return false;
    }

    m_materialParameterBuffer = Move(materialParameterBuffer);
    m_materialParameterBufferCapacity = capacity;
    invalidateGeometryBindingSets();
    return true;
}

bool RendererSystem::ensureMeshViewBuffer(Core::ICommandList& commandList, const f32 fallbackAspectRatio){
    if(!m_meshViewBuffer){
        Core::BufferDesc meshViewBufferDesc;
        meshViewBufferDesc
            .setByteSize(sizeof(__hidden_ecs_render::MeshViewGpuData))
            .setIsConstantBuffer(true)
            .setDebugName(__hidden_ecs_render::s_MeshViewBufferName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle meshViewBuffer = m_graphics.createBuffer(meshViewBufferDesc);
        if(!meshViewBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh view buffer"));
            return false;
        }

        m_meshViewBuffer = Move(meshViewBuffer);
        invalidateGeometryBindingSets();
    }

    const __hidden_ecs_render::MeshViewState viewState =
        __hidden_ecs_render::ResolveMeshViewState(m_world, fallbackAspectRatio)
    ;

    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_meshViewBuffer.get(), &viewState, sizeof(viewState));
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadInstanceBuffer(Core::ICommandList& commandList, const InstanceGpuDataVector& instanceData){
    if(instanceData.empty())
        return true;
    if(!ensureInstanceBufferCapacity(instanceData.size()))
        return false;
    if(!m_instanceBuffer)
        return false;

    if(instanceData.size() > Limit<usize>::s_Max / sizeof(InstanceGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: instance data upload size overflows"));
        return false;
    }

    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_instanceBuffer.get(), instanceData.data(), instanceData.size() * sizeof(InstanceGpuData));
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

bool RendererSystem::uploadMaterialParameterBuffer(Core::ICommandList& commandList, const MaterialParameterGpuDataVector& materialParameters){
    const usize uploadCount = Max<usize>(materialParameters.size(), 1u);
    if(!ensureMaterialParameterBufferCapacity(uploadCount))
        return false;
    if(!m_materialParameterBuffer)
        return false;

    if(uploadCount > Limit<usize>::s_Max / sizeof(MaterialParameterGpuData)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material parameter data upload size overflows"));
        return false;
    }

    MaterialParameterGpuData fallbackParameter;
    const MaterialParameterGpuData* data = materialParameters.empty() ? &fallbackParameter : materialParameters.data();
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::CopyDest);
    commandList.commitBarriers();
    commandList.writeBuffer(m_materialParameterBuffer.get(), data, uploadCount * sizeof(MaterialParameterGpuData));
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.commitBarriers();
    return true;
}

void RendererSystem::invalidateGeometryBindingSets(){
    m_emulationViewBindingSet = nullptr;
    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end(); ++it){
        GeometryResources& geometry = it.value();
        geometry.meshBindingSet = nullptr;
        geometry.computeBindingSet = nullptr;
    }
}

bool RendererSystem::findMaterialPassDrawItemResources(
    const MaterialPassDrawItem& drawItem,
    GeometryResources*& outGeometry,
    MaterialPipelineResources*& outPipelineResources
){
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

void RendererSystem::setMaterialPassCommonBufferStates(Core::ICommandList& commandList, const GeometryResources& geometry){
    commandList.setBufferState(geometry.shaderVertexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(geometry.shaderIndexBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_instanceBuffer.get(), Core::ResourceStates::ShaderResource);
    commandList.setBufferState(m_meshViewBuffer.get(), Core::ResourceStates::ConstantBuffer);
    commandList.setBufferState(m_materialParameterBuffer.get(), Core::ResourceStates::ShaderResource);
}

void RendererSystem::renderMeshMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.meshletPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
            return;
        if(!ensureMeshBindingSet(geometry))
            return;

        setMaterialPassCommonBufferStates(context.commandList, geometry);

        Core::MeshletState meshletState;
        meshletState.setPipeline(pipelineResources.meshletPipeline.get());
        meshletState.setFramebuffer(context.framebuffer);
        meshletState.setViewport(context.viewportState);
        meshletState.addBindingSet(geometry.meshBindingSet.get());
        if(context.passBindingSet)
            meshletState.addBindingSet(context.passBindingSet);

        context.commandList.setMeshletState(meshletState);

        if(!MaterialPipelinePassUsesRendererAvboit(context.pass)){
            const __hidden_ecs_render::ShaderDrivenPushConstants pushConstants =
                __hidden_ecs_render::BuildShaderDrivenPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        else{
            const __hidden_ecs_render::TransparentDrawPushConstants pushConstants =
                __hidden_ecs_render::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        }
        context.commandList.dispatchMesh(geometry.dispatchGroupCount);
    });
}

void RendererSystem::renderComputeMaterialPassDrawItems(
    const MaterialPassDrawContext& context,
    const MaterialPassDrawItemVector& drawItems
){
    if(drawItems.empty())
        return;
    if(!m_meshViewBuffer || !ensureEmulationViewResources() || !m_emulationViewBindingSet)
        return;

    forEachMaterialPassDrawItemResources(drawItems, [&](const MaterialPassDrawItem& drawItem, GeometryResources& geometry, MaterialPipelineResources& pipelineResources){
        if(!geometry.valid() || !pipelineResources.computePipeline || !pipelineResources.emulationPipeline || !m_instanceBuffer || !m_meshViewBuffer || !m_materialParameterBuffer)
            return;
        if(!ensureComputeBindingSet(geometry))
            return;
        if(!geometry.computeBindingSet || !geometry.emulationVertexBuffer)
            return;

        setMaterialPassCommonBufferStates(context.commandList, geometry);
        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::UnorderedAccess);

        Core::ComputeState computeState;
        computeState.setPipeline(pipelineResources.computePipeline.get());
        computeState.addBindingSet(geometry.computeBindingSet.get());

        context.commandList.setComputeState(computeState);

        const __hidden_ecs_render::ShaderDrivenPushConstants pushConstants =
            __hidden_ecs_render::BuildShaderDrivenPushConstants(
                geometry.triangleCount,
                drawItem.instanceIndex,
                geometry.sourceVertexLayout,
                context.viewportState
            );
        context.commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
        context.commandList.dispatch(geometry.dispatchGroupCount);

        context.commandList.setBufferState(geometry.emulationVertexBuffer.get(), Core::ResourceStates::VertexBuffer);

        Core::GraphicsState graphicsState;
        graphicsState.setPipeline(pipelineResources.emulationPipeline.get());
        graphicsState.setFramebuffer(context.framebuffer);
        graphicsState.setViewport(context.viewportState);
        graphicsState.addVertexBuffer(
            Core::VertexBufferBinding()
                .setBuffer(geometry.emulationVertexBuffer.get())
                .setSlot(0)
                .setOffset(0)
        );
        graphicsState.addBindingSet(m_emulationViewBindingSet.get());
        if(context.passBindingSet)
            graphicsState.addBindingSet(context.passBindingSet);

        context.commandList.setGraphicsState(graphicsState);

        if(MaterialPipelinePassUsesRendererAvboit(context.pass)){
            const __hidden_ecs_render::TransparentDrawPushConstants transparentPushConstants =
                __hidden_ecs_render::BuildTransparentDrawPushConstants(
                    geometry.triangleCount,
                    drawItem.instanceIndex,
                    geometry.sourceVertexLayout,
                    context.viewportState,
                    *context.avboitTargets,
                    drawItem.alpha
                );
            context.commandList.setPushConstants(&transparentPushConstants, sizeof(transparentPushConstants));
        }

        Core::DrawArguments drawArgs;
        drawArgs.setVertexCount(geometry.indexCount);
        context.commandList.draw(drawArgs);
    });
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Geometry::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not geometry"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(*loadedAsset);

    GeometryResources createdGeometry;
    createdGeometry.geometryName = geometryPath;
    createdGeometry.sourceVertexLayout = MeshSourceLayout::GeometryVertex;

    const usize indexCount = geometry.indices().size();
    if(indexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count exceeds u32 limits"), StringConvert(geometryPath.c_str()));
        return false;
    }

    createdGeometry.indexCount = static_cast<u32>(indexCount);
    if(createdGeometry.indexCount == 0 || (createdGeometry.indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' index count {} is incompatible with triangle-based mesh rendering")
            , StringConvert(geometryPath.c_str())
            , createdGeometry.indexCount
        );
        return false;
    }

    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = DivideUp(createdGeometry.triangleCount, __hidden_ecs_render::s_TrianglesPerWorkgroup);
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' produced no dispatch groups"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const Name shaderVertexBufferName = DeriveName(geometryPath, AStringView(":shader_vb"));
    const Name shaderIndexBufferName = DeriveName(geometryPath, AStringView(":shader_ib"));
    if(!shaderVertexBufferName || !shaderIndexBufferName){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive shader-driven buffer names for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    Core::Graphics::BufferSetupDesc shaderVertexSetup;
    shaderVertexSetup.bufferDesc
        .setByteSize(static_cast<u64>(geometry.vertices().size() * sizeof(GeometryVertex)))
        .setStructStride(__hidden_ecs_render::s_StaticGeometryVertexStride)
        .setDebugName(shaderVertexBufferName)
    ;
    shaderVertexSetup.data = geometry.vertices().data();
    shaderVertexSetup.dataSize = geometry.vertices().size() * sizeof(GeometryVertex);
    createdGeometry.shaderVertexBuffer = m_graphics.setupBuffer(shaderVertexSetup);
    if(!createdGeometry.shaderVertexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader vertex buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    const usize expandedIndexCount = static_cast<usize>(createdGeometry.indexCount);
    if(expandedIndexCount > Limit<usize>::s_Max / sizeof(u32)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: geometry '{}' expanded index buffer size overflows"), StringConvert(geometryPath.c_str()));
        return false;
    }
    const usize expandedIndexBytes = expandedIndexCount * sizeof(u32);

    Core::Graphics::BufferSetupDesc shaderIndexSetup;
    shaderIndexSetup.bufferDesc
        .setByteSize(static_cast<u64>(expandedIndexBytes))
        .setStructStride(sizeof(u32))
        .setDebugName(shaderIndexBufferName)
    ;
    shaderIndexSetup.data = geometry.indices().data();
    shaderIndexSetup.dataSize = expandedIndexBytes;
    createdGeometry.shaderIndexBuffer = m_graphics.setupBuffer(shaderIndexSetup);
    if(!createdGeometry.shaderIndexBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create shader index buffer for geometry '{}'"), StringConvert(geometryPath.c_str()));
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(geometryPath, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

bool RendererSystem::ensureRuntimeGeometryResources(const RuntimeGeometryDesc& desc, GeometryResources*& outGeometry){
    outGeometry = nullptr;

    if(!desc.valid())
        return false;

    const auto foundGeometry = m_geometryMeshes.find(desc.geometryKey);
    if(foundGeometry != m_geometryMeshes.end()){
        if(!foundGeometry.value().runtimeGeometry){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry '{}' collides with a static geometry resource")
                , StringConvert(desc.geometryKey.c_str())
            );
            return false;
        }
        if(foundGeometry.value().runtimeGeometryVersion != desc.version){
            m_geometryMeshes.erase(foundGeometry);
        }
        else{
            outGeometry = &foundGeometry.value();
            return outGeometry->valid();
        }
    }

    GeometryResources createdGeometry;
    createdGeometry.geometryName = desc.geometryKey;
    createdGeometry.shaderVertexBuffer = desc.shaderVertexBuffer;
    createdGeometry.shaderIndexBuffer = desc.shaderIndexBuffer;
    createdGeometry.indexCount = desc.indexCount;
    createdGeometry.triangleCount = createdGeometry.indexCount / 3u;
    createdGeometry.dispatchGroupCount = DivideUp(createdGeometry.triangleCount, __hidden_ecs_render::s_TrianglesPerWorkgroup);
    createdGeometry.sourceVertexLayout = desc.sourceVertexLayout;
    createdGeometry.runtimeGeometry = true;
    createdGeometry.runtimeGeometryVersion = desc.version;
    if(createdGeometry.dispatchGroupCount == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: runtime geometry '{}' produced no dispatch groups"), StringConvert(desc.geometryKey.c_str()));
        return false;
    }

    auto result = m_geometryMeshes.try_emplace(desc.geometryKey, Move(createdGeometry));
    auto it = result.first;

    outGeometry = &it.value();
    return outGeometry->valid();
}

void RendererSystem::pruneRuntimeGeometryResources(){
    if(m_geometryMeshes.empty())
        return;

    for(auto it = m_geometryMeshes.begin(); it != m_geometryMeshes.end();){
        const GeometryResources& geometry = it.value();
        if(!geometry.runtimeGeometry){
            ++it;
            continue;
        }

        bool alive = false;
        for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
            if(provider && provider->containsRuntimeGeometry(geometry.geometryName, geometry.runtimeGeometryVersion)){
                alive = true;
                break;
            }
        }
        if(alive){
            ++it;
            continue;
        }

        it = m_geometryMeshes.erase(it);
    }
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
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to load material '{}'"), StringConvert(materialPath.c_str()));
        return false;
    }
    if(!loadedAsset || loadedAsset->assetType() != Material::AssetTypeName()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: asset '{}' is not a material"), StringConvert(materialPath.c_str()));
        return false;
    }

    const Material& material = static_cast<const Material&>(*loadedAsset);

    MaterialSurfaceInfo createdInfo(m_arena);
    createdInfo.materialName = materialPath;
    createdInfo.shaderVariant = material.shaderVariant().empty()
        ? AString(Core::ShaderArchive::s_DefaultVariant)
        : material.shaderVariant()
    ;
    createdInfo.valid = true;

    (void)material.findShaderForStage(Core::ShaderType::PixelStage, createdInfo.pixelShader);
    (void)material.findShaderForStage(Core::ShaderType::MeshStage, createdInfo.meshShader);

    createdInfo.parameters.reserve(material.parameters().size());
    createdInfo.parameters.insert(createdInfo.parameters.end(), material.parameters().begin(), material.parameters().end());
    createdInfo.alpha = material.alpha();
    createdInfo.transparent = material.transparent();

    auto result = m_materialSurfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    outInfo = &it.value();
    return outInfo->valid;
}

bool RendererSystem::ensureMeshShaderResources(){
    if(m_meshBindingLayout)
        return true;

    Core::BindingLayoutDesc bindingLayoutDesc;
    bindingLayoutDesc.setVisibility(Core::ShaderType::Amplification | Core::ShaderType::Mesh | Core::ShaderType::Pixel);
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_render::TransparentDrawPushConstants)));

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
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(__hidden_ecs_render::ShaderDrivenPushConstants)));

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
            __hidden_ecs_render::s_MeshEmulationVertexShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Vertex,
            "ECSRender_MeshEmulationVS"
        ))
            return false;
    }

    if(!m_emulationInputLayout){
        Core::VertexAttributeDesc attributes[6];
        attributes[0]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(0)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("POSITION")
        ;
        attributes[1]
            .setFormat(Core::Format::RGB32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 4u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("NORMAL")
        ;
        attributes[2]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 8u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("TANGENT")
        ;
        attributes[3]
            .setFormat(Core::Format::RG32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 12u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("TEXCOORD")
        ;
        attributes[4]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 16u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("COLOR")
        ;
        attributes[5]
            .setFormat(Core::Format::RGBA32_FLOAT)
            .setBufferIndex(0)
            .setOffset(sizeof(f32) * 20u)
            .setElementStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setName("POSITION1")
        ;

        Core::IDevice* device = m_graphics.getDevice();
        m_emulationInputLayout = device->createInputLayout(attributes, 6, m_emulationVertexShader.get());
        if(!m_emulationInputLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation input layout"));
            return false;
        }
    }

    return true;
}

bool RendererSystem::ensureEmulationViewResources(){
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: emulation view resources require a mesh view buffer"));
        return false;
    }

    Core::IDevice* device = m_graphics.getDevice();
    if(!m_emulationViewBindingLayout){
        Core::BindingLayoutDesc bindingLayoutDesc;
        bindingLayoutDesc.setVisibility(Core::ShaderType::Pixel);
        bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(4, 1));

        m_emulationViewBindingLayout = device->createBindingLayout(bindingLayoutDesc);
        if(!m_emulationViewBindingLayout){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding layout"));
            return false;
        }
    }

    if(m_emulationViewBindingSet)
        return true;

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));

    m_emulationViewBindingSet = device->createBindingSet(bindingSetDesc, m_emulationViewBindingLayout);
    if(!m_emulationViewBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation view binding set"));
        return false;
    }

    return true;
}

bool RendererSystem::ensureMeshBindingSet(GeometryResources& geometry){
    if(geometry.meshBindingSet)
        return true;
    if(!ensureMeshShaderResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: mesh binding set requires a material parameter buffer"));
        return false;
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, m_materialParameterBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.meshBindingSet = device->createBindingSet(bindingSetDesc, m_meshBindingLayout);
    if(!geometry.meshBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create mesh shader binding set for geometry '{}'"), StringConvert(geometry.geometryName.c_str()));
        return false;
    }

    return true;
}

bool RendererSystem::ensureComputeBindingSet(GeometryResources& geometry){
    if(geometry.computeBindingSet)
        return true;
    if(!ensureComputeEmulationResources())
        return false;
    if(!m_instanceBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires an instance buffer"));
        return false;
    }
    if(!m_meshViewBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a mesh view buffer"));
        return false;
    }
    if(!m_materialParameterBuffer){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: compute binding set requires a material parameter buffer"));
        return false;
    }

    if(!geometry.emulationVertexBuffer){
        const Name emulationVertexBufferName = DeriveName(geometry.geometryName, AStringView(":emulation_vb"));
        if(!emulationVertexBufferName){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to derive compute-emulation vertex buffer name for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }

        Core::BufferDesc emulationVertexBufferDesc;
        emulationVertexBufferDesc
            .setByteSize(static_cast<u64>(geometry.indexCount) * __hidden_ecs_render::s_EmulatedVertexStride)
            .setStructStride(__hidden_ecs_render::s_EmulatedVertexStride)
            .setCanHaveUAVs(true)
            .setIsVertexBuffer(true)
            .setDebugName(emulationVertexBufferName)
        ;
        geometry.emulationVertexBuffer = m_graphics.createBuffer(emulationVertexBufferDesc);
        if(!geometry.emulationVertexBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation vertex buffer for geometry '{}'")
                , StringConvert(geometry.geometryName.c_str())
            );
            return false;
        }
    }

    Core::BindingSetDesc bindingSetDesc;
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(0, geometry.shaderVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(1, geometry.shaderIndexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_UAV(2, geometry.emulationVertexBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(3, m_instanceBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::ConstantBuffer(4, m_meshViewBuffer.get()));
    bindingSetDesc.addItem(Core::BindingSetItem::StructuredBuffer_SRV(5, m_materialParameterBuffer.get()));

    Core::IDevice* device = m_graphics.getDevice();
    geometry.computeBindingSet = device->createBindingSet(bindingSetDesc, m_computeBindingLayout);
    if(!geometry.computeBindingSet){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation binding set for geometry '{}'")
            , StringConvert(geometry.geometryName.c_str())
        );
        return false;
    }

    return true;
}


bool RendererSystem::ensureRendererPipeline(
    const MaterialSurfaceInfo& materialInfo,
    const MaterialPipelineKey& pipelineKey,
    Core::IFramebuffer* framebuffer,
    MaterialPipelineResources*& outResources
){
    outResources = nullptr;

    if(!framebuffer)
        return false;

    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    if(!materialInfo.valid || !materialKey){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return false;
    }

    auto [it, inserted] = m_materialPipelines.try_emplace(pipelineKey);
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
            m_materialPipelines.erase(it);
    };
    auto failMaterialPipeline = [&](){
        removeFailedEntry();
        return false;
    };

    const AStringView shaderVariant = materialInfo.shaderVariant.empty()
        ? AStringView(Core::ShaderArchive::s_DefaultVariant)
        : AStringView(materialInfo.shaderVariant)
    ;

    const bool hasPixelShader = materialInfo.pixelShader.valid();
    const bool hasMeshShader = materialInfo.meshShader.valid();
    Core::ShaderHandle passPixelShader;

    switch(pass){
    case MaterialPipelinePass::Opaque:
        break;
    case MaterialPipelinePass::WireframeOverlay:
        if(!ensureShaderLoaded(
            passPixelShader,
            __hidden_ecs_render::s_WireframeOverlayPixelShaderName,
            shaderVariant,
            Core::ShaderType::Pixel,
            "ECSRender_WireframeOverlayPS"
        ))
            return failMaterialPipeline();
        break;
    case MaterialPipelinePass::AvboitOccupancy:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitOccupancyPixelShader;
        break;
    case MaterialPipelinePass::AvboitExtinction:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitExtinctionPixelShader;
        break;
    case MaterialPipelinePass::AvboitAccumulate:
        if(!ensureAvboitResources()){
            return failMaterialPipeline();
        }
        passPixelShader = m_avboitAccumulatePixelShader;
        break;
    default:
        break;
    }

    Core::IDevice* device = m_graphics.getDevice();
    const Core::RenderState renderState = __hidden_ecs_render::BuildRenderStateForPass(pass);

    auto tryBuildMeshPipeline = [&]() -> bool{
        if(!hasMeshShader)
            return false;
        if(pass == MaterialPipelinePass::Opaque && !hasPixelShader)
            return false;
        if(!ensureMeshShaderResources())
            return false;
        if(!ensureShaderLoaded(resources.meshShader, materialInfo.meshShader.name(), shaderVariant, Core::ShaderType::Mesh, "ECSRender_RendererMesh"))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else
            resources.pixelShader = passPixelShader;

        Core::MeshletPipelineDesc pipelineDesc;
        pipelineDesc.setMeshShader(resources.meshShader);
        pipelineDesc.setPixelShader(resources.pixelShader);
        pipelineDesc.setRenderState(renderState);
        pipelineDesc.addBindingLayout(m_meshBindingLayout);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            pipelineDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            pipelineDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            pipelineDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }

        resources.meshletPipeline = device->createMeshletPipeline(pipelineDesc, framebuffer->getFramebufferInfo());
        if(!resources.meshletPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), StringConvert(materialKey.c_str()));
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
        const Name& meshComputeArchiveStageName = MaterialShaderStageNames::MeshComputeArchiveStageName();
        if(!ensureShaderLoaded(
            resources.computeShader,
            materialInfo.meshShader.name(),
            shaderVariant,
            Core::ShaderType::Compute,
            "ECSRender_RendererCS",
            &meshComputeArchiveStageName
        ))
            return false;
        if(pass == MaterialPipelinePass::Opaque){
            if(!ensureShaderLoaded(resources.pixelShader, materialInfo.pixelShader.name(), shaderVariant, Core::ShaderType::Pixel, "ECSRender_RendererPS"))
                return false;
        }
        else{
            resources.pixelShader = passPixelShader;
        }
        if(!ensureEmulationViewResources())
            return false;

        Core::ComputePipelineDesc computeDesc;
        computeDesc.setComputeShader(resources.computeShader);
        computeDesc.addBindingLayout(m_computeBindingLayout);
        resources.computePipeline = device->createComputePipeline(computeDesc);
        if(!resources.computePipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            return false;
        }

        Core::GraphicsPipelineDesc emulationDesc;
        emulationDesc.setInputLayout(m_emulationInputLayout);
        emulationDesc.setVertexShader(m_emulationVertexShader);
        emulationDesc.setPixelShader(resources.pixelShader);
        emulationDesc.setRenderState(renderState);
        emulationDesc.addBindingLayout(m_emulationViewBindingLayout);
        switch(pass){
        case MaterialPipelinePass::AvboitOccupancy:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitOccupancyBindingLayout);
            break;
        case MaterialPipelinePass::AvboitExtinction:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitExtinctionBindingLayout);
            break;
        case MaterialPipelinePass::AvboitAccumulate:
            emulationDesc.addBindingLayout(m_avboitEmptyBindingLayout);
            emulationDesc.addBindingLayout(m_avboitAccumulateBindingLayout);
            break;
        case MaterialPipelinePass::Opaque:
        default:
            break;
        }
        resources.emulationPipeline = device->createGraphicsPipeline(emulationDesc, framebuffer->getFramebufferInfo());
        if(!resources.emulationPipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), StringConvert(materialKey.c_str()));
            resources.computePipeline.reset();
            return false;
        }

        resources.renderPath = RenderPath::ComputeEmulation;
        return true;
    };

    const bool meshSupported = device->queryFeatureSupport(Core::Feature::Meshlets);
    if(pass == MaterialPipelinePass::Opaque && !hasPixelShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a pixel shader"), StringConvert(materialKey.c_str()));
        return failMaterialPipeline();
    }

    if(!hasMeshShader){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' requires a mesh shader; compute emulation is derived internally from that mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    if(meshSupported && hasMeshShader){
        if(!tryBuildMeshPipeline()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device")
                , StringConvert(materialKey.c_str())
            );
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        outResources = &resources;
        return true;
    }

    if(!tryBuildComputePipeline()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader")
            , StringConvert(materialKey.c_str())
        );
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    outResources = &resources;
    return true;
}

bool RendererSystem::hasTransparentRenderers(){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> bool{
        MaterialSurfaceInfo* materialInfo = nullptr;
        if(!ensureMaterialSurfaceInfo(material, materialInfo))
            return false;
        return materialInfo && materialInfo->valid && materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        static_cast<void>(entity);
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }

    bool runtimeTransparent = false;
    for(IRuntimeGeometryProvider* provider : m_runtimeGeometryProviders){
        if(!provider)
            continue;

        provider->forEachRuntimeGeometry(
            [&](const RuntimeGeometryDesc& desc){
                if(runtimeTransparent || !desc.valid())
                    return;
                runtimeTransparent = materialIsTransparent(desc.material);
            }
        );
        if(runtimeTransparent)
            return true;
    }

    return false;
}

void RendererSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const bool meshSupported){
    auto [it, inserted] = m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
    if(!inserted){
        if(it.value() == renderPath)
            return;
        it.value() = renderPath;
    }

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
){
    return ShaderAssetLoader::EnsureLoaded(
        outShader,
        shaderName,
        variantName,
        shaderType,
        debugName,
        m_graphics,
        m_assetManager,
        m_shaderPathResolver,
        NWB_TEXT("RendererSystem"),
        archiveStageName
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

