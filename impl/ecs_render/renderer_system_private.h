// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_system.h"

#include "renderer_avboit.h"
#include "renderer_system_mesh_view_private.h"

#include <core/assets/asset_manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_mesh/mesh_asset.h>
#include <impl/assets_material/material_asset.h>
#include <impl/assets_material/material_shader_stage_names.h>
#include <impl/assets_shader/shader_asset.h>
#include <impl/assets_shader/shader_asset_loader.h>
#include <impl/ecs_camera/camera.h>
#include <impl/ecs_mesh/ecs_mesh.h>
#include <impl/ecs_lighting/lighting.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
inline constexpr u32 s_EmulatedVertexStride = sizeof(f32) * 24u;
inline constexpr u32 s_MeshDispatchFlagScissorCull = 1u << 0u;
inline constexpr u32 s_MeshDispatchFlagMeshletFrustumCull = 1u << 1u;
inline constexpr u32 s_MeshDispatchFlagMeshletConeCull = 1u << 2u;
inline constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


struct ShaderDrivenPushConstants{
    u32 meshletCount = 0;
    u32 dispatchFlags = 0;
    u32 instanceIndex = 0;
    u32 reserved0 = 0;
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

struct SceneShadingGpuData{
    Float4 directionalLightDirection = Float4(0.f, 0.f, -1.f, 0.f);
    Float4 directionalLightColorIntensity = Float4(1.f, 1.f, 1.f, 1.f);
    Float4 cameraPosition = Float4(0.f, 0.f, 0.f, 1.f);
};

struct MaterialTypedByteBlock{
    u32 byteOffset = 0;
    u32 byteCount = 0;
};

static_assert(sizeof(ShaderDrivenPushConstants) == 48, "ShaderDrivenPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) == s_RendererAvboitTransparentDrawPushConstantSize, "TransparentDrawPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize, "Transparent draw push constants must fit the portable push constant budget");
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");
static_assert(alignof(EmulatedVertex) >= alignof(Float4), "EmulatedVertex must stay SIMD-aligned");
static_assert(sizeof(SceneShadingGpuData) == sizeof(f32) * 12u, "SceneShadingGpuData layout must match the shading shaders");
static_assert(alignof(SceneShadingGpuData) >= alignof(Float4), "SceneShadingGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshEmulationVertexShaderName("engine/graphics/mesh/emulation_vs");
inline constexpr Name s_InstanceBufferName("ecs_render/instance_data");
inline constexpr Name s_MaterialTypedBufferName("ecs_render/material_typed_data");
inline constexpr Name s_SceneShadingBufferName("ecs_render/scene_shading_data");
inline constexpr Name s_DeferredCompositeVertexShaderName("engine/graphics/deferred/composite_vs");
inline constexpr Name s_DeferredLightingPixelShaderName("engine/graphics/deferred/lighting_ps");
inline constexpr Name s_DeferredCompositePixelShaderName("engine/graphics/deferred/composite_ps");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize N>
inline Core::Format::Enum SelectSupportedFormat(
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

inline bool CreateClampSampler(
    Core::IDevice& device,
    Core::SamplerHandle& sampler,
    const bool linearFiltering,
    const tchar* failureMessage
){
    if(sampler)
        return true;

    Core::SamplerDesc samplerDesc;
    samplerDesc
        .setAllFilters(linearFiltering)
        .setAllAddressModes(Core::SamplerAddressMode::Clamp)
    ;
    sampler = device.createSampler(samplerDesc);
    if(sampler)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{}"), failureMessage);
    return false;
}

inline bool CreatePointClampSampler(Core::IDevice& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    return CreateClampSampler(device, sampler, false, failureMessage);
}

inline Core::Format::Enum SelectGBufferAlbedoFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferVectorFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport =
        Core::FormatSupport::Texture
        | Core::FormatSupport::RenderTarget
    ;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferDepthFormat(Core::IDevice& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::D32,
        Core::Format::D24S8,
        Core::Format::D16,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::DepthStencil;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::RenderState BuildMeshRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .enableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip();
    return renderState;
}

inline Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass, const bool twoSided){
    auto applyTwoSided = [&](Core::RenderState renderState){
        if(twoSided)
            renderState.rasterState.setCullNone();
        return renderState;
    };

    switch(pass){
    case MaterialPipelinePass::Opaque:
        return applyTwoSided(BuildMeshRenderState());
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return applyTwoSided(BuildRendererAvboitVoxelRenderState());
    case MaterialPipelinePass::AvboitAccumulate:
        return applyTwoSided(BuildRendererAvboitAccumulateRenderState());
    default:
        return applyTwoSided(BuildMeshRenderState());
    }
}

inline Core::RenderState BuildCompositeRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState.disableDepthTest().disableDepthWrite();
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}

inline usize NextGrowingCapacity(const usize currentCapacity, const usize requiredCapacity){
    usize capacity = Max<usize>(currentCapacity, 1u);
    while(capacity < requiredCapacity){
        if(capacity > (Limit<usize>::s_Max / 2u))
            return requiredCapacity;
        capacity *= 2u;
    }
    return capacity;
}

inline InstanceGpuData BuildInstanceGpuData(
    const NWB::Impl::TransformComponent* transform,
    const u32 materialTypedByteOffset,
    const u32 materialTypedByteCount
){
    InstanceGpuData data;
    data.materialTypedBytes.x = materialTypedByteOffset;
    data.materialTypedBytes.y = materialTypedByteCount;
    if(!transform)
        return data;

    data.rotation = transform->rotation;
    data.translation = transform->position;
    data.scale = transform->scale;
    return data;
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    SceneShadingGpuData state;
    const NWB::Impl::SceneViewBasis defaultBasis = NWB::Impl::BuildDefaultSceneViewBasis();

    const NWB::Impl::SceneCameraView cameraView = NWB::Impl::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreFloat(VectorSetW(LoadFloat(cameraView.transform->position), 1.0f), &state.cameraPosition);
    }
    else
        StoreFloat(VectorSetW(LoadFloat(defaultBasis.positionDepthBias), 1.0f), &state.cameraPosition);

