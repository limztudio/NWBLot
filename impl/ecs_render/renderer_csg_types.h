// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_mesh_runtime/mesh.h>

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverRangeGpuData{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    u32 padding0 = 0u;
};

inline constexpr i32 s_CsgBoundsValidFlag = 1 << 0;
inline constexpr i32 s_CsgBoundsFiniteFlag = 1 << 1;

struct CsgReceiverCpuBounds{
    Float3Int minBounds = Float3Int(0.f, 0.f, 0.f, 0);
    Float3Int maxBounds = Float3Int(0.f, 0.f, 0.f, 0);

    [[nodiscard]] bool valid()const noexcept{ return (minBounds.w & s_CsgBoundsValidFlag) != 0; }
};

[[nodiscard]] inline Float34 MakeIdentityCsgMatrix(){
    Float34 matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    return matrix;
}

struct CsgCutterGpuData{
    u32 shapeType = 0u;
    u32 operation = 0u;
    u32 parameterByteOffset = 0u;
    u32 parameterByteSize = 0u;
    Float34 worldToShape = MakeIdentityCsgMatrix();
    Float34 shapeToWorld = MakeIdentityCsgMatrix();
    Float4 parameter0 = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 parameter1 = Float4(0.f, 0.f, 0.f, 0.f);
    f32 worldToShapeScaleBound = 1.f;
    f32 padding0 = 0.f;
    f32 padding1 = 0.f;
    f32 padding2 = 0.f;
};

static_assert(sizeof(CsgReceiverRangeGpuData) == sizeof(u32) * 4u, "CsgReceiverRangeGpuData layout must match the CSG shader");
static_assert(sizeof(CsgCutterGpuData) == sizeof(u32) * 4u + sizeof(Float34) * 2u + sizeof(Float4) * 3u, "CsgCutterGpuData layout must match the CSG shader");
static_assert(alignof(CsgCutterGpuData) >= alignof(Float4), "CsgCutterGpuData must stay SIMD-aligned");
static_assert(alignof(CsgReceiverCpuBounds) >= alignof(Float4), "CsgReceiverCpuBounds must stay SIMD-friendly");
static_assert(IsStandardLayout_V<CsgReceiverCpuBounds>, "CsgReceiverCpuBounds must stay cache-friendly");
static_assert(IsTriviallyCopyable_V<CsgReceiverCpuBounds>, "CsgReceiverCpuBounds must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");

using CsgCapMeshVertex = RuntimeMeshCapSourceVertex;
using CsgCapMeshTriangle = RuntimeMeshCapSourceTriangle;

inline constexpr u32 s_CsgCapProxyPlaneShapeMask = 1u << 0;
inline constexpr u32 s_CsgCapProxyBoxShapeMask = 1u << 1;
inline constexpr u32 s_CsgCapProxySphereShapeMask = 1u << 2;
inline constexpr u32 s_CsgCapProxyCapsuleShapeMask = 1u << 3;
inline constexpr u32 s_CsgCapProxyBuiltInShapeMask =
    s_CsgCapProxyPlaneShapeMask
    | s_CsgCapProxyBoxShapeMask
    | s_CsgCapProxySphereShapeMask
    | s_CsgCapProxyCapsuleShapeMask
;

[[nodiscard]] inline constexpr u32 CsgCapProxyShapeMask(const u32 shapeType)noexcept{
    switch(shapeType){
    case NWB_CSG_SHAPE_PLANE: return s_CsgCapProxyPlaneShapeMask;
    case NWB_CSG_SHAPE_BOX: return s_CsgCapProxyBoxShapeMask;
    case NWB_CSG_SHAPE_SPHERE: return s_CsgCapProxySphereShapeMask;
    case NWB_CSG_SHAPE_CAPSULE: return s_CsgCapProxyCapsuleShapeMask;
    default: return 0u;
    }
}

struct CsgCapVertexGpuData{
    Float4 positionReceiverIndex;
    Float4 normalCutterIndex;
    Float4 tangent;
    Float4 color;
    Float4 uv0;
};

struct CsgCapDrawItem{
    u32 firstVertex = 0u;
    u32 vertexCount = 0u;
};

struct CsgCapProxyBounds{
    Float3Int minBounds = Float3Int(0.f, 0.f, 0.f, 0);
    Float3Int maxBounds = Float3Int(0.f, 0.f, 0.f, 0);

    [[nodiscard]] bool valid()const noexcept{ return (minBounds.w & s_CsgBoundsValidFlag) != 0; }
    [[nodiscard]] bool finite()const noexcept{ return (minBounds.w & s_CsgBoundsFiniteFlag) != 0; }
};

struct CsgCapProxyDrawItem{
    NameHash receiverGroupHash = {};
    UInt4U receiverCutterShapePass = {};
    CsgCapProxyBounds receiverBounds;
    CsgCapProxyBounds cutterBounds;

    [[nodiscard]] u32 receiverIndex()const noexcept{ return receiverCutterShapePass.x; }
    [[nodiscard]] u32 cutterIndex()const noexcept{ return receiverCutterShapePass.y; }
    [[nodiscard]] u32 shapeType()const noexcept{ return receiverCutterShapePass.z; }
    [[nodiscard]] CsgReceiverPass::Enum pass()const noexcept{ return static_cast<CsgReceiverPass::Enum>(receiverCutterShapePass.w); }
};

struct CsgCapProxyGpuData{
    UInt4 receiverCutterShapePass = {};
    CsgCapProxyBounds receiverBounds;
    CsgCapProxyBounds cutterBounds;
};

