// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/kernel/renderer_constants_private.h>

#include <core/graphics/module.h>
#include <core/graphics/vulkan/backend_context.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/scene/binding_slots.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshFrameHeapSlots{
    u32 instance = 0u;
    u32 materialTyped = 0u;
    u32 view = 0u;
    // Compute-emulation's writable generated-vertex buffer. Mesh-shader/raster draws leave this lane zero.
    u32 generatedVertex = 0u;
};

struct ShaderDrivenPushConstants{
    u32 meshletCount = 0;
    u32 instanceIndex = 0;
    u32 materialConstantByteOffset = 0;
    u32 dispatchFlags = 0;
    Float4 viewportRect = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 scissorRect = Float4(0.f, 0.f, 0.f, 0.f);
    MeshFrameHeapSlots frameHeapSlots;
};

struct TransparentDrawPushConstants{
    ShaderDrivenPushConstants mesh;
    RendererAvboitPushConstants avboit;
};

struct EmulatedVertex{
    Float4 position;
    Half4U normal;
    Half4U tangent;
    Float2U uv0;
    Half4U color;
    Float4 worldPosition;
};

struct SceneShadingGpuData{
    // xyz = camera world position, w = active light count.
    Float4 cameraPositionLightCount = Float4(0.f, 0.f, 0.f, 0.f);
};

struct SceneLightGpuData{
    // xyz = world position (point/spot), w = spot inner cone cosine.
    Float4 position = Float4(0.f, 0.f, 0.f, 1.f);
    // directional: toward-light; spot: emission axis. w = spot outer cone cosine.
    Float4 direction = Float4(0.f, 0.f, -1.f, 1.f);
    // xyz = color, w = intensity.
    Float4 colorIntensity = Float4(1.f, 1.f, 1.f, 1.f);
    // x = range, y = light type, z = shadow slot (negative = no slot), w = caustic slot (negative = no slot).
    Float4 params = Float4(0.f, 0.f, -1.f, -1.f);
    // Soft-shadow source size: x = directional angular radius (radians), y = punctual source radius (world units); z/w reserved.
    Float4 params2 = Float4(0.00465f, 0.1f, 0.f, 0.f);
};

