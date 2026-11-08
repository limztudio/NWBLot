// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "binding.h"
#include "hardware_ray_tracing_policy.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace PrimitiveType{
    enum Enum : u8{
        PointList,
        LineList,
        LineStrip,
        TriangleList,
        TriangleStrip,
        TriangleFan,
        TriangleListWithAdjacency,
        TriangleStripWithAdjacency,
        PatchList,
    };
};

using SinglePassStereoState = NWB::Core::HardwareRayTracingPolicy::SinglePassStereoState;
inline b8 operator==(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs)noexcept{
    return lhs.enabled == rhs.enabled && lhs.independentViewportMask == rhs.independentViewportMask && lhs.renderTargetIndexOffset == rhs.renderTargetIndexOffset;
}

inline b8 operator!=(const SinglePassStereoState& lhs, const SinglePassStereoState& rhs)noexcept{ return !(lhs == rhs); }

struct RenderState{
    RasterState rasterState;
    BlendState blendState;
    DepthStencilState depthStencilState;
    SinglePassStereoState singlePassStereo;


public:

    constexpr RenderState& setDepthStencilState(const DepthStencilState& value)noexcept{ depthStencilState = value; return *this; }

    constexpr RenderState& setRasterState(const RasterState& value)noexcept{ rasterState = value; return *this; }

};

namespace VariableShadingRate{
    enum Enum : u8{
        e1x1,
        e1x2,
        e2x1,
        e2x2,
        e2x4,
        e4x2,
        e4x4,
    };
};

namespace ShadingRateCombiner{
    enum Enum : u8{
        Passthrough,
        Override,
        Min,
        Max,
        ApplyRelative,
    };
};

struct VariableRateShadingState{
    VariableShadingRate::Enum shadingRate = VariableShadingRate::e1x1;
    ShadingRateCombiner::Enum pipelinePrimitiveCombiner = ShadingRateCombiner::Passthrough;
    ShadingRateCombiner::Enum imageCombiner = ShadingRateCombiner::Passthrough;
    b8 enabled = false;
};
inline b8 operator==(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs)noexcept{
    return lhs.enabled == rhs.enabled && lhs.shadingRate == rhs.shadingRate && lhs.pipelinePrimitiveCombiner == rhs.pipelinePrimitiveCombiner && lhs.imageCombiner == rhs.imageCombiner;
}

inline b8 operator!=(const VariableRateShadingState& lhs, const VariableRateShadingState& rhs)noexcept{ return !(lhs == rhs); }

typedef FixedVector<BindingLayoutHandle, s_MaxBindingLayouts> BindingLayoutVector;

struct GraphicsPipelineDesc{
    InputLayoutHandle inputLayout;

    ShaderHandle vertexShader;
    ShaderHandle hullShader;
    ShaderHandle domainShader;
    ShaderHandle geometryShader;
    ShaderHandle pixelShader;

    RenderState renderState;
    BindingLayoutVector bindingLayouts;
    VariableRateShadingState shadingRateState;

    PrimitiveType::Enum primType = PrimitiveType::TriangleList;
    u32 patchControlPoints = 0;


public:
    ~GraphicsPipelineDesc()noexcept;


public:

    GraphicsPipelineDesc& setInputLayout(const InputLayoutHandle& value)noexcept;
    GraphicsPipelineDesc& setVertexShader(const ShaderHandle& value)noexcept;
    GraphicsPipelineDesc& setPixelShader(const ShaderHandle& value)noexcept;
    constexpr GraphicsPipelineDesc& setRenderState(const RenderState& value)noexcept{ renderState = value; return *this; }

    GraphicsPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<GraphicsPipeline> GraphicsPipelineHandle;

struct ComputePipelineDesc{
    ShaderHandle computeShader;

    BindingLayoutVector bindingLayouts;


public:
    ~ComputePipelineDesc()noexcept;


public:
    ComputePipelineDesc& setComputeShader(const ShaderHandle& value)noexcept;
    ComputePipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<ComputePipeline> ComputePipelineHandle;

struct MeshletPipelineDesc{
    BindingLayoutVector bindingLayouts;

