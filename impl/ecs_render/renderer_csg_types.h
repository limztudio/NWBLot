// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
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

struct CsgReceiverCpuBounds{
    Float4 minBounds = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 maxBounds = Float4(0.f, 0.f, 0.f, 0.f);
    bool valid = false;
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

static_assert(sizeof(CsgCapMeshVertex) == sizeof(Float4) * 5u, "CsgCapMeshVertex must stay tightly packed");
static_assert(sizeof(CsgCapMeshTriangle) == sizeof(CsgCapMeshVertex) * 3u, "CsgCapMeshTriangle must stay tightly packed");
static_assert(sizeof(CsgCapVertexGpuData) == sizeof(Float4) * 5u, "CsgCapVertexGpuData layout must match the CSG cap shaders");
static_assert(alignof(CsgCapMeshVertex) >= alignof(Float4), "CsgCapMeshVertex must stay SIMD-aligned");
static_assert(alignof(CsgCapMeshTriangle) >= alignof(Float4), "CsgCapMeshTriangle must stay SIMD-aligned");
static_assert(alignof(CsgCapVertexGpuData) >= alignof(Float4), "CsgCapVertexGpuData must stay SIMD-aligned");
static_assert(IsStandardLayout_V<CsgCapMeshVertex>, "CsgCapMeshVertex must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgCapMeshVertex>, "CsgCapMeshVertex must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgCapMeshTriangle>, "CsgCapMeshTriangle must stay GPU-friendly");
static_assert(IsTriviallyCopyable_V<CsgCapMeshTriangle>, "CsgCapMeshTriangle must stay GPU-friendly");
static_assert(IsStandardLayout_V<CsgCapVertexGpuData>, "CsgCapVertexGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCapVertexGpuData>, "CsgCapVertexGpuData must stay GPU-uploadable");
static_assert(IsStandardLayout_V<CsgCapDrawItem>, "CsgCapDrawItem must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgCapDrawItem>, "CsgCapDrawItem must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CsgReceiverRangeGpuDataVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::ScratchArena>;
using CsgCutterGpuDataVector = Vector<CsgCutterGpuData, Core::Alloc::ScratchArena>;
using CsgParameterByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;
using CsgCapMeshTriangleVector = Vector<CsgCapMeshTriangle, Core::Alloc::GlobalArena>;
using CsgCapVertexGpuDataVector = Vector<CsgCapVertexGpuData, Core::Alloc::ScratchArena>;
using CsgCapDrawItemVector = Vector<CsgCapDrawItem, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameGpuData{
    CsgReceiverRangeGpuDataVector receiverRanges;
    CsgCutterGpuDataVector cutters;
    CsgParameterByteDataVector parameterBytes;
    CsgCapVertexGpuDataVector capVertices;
    CsgCapDrawItemVector opaqueCapDrawItems;
    CsgCapDrawItemVector transparentCapDrawItems;

    explicit CsgFrameGpuData(Core::Alloc::ScratchArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
        , parameterBytes(arena)
        , capVertices(arena)
        , opaqueCapDrawItems(arena)
        , transparentCapDrawItems(arena)
    {}

    [[nodiscard]] bool hasWork()const noexcept{ return !receiverRanges.empty() && !cutters.empty(); }
    [[nodiscard]] bool hasCapWork()const noexcept{ return !capVertices.empty() && (!opaqueCapDrawItems.empty() || !transparentCapDrawItems.empty()); }
    [[nodiscard]] bool hasOpaqueCapWork()const noexcept{ return !capVertices.empty() && !opaqueCapDrawItems.empty(); }
    [[nodiscard]] bool hasTransparentCapWork()const noexcept{ return !capVertices.empty() && !transparentCapDrawItems.empty(); }
    void reserve(const usize receiverCapacity, const usize cutterCapacity){
        receiverRanges.reserve(receiverCapacity);
        cutters.reserve(cutterCapacity);
        opaqueCapDrawItems.reserve(cutterCapacity);
        transparentCapDrawItems.reserve(cutterCapacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

