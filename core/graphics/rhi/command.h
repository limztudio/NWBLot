#pragma once


#include "pipeline.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


typedef GraphicsBackend::Handle<EventQuery> EventQueryHandle;
typedef GraphicsBackend::Handle<TimerQuery> TimerQueryHandle;

struct VertexBufferBinding{
    Buffer* buffer = nullptr;
    u64 offset = 0;
    u32 slot = 0;

    constexpr VertexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr VertexBufferBinding& setSlot(u32 value){ slot = value; return *this; }
    constexpr VertexBufferBinding& setOffset(u64 value){ offset = value; return *this; }
};
inline bool operator==(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.slot == rhs.slot;
}
inline bool operator!=(const VertexBufferBinding& lhs, const VertexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

struct IndexBufferBinding{
    Buffer* buffer = nullptr;
    u32 offset = 0;
    Format::Enum format = Format::UNKNOWN;

    constexpr IndexBufferBinding& setBuffer(Buffer* value){ buffer = value; return *this; }
    constexpr IndexBufferBinding& setFormat(Format::Enum value){ format = value; return *this; }
    constexpr IndexBufferBinding& setOffset(u32 value){ offset = value; return *this; }
};
inline bool operator==(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{
    return lhs.buffer == rhs.buffer && lhs.offset == rhs.offset && lhs.format == rhs.format;
}
inline bool operator!=(const IndexBufferBinding& lhs, const IndexBufferBinding& rhs)noexcept{ return !(lhs == rhs); }

typedef FixedVector<BindingSet*, s_MaxBindingLayouts> BindingSetVector;

struct GraphicsState{
    GraphicsPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    VariableRateShadingState shadingRateState;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    FixedVector<VertexBufferBinding, s_MaxVertexAttributes> vertexBuffers;
    IndexBufferBinding indexBuffer;

    Buffer* indirectParams = nullptr;

    constexpr GraphicsState& setPipeline(GraphicsPipeline* value){ pipeline = value; return *this; }
    constexpr GraphicsState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr GraphicsState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr GraphicsState& setShadingRateState(const VariableRateShadingState& value){ shadingRateState = value; return *this; }
    constexpr GraphicsState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    constexpr GraphicsState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
    GraphicsState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    GraphicsState& addVertexBuffer(const VertexBufferBinding& value){ vertexBuffers.push_back(value); return *this; }
    constexpr GraphicsState& setIndexBuffer(const IndexBufferBinding& value){ indexBuffer = value; return *this; }
    constexpr GraphicsState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
};

struct DrawArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndirectArguments{
    u32 vertexCount = 0;
    u32 instanceCount = 1;
    u32 startVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndirectArguments& setVertexCount(u32 value){ vertexCount = value; return *this; }
    constexpr DrawIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndirectArguments& setStartVertexLocation(u32 value){ startVertexLocation = value; return *this; }
    constexpr DrawIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct DrawIndexedIndirectArguments{
    u32 indexCount = 0;
    u32 instanceCount = 1;
    u32 startIndexLocation = 0;
    i32  baseVertexLocation = 0;
    u32 startInstanceLocation = 0;

    constexpr DrawIndexedIndirectArguments& setIndexCount(u32 value){ indexCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setInstanceCount(u32 value){ instanceCount = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartIndexLocation(u32 value){ startIndexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setBaseVertexLocation(i32 value){ baseVertexLocation = value; return *this; }
    constexpr DrawIndexedIndirectArguments& setStartInstanceLocation(u32 value){ startInstanceLocation = value; return *this; }
};

struct ComputeState{
    ComputePipeline* pipeline = nullptr;

    BindingSetVector bindings;

    Buffer* indirectParams = nullptr;

    constexpr ComputeState& setPipeline(ComputePipeline* value){ pipeline = value; return *this; }
    ComputeState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    constexpr ComputeState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
};

struct DispatchIndirectArguments{
    u32 groupsX = 1;
    u32 groupsY = 1;
    u32 groupsZ = 1;

    constexpr DispatchIndirectArguments& setGroupsX(u32 value){ groupsX = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsY(u32 value){ groupsY = value; return *this; }
    constexpr DispatchIndirectArguments& setGroupsZ(u32 value){ groupsZ = value; return *this; }
    constexpr DispatchIndirectArguments& setGroups2D(u32 x, u32 y){ groupsX = x; groupsY = y; return *this; }
    constexpr DispatchIndirectArguments& setGroups3D(u32 x, u32 y, u32 z){ groupsX = x; groupsY = y; groupsZ = z; return *this; }
};

struct MeshletState{
    MeshletPipeline* pipeline = nullptr;
    Framebuffer* framebuffer = nullptr;
    ViewportState viewport;
    Color blendConstantColor{};
    u8 dynamicStencilRefValue = 0;

    BindingSetVector bindings;

    Buffer* indirectParams = nullptr;

    constexpr MeshletState& setPipeline(MeshletPipeline* value){ pipeline = value; return *this; }
    constexpr MeshletState& setFramebuffer(Framebuffer* value){ framebuffer = value; return *this; }
    constexpr MeshletState& setViewport(const ViewportState& value){ viewport = value; return *this; }
    constexpr MeshletState& setBlendColor(const Color& value){ blendConstantColor = value; return *this; }
    MeshletState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
    constexpr MeshletState& setIndirectParams(Buffer* value){ indirectParams = value; return *this; }
    constexpr MeshletState& setDynamicStencilRefValue(u8 value){ dynamicStencilRefValue = value; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Ray Tracing


struct RayTracingPipelineShaderDesc{
    ShaderHandle shader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;

    explicit RayTracingPipelineShaderDesc(GraphicsArena& arena);
    ~RayTracingPipelineShaderDesc();

    RayTracingPipelineShaderDesc& setShader(const ShaderHandle& value);
    RayTracingPipelineShaderDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineShaderDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
};

struct RayTracingPipelineHitGroupDesc{
    ShaderHandle closestHitShader;
    ShaderHandle anyHitShader;
    ShaderHandle intersectionShader;
    BindingLayoutHandle bindingLayout;
    GraphicsString exportName;
    bool isProceduralPrimitive = false;

    explicit RayTracingPipelineHitGroupDesc(GraphicsArena& arena);
    ~RayTracingPipelineHitGroupDesc();

    RayTracingPipelineHitGroupDesc& setClosestHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setAnyHitShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setIntersectionShader(const ShaderHandle& value);
    RayTracingPipelineHitGroupDesc& setBindingLayout(const BindingLayoutHandle& value);
    RayTracingPipelineHitGroupDesc& setExportName(AStringView value){ exportName.assign(value); return *this; }
    constexpr RayTracingPipelineHitGroupDesc& setIsProceduralPrimitive(bool value){ isProceduralPrimitive = value; return *this; }
};

struct RayTracingPipelineDesc{
    GraphicsVector<RayTracingPipelineShaderDesc> shaders;
    GraphicsVector<RayTracingPipelineHitGroupDesc> hitGroups;
    BindingLayoutVector globalBindingLayouts;
    u32 maxPayloadSize = 0;
    u32 maxAttributeSize = sizeof(f32) * 2; // typical case: float2 uv;
    u32 maxRecursionDepth = 1;
    i32 hlslExtensionsUAV = -1;
    bool allowOpacityMicromaps = false;
    bool allowSpheres = false;
    bool allowLinearSweptSpheres = false;

    explicit RayTracingPipelineDesc(GraphicsArena& arena)
        : shaders(arena)
        , hitGroups(arena)
    {}
    ~RayTracingPipelineDesc();

    RayTracingPipelineDesc& addShader(const RayTracingPipelineShaderDesc& value);
    RayTracingPipelineDesc& addHitGroup(const RayTracingPipelineHitGroupDesc& value);
    RayTracingPipelineDesc& addBindingLayout(const BindingLayoutHandle& value);
    constexpr RayTracingPipelineDesc& setMaxPayloadSize(u32 value){ maxPayloadSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxAttributeSize(u32 value){ maxAttributeSize = value; return *this; }
    constexpr RayTracingPipelineDesc& setMaxRecursionDepth(u32 value){ maxRecursionDepth = value; return *this; }
    constexpr RayTracingPipelineDesc& setHlslExtensionsUAV(i32 value){ hlslExtensionsUAV = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowOpacityMicromaps(bool value){ allowOpacityMicromaps = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowSpheres(bool value){ allowSpheres = value; return *this; }
    constexpr RayTracingPipelineDesc& setAllowLinearSweptSpheres(bool value){ allowLinearSweptSpheres = value; return *this; }
};

typedef GraphicsBackend::Handle<RayTracingShaderTable> RayTracingShaderTableHandle;
typedef GraphicsBackend::Handle<RayTracingPipeline> RayTracingPipelineHandle;

struct RayTracingState{
    RayTracingShaderTable* shaderTable = nullptr;

    BindingSetVector bindings;

    constexpr RayTracingState& setShaderTable(RayTracingShaderTable* value){ shaderTable = value; return *this; }
    RayTracingState& addBindingSet(BindingSet* value){ bindings.push_back(value); return *this; }
};

struct RayTracingDispatchRaysArguments{
    u32 width = 1;
    u32 height = 1;
    u32 depth = 1;

    constexpr RayTracingDispatchRaysArguments& setWidth(u32 value){ width = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setHeight(u32 value){ height = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDepth(u32 value){ depth = value; return *this; }
    constexpr RayTracingDispatchRaysArguments& setDimensions(u32 w, u32 h = 1, u32 d = 1){ width = w; height = h; depth = d; return *this; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