    ShaderHandle amplificationShader;
    ShaderHandle meshShader;
    ShaderHandle pixelShader;

    RenderState renderState;

    PrimitiveType::Enum primType = PrimitiveType::TriangleList;


public:
    ~MeshletPipelineDesc()noexcept;


public:
    MeshletPipelineDesc& setMeshShader(const ShaderHandle& value)noexcept;
    MeshletPipelineDesc& setPixelShader(const ShaderHandle& value)noexcept;
    constexpr MeshletPipelineDesc& setRenderState(const RenderState& value)noexcept{ renderState = value; return *this; }
    MeshletPipelineDesc& addBindingLayout(const BindingLayoutHandle& layout);
};

typedef GraphicsBackend::Handle<MeshletPipeline> MeshletPipelineHandle;

struct RayTracingPipelineShaderDesc{
    ShaderHandle shader;
    BindingLayoutHandle bindingLayout;
    // Borrowed until pipeline creation; retained descriptions refer to pipeline-owned group metadata.
    AStringView exportName;


public:
    ~RayTracingPipelineShaderDesc()noexcept;


public:
    RayTracingPipelineShaderDesc& setShader(const ShaderHandle& value)noexcept;
    RayTracingPipelineShaderDesc& setBindingLayout(const BindingLayoutHandle& value)noexcept;
    RayTracingPipelineShaderDesc& setExportName(AStringView value)noexcept{ exportName = value; return *this; }
};

struct RayTracingPipelineHitGroupDesc{
    ShaderHandle closestHitShader;
    ShaderHandle anyHitShader;
    ShaderHandle intersectionShader;
    BindingLayoutHandle bindingLayout;
    // Borrowed until pipeline creation; retained descriptions refer to pipeline-owned group metadata.
    AStringView exportName;
    b8 isProceduralPrimitive = false;


public:
    ~RayTracingPipelineHitGroupDesc()noexcept;


public:
    RayTracingPipelineHitGroupDesc& setClosestHitShader(const ShaderHandle& value)noexcept;
    RayTracingPipelineHitGroupDesc& setBindingLayout(const BindingLayoutHandle& value)noexcept;
    RayTracingPipelineHitGroupDesc& setExportName(AStringView value)noexcept{ exportName = value; return *this; }

};

struct RayTracingPipelineDesc{
    GraphicsVector<RayTracingPipelineShaderDesc> shaders;
    GraphicsVector<RayTracingPipelineHitGroupDesc> hitGroups;
    BindingLayoutVector globalBindingLayouts;
    u32 maxPayloadSize = 0;
    u32 maxAttributeSize = sizeof(f32) * 2; // typical case: float2 uv;
    u32 maxRecursionDepth = 1;
    b8 allowOpacityMicromaps = false;
    b8 allowClusterAccelerationStructures = false;
    b8 allowSpheres = false;
    b8 allowLinearSweptSpheres = false;


public:
    explicit RayTracingPipelineDesc(GraphicsArena& arena)noexcept
        : shaders(arena)
        , hitGroups(arena)
    {}
    ~RayTracingPipelineDesc()noexcept;


public:
    RayTracingPipelineDesc& addShader(const RayTracingPipelineShaderDesc& value);
    RayTracingPipelineDesc& addHitGroup(const RayTracingPipelineHitGroupDesc& value);
    RayTracingPipelineDesc& addBindingLayout(const BindingLayoutHandle& value);
    constexpr RayTracingPipelineDesc& setMaxPayloadSize(u32 value)noexcept{ maxPayloadSize = value; return *this; }

    constexpr RayTracingPipelineDesc& setMaxRecursionDepth(u32 value)noexcept{ maxRecursionDepth = value; return *this; }

};

typedef GraphicsBackend::Handle<RayTracingPipeline> RayTracingPipelineHandle;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