    const NWB::Impl::SceneDirectionalLight light = NWB::Impl::ResolveSceneDirectionalLight(world, defaultBasis);
    state.directionalLightDirection = light.direction;
    state.directionalLightColorIntensity = light.colorIntensity;

    return state;
}

inline ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState,
    const u32 dispatchFlags
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.meshletCount = meshletCount;
    pushConstants.dispatchFlags = dispatchFlags;
    pushConstants.instanceIndex = instanceIndex;

    if(viewportState.viewports.empty())
        return pushConstants;

    const Core::Viewport& viewport = viewportState.viewports[0];
    pushConstants.dispatchFlags |= s_MeshDispatchFlagScissorCull;
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

inline TransparentDrawPushConstants BuildTransparentDrawPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const u32 dispatchFlags
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(meshletCount, instanceIndex, viewportState, dispatchFlags);
    pushConstants.avboit = BuildRendererAvboitPushConstants(targets);
    return pushConstants;
}

inline void SetShaderDrivenPushConstants(
    Core::ICommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState,
    const u32 dispatchFlags
){
    const ShaderDrivenPushConstants pushConstants =
        BuildShaderDrivenPushConstants(meshletCount, instanceIndex, viewportState, dispatchFlags)
    ;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

inline void SetTransparentDrawPushConstants(
    Core::ICommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const u32 dispatchFlags
){
    const TransparentDrawPushConstants pushConstants =
        BuildTransparentDrawPushConstants(meshletCount, instanceIndex, viewportState, targets, dispatchFlags)
    ;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

inline void SetEmulatedVertexAttribute(
    Core::VertexAttributeDesc& attribute,
    const Core::Format::Enum format,
    const u32 offsetFloatCount,
    const char* name
){
    attribute
        .setFormat(format)
        .setBufferIndex(0)
        .setOffset(sizeof(f32) * offsetFloatCount)
        .setElementStride(s_EmulatedVertexStride)
        .setName(name)
    ;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

