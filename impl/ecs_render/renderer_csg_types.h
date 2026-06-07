// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/alloc/scratch.h>
#include <impl/assets/graphics/csg/constants.h>
#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>

#include <global/containers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr i32 s_CsgBoundsValidFlag = NWB_CSG_BOUNDS_VALID_FLAG;
inline constexpr i32 s_CsgBoundsFiniteFlag = NWB_CSG_BOUNDS_FINITE_FLAG;

struct CsgBoundsGpuData{
    Float3Int minBounds = Float3Int(0.f, 0.f, 0.f, 0);
    Float3Int maxBounds = Float3Int(0.f, 0.f, 0.f, 0);

    [[nodiscard]] bool valid()const noexcept{ return (minBounds.w & s_CsgBoundsValidFlag) != 0; }
    [[nodiscard]] bool finite()const noexcept{ return (minBounds.w & s_CsgBoundsFiniteFlag) != 0; }
};

using CsgReceiverCpuBounds = CsgBoundsGpuData;

[[nodiscard]] inline Float34 MakeIdentityCsgMatrix(){
    Float34 matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    return matrix;
}

struct CsgReceiverRangeGpuData{
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    u32 padding0 = 0u;
    Float34 worldToReceiver = MakeIdentityCsgMatrix();
    CsgBoundsGpuData localBounds;
};

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

static_assert(sizeof(CsgReceiverRangeGpuData) == sizeof(u32) * 4u + sizeof(Float34) + sizeof(CsgBoundsGpuData), "CsgReceiverRangeGpuData layout must match the CSG shader");
static_assert(sizeof(CsgCutterGpuData) == sizeof(u32) * 4u + sizeof(Float34) * 2u + sizeof(Float4) * 3u, "CsgCutterGpuData layout must match the CSG shader");
static_assert(alignof(CsgReceiverRangeGpuData) >= alignof(Float4), "CsgReceiverRangeGpuData must stay SIMD-aligned");
static_assert(alignof(CsgCutterGpuData) >= alignof(Float4), "CsgCutterGpuData must stay SIMD-aligned");
static_assert(alignof(CsgReceiverCpuBounds) >= alignof(Float4), "CsgReceiverCpuBounds must stay SIMD-friendly");
static_assert(IsStandardLayout_V<CsgReceiverCpuBounds>, "CsgReceiverCpuBounds must stay cache-friendly");
static_assert(IsTriviallyCopyable_V<CsgReceiverCpuBounds>, "CsgReceiverCpuBounds must stay cheap to pass by value");
static_assert(IsStandardLayout_V<CsgReceiverRangeGpuData>, "CsgReceiverRangeGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgReceiverRangeGpuData>, "CsgReceiverRangeGpuData must stay GPU-uploadable");
static_assert(IsStandardLayout_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");
static_assert(IsTriviallyCopyable_V<CsgCutterGpuData>, "CsgCutterGpuData must stay GPU-uploadable");

using CsgReceiverRangeGpuDataVector = Vector<CsgReceiverRangeGpuData, Core::Alloc::ScratchArena>;
using CsgCutterGpuDataVector = Vector<CsgCutterGpuData, Core::Alloc::ScratchArena>;
using CsgParameterByteDataVector = Vector<u8, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverClipDrawInfo{
    u32 cutterCount = 0u;
    Name evaluatorVariant = NAME_NONE;
};

struct CsgFrameGpuData{
    CsgReceiverRangeGpuDataVector receiverRanges;
    CsgCutterGpuDataVector cutters;
    CsgParameterByteDataVector parameterBytes;

    explicit CsgFrameGpuData(Core::Alloc::ScratchArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
        , parameterBytes(arena)
    {}

    [[nodiscard]] bool hasWork()const noexcept{ return !receiverRanges.empty() && !cutters.empty(); }
    void reserve(const usize receiverCapacity, const usize cutterCapacity){
        receiverRanges.reserve(receiverCapacity);
        cutters.reserve(cutterCapacity);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
