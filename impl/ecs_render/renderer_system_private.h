// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "renderer_system.h"

#include "renderer_avboit.h"

#include <core/assets/asset_manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <impl/assets_material/material_asset.h>
#include <impl/assets_material/material_shader_stage_names.h>
#include <impl/assets_shader/shader_asset.h>
#include <impl/assets_shader/shader_asset_loader.h>
#include <impl/ecs_camera/camera.h>
#include <impl/ecs_geometry/ecs_geometry.h>
#include <impl/ecs_lighting/lighting.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
inline constexpr u32 s_EmulatedVertexStride = sizeof(f32) * 24u;
inline constexpr u32 s_MeshDispatchFlagScissorCull = 1u << 0u;
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

struct MeshViewGpuData{
    Float4 worldToClip[4] = {
        Float4(1.f, 0.f, 0.f, 0.f),
        Float4(0.f, 1.f, 0.f, 0.f),
        Float4(0.f, 0.f, 1.f, 0.f),
        Float4(0.f, 0.f, 0.f, 1.f),
    };
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
static_assert(sizeof(MeshViewGpuData) == sizeof(f32) * 16u, "MeshViewGpuData layout must match the mesh shaders");
static_assert(alignof(MeshViewGpuData) >= alignof(Float4), "MeshViewGpuData must stay SIMD-aligned");
static_assert(sizeof(SceneShadingGpuData) == sizeof(f32) * 12u, "SceneShadingGpuData layout must match the shading shaders");
static_assert(alignof(SceneShadingGpuData) >= alignof(Float4), "SceneShadingGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_MeshEmulationVertexShaderName("engine/graphics/mesh/emulation_vs");
inline constexpr Name s_InstanceBufferName("ecs_render/instance_data");
inline constexpr Name s_MaterialTypedBufferName("ecs_render/material_typed_data");
inline constexpr Name s_MeshViewBufferName("ecs_render/mesh_view_data");
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

inline void AddGeometrySourceBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc){
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(0, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(1, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(2, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(3, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(4, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(5, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(6, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(7, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(8, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::RawBuffer_SRV(9, 1));
}

inline void AddGeometryFrameBindingLayoutItems(Core::BindingLayoutDesc& bindingLayoutDesc){
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(10, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::ConstantBuffer(11, 1));
    bindingLayoutDesc.addItem(Core::BindingLayoutItem::StructuredBuffer_SRV(12, 1));
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

inline Core::RenderState BuildGeometryRenderState(){
    Core::RenderState renderState;
    renderState.depthStencilState
        .enableDepthTest()
        .enableDepthWrite()
        .setDepthFunc(Core::ComparisonFunc::LessOrEqual)
    ;
    renderState.rasterState.enableDepthClip();
    return renderState;
}

inline Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass){
    switch(pass){
    case MaterialPipelinePass::Opaque:
        return BuildGeometryRenderState();
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return BuildRendererAvboitVoxelRenderState();
    case MaterialPipelinePass::AvboitAccumulate:
        return BuildRendererAvboitAccumulateRenderState();
    default:
        return BuildGeometryRenderState();
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

inline void ApplyDirectionalLightSceneShadingState(
    SceneShadingGpuData& state,
    const NWB::Impl::SceneDirectionalLight& light
){
    state.directionalLightDirection = light.direction;
    state.directionalLightColorIntensity = light.colorIntensity;
}

inline void StoreProjectedViewColumn(
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

inline void StoreWorldToClipMatrix(
    Float4 (&outWorldToClip)[4],
    const NWB::Impl::SceneViewBasis& basis,
    const Float4& projectionParams
){
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

inline MeshViewGpuData ResolveMeshViewState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    MeshViewGpuData state;
    const NWB::Impl::SceneViewBasis defaultBasis = NWB::Impl::BuildDefaultSceneViewBasis();

    const NWB::Impl::SceneCameraView cameraView = NWB::Impl::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreWorldToClipMatrix(
            state.worldToClip,
            NWB::Impl::BuildSceneViewBasis(*cameraView.transform),
            cameraView.projectionData.projectionParams
        );
    }
    else{
        StoreWorldToClipMatrix(
            state.worldToClip,
            defaultBasis,
            NWB::Impl::BuildDefaultCameraProjectionParams(fallbackAspectRatio)
        );
    }

    return state;
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    SceneShadingGpuData state;
    const NWB::Impl::SceneViewBasis defaultBasis = NWB::Impl::BuildDefaultSceneViewBasis();

    const NWB::Impl::SceneCameraView cameraView = NWB::Impl::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreFloat(VectorSetW(LoadFloat(cameraView.transform->position), 1.0f), &state.cameraPosition);
    }
    else{
        state.cameraPosition = Float4(
            defaultBasis.positionDepthBias.x,
            defaultBasis.positionDepthBias.y,
            defaultBasis.positionDepthBias.z,
            1.0f
        );
    }

    ECSRenderDetail::ApplyDirectionalLightSceneShadingState(
        state,
        NWB::Impl::ResolveSceneDirectionalLight(world, defaultBasis)
    );

    return state;
}

inline ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.meshletCount = meshletCount;
    pushConstants.instanceIndex = instanceIndex;

    if(viewportState.viewports.empty())
        return pushConstants;

    const Core::Viewport& viewport = viewportState.viewports[0];
    pushConstants.dispatchFlags = s_MeshDispatchFlagScissorCull;
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
    const RendererSystem::AvboitFrameTargets& targets
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(meshletCount, instanceIndex, viewportState);
    pushConstants.avboit = BuildRendererAvboitPushConstants(targets);
    return pushConstants;
}

inline void SetShaderDrivenPushConstants(
    Core::ICommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState
){
    const ShaderDrivenPushConstants pushConstants =
        BuildShaderDrivenPushConstants(meshletCount, instanceIndex, viewportState)
    ;
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

inline void SetTransparentDrawPushConstants(
    Core::ICommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets
){
    const TransparentDrawPushConstants pushConstants =
        BuildTransparentDrawPushConstants(meshletCount, instanceIndex, viewportState, targets)
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

