// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/components.h>

#include <core/alloc/scratch.h>
#include <core/graphics/api.h>
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

[[nodiscard]] NWB_INLINE Float34 MakeIdentityCsgMatrix(){
    Float34 matrix{};
    matrix.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    matrix.rows[1] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    matrix.rows[2] = Float4(0.0f, 0.0f, 1.0f, 0.0f);
    return matrix;
}

struct CsgReceiverRangeGpuData{
    // The CSG shaders pack these four scalars as a single uint4 (rangeInfo):
    //   x = firstCutter, y = cutterCount, z = flags, w = shadingModelId.
    u32 firstCutter = 0u;
    u32 cutterCount = 0u;
    u32 flags = 0u;
    u32 shadingModelId = 0u;
    Float34 worldToReceiver = MakeIdentityCsgMatrix();
    CsgBoundsGpuData localBounds;
    Float4 baseColor = Float4(0.78f, 0.72f, 0.76f, 1.0f);
};

struct CsgCutterGpuData{
    u32 shapeType = 0u;
    f32 worldToShapeScaleBound = 1.f;
    u32 padding0 = 0u;
    u32 padding1 = 0u;
    Float34 worldToShape = MakeIdentityCsgMatrix();
    Float4 parameter0 = Float4(0.f, 0.f, 0.f, 0.f);
    Float4 parameter1 = Float4(0.f, 0.f, 0.f, 0.f);
};

static_assert(sizeof(CsgReceiverRangeGpuData) == sizeof(u32) * 4u + sizeof(Float34) + sizeof(CsgBoundsGpuData) + sizeof(Float4), "CsgReceiverRangeGpuData layout must match the CSG shader");
static_assert(sizeof(CsgCutterGpuData) == sizeof(Float4) + sizeof(Float34) + sizeof(Float4) * 2u, "CsgCutterGpuData layout must match the CSG shader");
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgFrameWorkRegion{
    u32 minX = Limit<u32>::s_Max;
    u32 minY = Limit<u32>::s_Max;
    u32 maxX = 0u;
    u32 maxY = 0u;
    bool fullFrame = false;

    [[nodiscard]] bool bounded()const noexcept{ return !fullFrame && minX < maxX && minY < maxY; }
    [[nodiscard]] bool valid()const noexcept{ return fullFrame || bounded(); }
    [[nodiscard]] u32 width()const noexcept{ return bounded() ? maxX - minX : 0u; }
    [[nodiscard]] u32 height()const noexcept{ return bounded() ? maxY - minY : 0u; }

    void expandFull()noexcept{ fullFrame = true; }
    void expandClamped(const i32 rectMinX, const i32 rectMaxX, const i32 rectMinY, const i32 rectMaxY, const u32 frameWidth, const u32 frameHeight)noexcept{
        if(fullFrame || frameWidth == 0u || frameHeight == 0u)
            return;

        const i32 clampedMinX = Max<i32>(0, Min<i32>(rectMinX, static_cast<i32>(frameWidth)));
        const i32 clampedMaxX = Max<i32>(0, Min<i32>(rectMaxX, static_cast<i32>(frameWidth)));
        const i32 clampedMinY = Max<i32>(0, Min<i32>(rectMinY, static_cast<i32>(frameHeight)));
        const i32 clampedMaxY = Max<i32>(0, Min<i32>(rectMaxY, static_cast<i32>(frameHeight)));
        if(clampedMinX >= clampedMaxX || clampedMinY >= clampedMaxY)
            return;

        minX = Min(minX, static_cast<u32>(clampedMinX));
        minY = Min(minY, static_cast<u32>(clampedMinY));
        maxX = Max(maxX, static_cast<u32>(clampedMaxX));
        maxY = Max(maxY, static_cast<u32>(clampedMaxY));
    }
    [[nodiscard]] Core::Rect resolveRect(const u32 frameWidth, const u32 frameHeight)const noexcept{
        if(!bounded())
            return Core::Rect(static_cast<i32>(frameWidth), static_cast<i32>(frameHeight));

        return Core::Rect(
            static_cast<i32>(minX),
            static_cast<i32>(Min(maxX, frameWidth)),
            static_cast<i32>(minY),
            static_cast<i32>(Min(maxY, frameHeight))
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverClipDrawInfo{
    u32 cutterCount = 0u;
    Name evaluatorVariant = NAME_NONE;
};

struct CsgFrameGpuData{
    CsgReceiverRangeGpuDataVector receiverRanges;
    CsgCutterGpuDataVector cutters;
    CsgFrameWorkRegion workRegion;

    explicit CsgFrameGpuData(Core::Alloc::ScratchArena& arena)
        : receiverRanges(arena)
        , cutters(arena)
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

