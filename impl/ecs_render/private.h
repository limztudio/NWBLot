// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "system.h"

#include "avboit.h"
#include "material_typed_private.h"
#include "mesh_view_private.h"

#include <core/assets/manager.h>
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/module.h>
#include <core/graphics/shader_archive.h>
#include <impl/assets_mesh/asset.h>
#include <impl/assets/graphics/avboit/binding_slots.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/deferred/binding_slots.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_material/shader_stage_names.h>
#include <impl/assets_shader/asset.h>
#include <impl/assets_shader/loader.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Core::Color s_ClearColor = Core::Color(0.07f, 0.09f, 0.13f, 1.f);
inline constexpr u32 s_EmulatedVertexStride = sizeof(f32) * NWB_MESH_EMULATION_VERTEX_FLOAT_COUNT;
inline constexpr u32 s_MeshDispatchFlagScissorCull = NWB_MESH_DISPATCH_FLAG_SCISSOR_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletFrustumCull = NWB_MESH_DISPATCH_FLAG_MESHLET_FRUSTUM_CULL;
inline constexpr u32 s_MeshDispatchFlagMeshletConeCull = NWB_MESH_DISPATCH_FLAG_MESHLET_CONE_CULL;
inline constexpr Core::TextureSubresourceSet s_FramebufferSubresources = Core::TextureSubresourceSet(0, 1, 0, 1);


struct ShaderDrivenPushConstants{
    u32 meshletCount = 0;
    u32 instanceIndex = 0;
    u32 materialConstantByteOffset = 0;
    u32 dispatchFlags = 0;
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

static_assert(sizeof(ShaderDrivenPushConstants) == NWB_MESH_PUSH_CONSTANT_BYTE_SIZE, "ShaderDrivenPushConstants layout must stay stable");
static_assert(offsetof(ShaderDrivenPushConstants, meshletCount) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_MESHLET_COUNT, "ShaderDrivenPushConstants dispatch.x must be meshlet count");
static_assert(offsetof(ShaderDrivenPushConstants, instanceIndex) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_INSTANCE_INDEX, "ShaderDrivenPushConstants dispatch.y must be instance index");
static_assert(offsetof(ShaderDrivenPushConstants, materialConstantByteOffset) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_MATERIAL_CONSTANT_BYTE_OFFSET, "ShaderDrivenPushConstants dispatch.z must be material constant byte offset");
static_assert(offsetof(ShaderDrivenPushConstants, dispatchFlags) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_FLAGS, "ShaderDrivenPushConstants dispatch.w must be dispatch flags");
static_assert(sizeof(TransparentDrawPushConstants) == s_RendererAvboitTransparentDrawPushConstantSize, "TransparentDrawPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize, "Transparent draw push constants must fit the portable push constant budget");
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");
static_assert(
    offsetof(EmulatedVertex, position) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_POSITION_FLOAT_OFFSET,
    "EmulatedVertex position offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, normal) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_NORMAL_FLOAT_OFFSET,
    "EmulatedVertex normal offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, tangent) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_TANGENT_FLOAT_OFFSET,
    "EmulatedVertex tangent offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, uv0) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_UV0_FLOAT_OFFSET,
    "EmulatedVertex uv0 offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, color) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_COLOR_FLOAT_OFFSET,
    "EmulatedVertex color offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, worldPosition) == sizeof(f32) * NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_FLOAT_OFFSET,
    "EmulatedVertex world-position offset must match the mesh emulation shader"
);
static_assert(alignof(EmulatedVertex) >= alignof(Float4), "EmulatedVertex must stay SIMD-aligned");
static_assert(sizeof(SceneShadingGpuData) == sizeof(f32) * NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT, "SceneShadingGpuData layout must match the shading shaders");
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
    Core::Device& device,
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
    Core::Device& device,
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

    NWB_LOGGER_ERROR(failureMessage);
    return false;
}

inline bool CreatePointClampSampler(Core::Device& device, Core::SamplerHandle& sampler, const tchar* failureMessage){
    return CreateClampSampler(device, sampler, false, failureMessage);
}

inline Core::Format::Enum SelectGBufferAlbedoFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferVectorFormat(Core::Device& device){
    constexpr Core::Format::Enum candidates[] = {
        Core::Format::RGBA16_FLOAT,
    };
    constexpr Core::FormatSupport::Mask requiredSupport = Core::FormatSupport::Texture | Core::FormatSupport::RenderTarget;

    return SelectSupportedFormat(device, candidates, requiredSupport);
}

inline Core::Format::Enum SelectGBufferDepthFormat(Core::Device& device){
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

inline f32 ResolveExtentAspectRatio(const u32 width, const u32 height){
    if(width != 0u && height != 0u)
        return static_cast<f32>(width) / static_cast<f32>(height);

    return 1.0f;
}

inline f32 ResolveFramebufferAspectRatio(const Core::FramebufferInfoEx& framebufferInfo){
    return ResolveExtentAspectRatio(framebufferInfo.width, framebufferInfo.height);
}

inline SceneShadingGpuData ResolveSceneShadingState(Core::ECS::World& world, const f32 fallbackAspectRatio){
    SceneShadingGpuData state;
    const NWB::Impl::Scene::SceneViewBasis defaultBasis = NWB::Impl::Scene::BuildDefaultSceneViewBasis();

    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world, fallbackAspectRatio);
    if(cameraView.valid()){
        StoreFloat(VectorSetW(LoadFloat(cameraView.transform->position), 1.0f), &state.cameraPosition);
    }
    else
        StoreFloat(VectorSetW(LoadFloat(defaultBasis.positionDepthBias), 1.0f), &state.cameraPosition);

    const NWB::Impl::Scene::SceneDirectionalLight light = NWB::Impl::Scene::ResolveSceneDirectionalLight(world, defaultBasis);
    state.directionalLightDirection = light.direction;
    state.directionalLightColorIntensity = light.colorIntensity;

    return state;
}

inline ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const u32 dispatchFlags
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.meshletCount = meshletCount;
    pushConstants.instanceIndex = instanceIndex;
    pushConstants.materialConstantByteOffset = materialConstantByteOffset;
    pushConstants.dispatchFlags = dispatchFlags;

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
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const u32 dispatchFlags
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(meshletCount, instanceIndex, materialConstantByteOffset, viewportState, dispatchFlags);
    pushConstants.avboit = BuildRendererAvboitPushConstants(targets);
    return pushConstants;
}

inline void SetShaderDrivenPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const u32 dispatchFlags
){
    const ShaderDrivenPushConstants pushConstants = BuildShaderDrivenPushConstants(meshletCount, instanceIndex, materialConstantByteOffset, viewportState, dispatchFlags);
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

inline void SetTransparentDrawPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const RendererSystem::AvboitFrameTargets& targets,
    const u32 dispatchFlags
){
    const TransparentDrawPushConstants pushConstants = BuildTransparentDrawPushConstants(meshletCount, instanceIndex, materialConstantByteOffset, viewportState, targets, dispatchFlags);
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
        .setBufferIndex(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX)
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

