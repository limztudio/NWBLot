// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "avboit.h"
#include "renderer_constants_private.h"

#include <core/graphics/module.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    const AvboitFrameTargets& targets,
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
    const AvboitFrameTargets& targets,
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