static_assert(sizeof(MeshFrameHeapSlots) == sizeof(u32) * NWB_MESH_FRAME_HEAP_SLOT_COUNT, "Mesh frame heap-slot payload must stay a uint4");
static_assert(offsetof(MeshFrameHeapSlots, instance) == sizeof(u32) * NWB_MESH_FRAME_HEAP_SLOT_INSTANCE, "Instance heap slot must map to frame heap lane x");
static_assert(offsetof(MeshFrameHeapSlots, materialTyped) == sizeof(u32) * NWB_MESH_FRAME_HEAP_SLOT_MATERIAL_TYPED, "Material heap slot must map to frame heap lane y");
static_assert(offsetof(MeshFrameHeapSlots, view) == sizeof(u32) * NWB_MESH_FRAME_HEAP_SLOT_VIEW, "View heap slot must map to frame heap lane z");
static_assert(offsetof(MeshFrameHeapSlots, generatedVertex) == sizeof(u32) * NWB_MESH_FRAME_HEAP_SLOT_GENERATED_VERTEX, "Generated-vertex heap slot must map to frame heap lane w");
static_assert(sizeof(ShaderDrivenPushConstants) == NWB_MESH_PUSH_CONSTANT_BYTE_SIZE, "ShaderDrivenPushConstants layout must stay stable");
static_assert(offsetof(ShaderDrivenPushConstants, meshletCount) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_MESHLET_COUNT, "ShaderDrivenPushConstants dispatch.x must be meshlet count");
static_assert(offsetof(ShaderDrivenPushConstants, instanceIndex) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_INSTANCE_INDEX, "ShaderDrivenPushConstants dispatch.y must be instance index");
static_assert(offsetof(ShaderDrivenPushConstants, materialConstantByteOffset) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_MATERIAL_CONSTANT_BYTE_OFFSET, "ShaderDrivenPushConstants dispatch.z must be material constant byte offset");
static_assert(offsetof(ShaderDrivenPushConstants, dispatchFlags) == sizeof(u32) * NWB_MESH_PUSH_DISPATCH_FLAGS, "ShaderDrivenPushConstants dispatch.w must be dispatch flags");
static_assert(offsetof(ShaderDrivenPushConstants, frameHeapSlots) == sizeof(u32) * NWB_MESH_PUSH_FRAME_HEAP_SLOT_WORD_OFFSET, "ShaderDrivenPushConstants frame heap slots must follow the viewport lanes");
static_assert(sizeof(TransparentDrawPushConstants) == s_RendererAvboitTransparentDrawPushConstantSize, "TransparentDrawPushConstants layout must stay stable");
static_assert(sizeof(TransparentDrawPushConstants) <= Core::s_MaxPushConstantSize, "Transparent draw push constants must fit the portable push constant budget");
static_assert(sizeof(EmulatedVertex) == s_EmulatedVertexStride, "EmulatedVertex layout must match the mesh emulation shader");
static_assert(
    offsetof(EmulatedVertex, position) == NWB_MESH_EMULATION_VERTEX_POSITION_BYTE_OFFSET,
    "EmulatedVertex position offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, normal) == NWB_MESH_EMULATION_VERTEX_NORMAL_BYTE_OFFSET,
    "EmulatedVertex normal offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, tangent) == NWB_MESH_EMULATION_VERTEX_TANGENT_BYTE_OFFSET,
    "EmulatedVertex tangent offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, uv0) == NWB_MESH_EMULATION_VERTEX_UV0_BYTE_OFFSET,
    "EmulatedVertex uv0 offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, color) == NWB_MESH_EMULATION_VERTEX_COLOR_BYTE_OFFSET,
    "EmulatedVertex color offset must match the mesh emulation shader"
);
static_assert(
    offsetof(EmulatedVertex, worldPosition) == NWB_MESH_EMULATION_VERTEX_WORLD_POSITION_BYTE_OFFSET,
    "EmulatedVertex world-position offset must match the mesh emulation shader"
);
static_assert(alignof(EmulatedVertex) >= alignof(Float4), "EmulatedVertex must stay SIMD-aligned");
static_assert(sizeof(SceneShadingGpuData) == sizeof(f32) * NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT, "SceneShadingGpuData layout must match the shading shaders");
static_assert(alignof(SceneShadingGpuData) >= alignof(Float4), "SceneShadingGpuData must stay SIMD-aligned");
static_assert(sizeof(SceneLightGpuData) == sizeof(f32) * NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT, "SceneLightGpuData layout must match the shading shaders");
static_assert(alignof(SceneLightGpuData) >= alignof(Float4), "SceneLightGpuData must stay SIMD-aligned");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_INLINE ShaderDrivenPushConstants BuildShaderDrivenPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags
){
    ShaderDrivenPushConstants pushConstants;
    pushConstants.meshletCount = meshletCount;
    pushConstants.instanceIndex = instanceIndex;
    pushConstants.materialConstantByteOffset = materialConstantByteOffset;
    pushConstants.dispatchFlags = dispatchFlags;
    pushConstants.frameHeapSlots = frameHeapSlots;

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

NWB_INLINE TransparentDrawPushConstants BuildTransparentDrawPushConstants(
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const AvboitFrameTargets& targets,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags,
    const u32 csgContextHeapSlot = 0u
){
    TransparentDrawPushConstants pushConstants;
    pushConstants.mesh = BuildShaderDrivenPushConstants(
        meshletCount,
        instanceIndex,
        materialConstantByteOffset,
        viewportState,
        frameHeapSlots,
        dispatchFlags
    );
    pushConstants.avboit = BuildRendererAvboitPushConstants(targets);
    pushConstants.avboit.heapSlots[NWB_AVBOIT_PUSH_HEAP_SLOT_CSG_CONTEXT] = csgContextHeapSlot;
    return pushConstants;
}

NWB_INLINE void SetShaderDrivenPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags
){
    const ShaderDrivenPushConstants pushConstants = BuildShaderDrivenPushConstants(
        meshletCount,
        instanceIndex,
        materialConstantByteOffset,
        viewportState,
        frameHeapSlots,
        dispatchFlags
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

NWB_INLINE void SetTransparentDrawPushConstants(
    Core::CommandList& commandList,
    const u32 meshletCount,
    const u32 instanceIndex,
    const u32 materialConstantByteOffset,
    const Core::ViewportState& viewportState,
    const AvboitFrameTargets& targets,
    const MeshFrameHeapSlots& frameHeapSlots,
    const u32 dispatchFlags,
    const u32 csgContextHeapSlot = 0u
){
    const TransparentDrawPushConstants pushConstants = BuildTransparentDrawPushConstants(
        meshletCount,
        instanceIndex,
        materialConstantByteOffset,
        viewportState,
        targets,
        frameHeapSlots,
        dispatchFlags,
        csgContextHeapSlot
    );
    commandList.setPushConstants(&pushConstants, sizeof(pushConstants));
}

NWB_INLINE void SetEmulatedVertexAttribute(
    Core::VertexAttributeDesc& attribute,
    const Core::Format::Enum format,
    const u32 offsetByteCount,
    const char* name
){
    attribute
        .setFormat(format)
        .setBufferIndex(NWB_MESH_EMULATION_VERTEX_BUFFER_INDEX)
        .setOffset(offsetByteCount)
        .setElementStride(s_EmulatedVertexStride)
        .setName(name)
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