static_assert(sizeof(CsgCapMeshVertex) == sizeof(Float4) * 5u, "CsgCapMeshVertex must stay tightly packed");
static_assert(sizeof(CsgCapMeshTriangle) == sizeof(CsgCapMeshVertex) * 3u, "CsgCapMeshTriangle must stay tightly packed");
static_assert(sizeof(CsgCapVertexGpuData) == sizeof(Float4) * 5u, "CsgCapVertexGpuData layout must match the CSG cap shaders");
static_assert(sizeof(CsgCapProxyGpuData) == sizeof(UInt4) + sizeof(CsgCapProxyBounds) * 2u, "CsgCapProxyGpuData layout must match the CSG cap proxy shaders");
static_assert(alignof(CsgCapMeshVertex) >= alignof(Float4), "CsgCapMeshVertex must stay SIMD-aligned");
static_assert(alignof(CsgCapMeshTriangle) >= alignof(Float4), "CsgCapMeshTriangle must stay SIMD-aligned");
static_assert(alignof(CsgCapVertexGpuData) >= alignof(Float4), "CsgCapVertexGpuData must stay SIMD-aligned");
static_assert(alignof(CsgCapProxyGpuData) >= alignof(Float4), "CsgCapProxyGpuData must stay SIMD-aligned");
static_assert(IsStandardLayout_V<CsgCapMeshVertex>, "CsgCapMeshVertex must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgCapMeshVertex>, "CsgCapMeshVertex must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgCapMeshTriangle>, "CsgCapMeshTriangle must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgCapMeshTriangle>, "CsgCapMeshTriangle must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgCapVertexGpuData>, "CsgCapVertexGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCapVertexGpuData>, "CsgCapVertexGpuData must stay GPU-uploadable");
static_assert(IsStandardLayout_V<CsgCapProxyGpuData>, "CsgCapProxyGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCapProxyGpuData>, "CsgCapProxyGpuData must stay GPU-uploadable");
static_assert(IsStandardLayout_V<CsgCapDrawItem>, "CsgCapDrawItem must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgCapDrawItem>, "CsgCapDrawItem must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgCapProxyBounds>, "CsgCapProxyBounds must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgCapProxyBounds>, "CsgCapProxyBounds must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgCapProxyDrawItem>, "CsgCapProxyDrawItem must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgCapProxyDrawItem>, "CsgCapProxyDrawItem must stay cheap to pass by value");
static_assert(sizeof(CsgCapProxyDrawItem) == sizeof(NameHash) + sizeof(UInt4U) + sizeof(CsgCapProxyBounds) * 2u, "CsgCapProxyDrawItem must stay tightly packed");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CsgReceiverRangeGpuDataVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::ScratchArena>;
using CsgCutterGpuDataVector = Vector<CsgCutterGpuData, Core::Alloc::ScratchArena>;
using CsgParameterByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;
using CsgCapMeshTriangleVector = Vector<CsgCapMeshTriangle, Core::Alloc::GlobalArena>;
using CsgCapVertexGpuDataVector = Vector<CsgCapVertexGpuData, Core::Alloc::ScratchArena>;
using CsgCapDrawItemVector = Vector<CsgCapDrawItem, Core::Alloc::ScratchArena>;
using CsgCapProxyDrawItemVector = Vector<CsgCapProxyDrawItem, Core::Alloc::ScratchArena>;
using CsgCapProxyGpuDataVector = Vector<CsgCapProxyGpuData, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameGpuData{
    CsgReceiverRangeGpuDataVector receiverRanges;
    CsgCutterGpuDataVector cutters;
    CsgParameterByteDataVector parameterBytes;
    CsgCapVertexGpuDataVector capVertices;
    CsgCapDrawItemVector opaqueCapDrawItems;
    CsgCapDrawItemVector transparentCapDrawItems;
    CsgCapProxyDrawItemVector capProxyDrawItems;
    CsgCapProxyGpuDataVector capProxyGpuItems;
    u32 capProxyShapeMask = 0u;

    explicit CsgFrameGpuData(Core::Alloc::ScratchArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
        , parameterBytes(arena)
        , capVertices(arena)
        , opaqueCapDrawItems(arena)
        , transparentCapDrawItems(arena)
        , capProxyDrawItems(arena)
        , capProxyGpuItems(arena)
    {}

    [[nodiscard]] bool hasWork()const noexcept{ return !receiverRanges.empty() && !cutters.empty(); }
    [[nodiscard]] bool hasCapWork()const noexcept{ return !capVertices.empty() && (!opaqueCapDrawItems.empty() || !transparentCapDrawItems.empty()); }
    [[nodiscard]] bool hasOpaqueCapWork()const noexcept{ return !capVertices.empty() && !opaqueCapDrawItems.empty(); }
    [[nodiscard]] bool hasTransparentCapWork()const noexcept{ return !capVertices.empty() && !transparentCapDrawItems.empty(); }
    [[nodiscard]] bool hasCapProxyWork()const noexcept{ return capProxyShapeMask != 0u && !capProxyGpuItems.empty(); }
    void reserve(const usize receiverCapacity, const usize cutterCapacity){
        receiverRanges.reserve(receiverCapacity);
        cutters.reserve(cutterCapacity);
        opaqueCapDrawItems.reserve(cutterCapacity);
        transparentCapDrawItems.reserve(cutterCapacity);
        capProxyDrawItems.reserve(cutterCapacity);
        capProxyGpuItems.reserve(cutterCapacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
