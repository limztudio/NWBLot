// limztudio@gmail.com
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

// Shared POD and deterministic thresholds for deformable CSG editing.
//
// Preview and commit observe one epsilon, one edge-cache rule, and one cap
// orientation rule through these constants, so viability always agrees.
// Default cutter/build values. Named so preview and commit observe one default.
inline constexpr f32 s_DefaultDistanceEpsilon = 0.00001f;
inline constexpr Float4 s_DefaultShapeParameter = Float4(0.0f, 1.0f, 0.0f, 0.0f);

struct CsgDeformVertex{
    Float3U position;
    Float4 normal;
    Float4 tangent;
    Float2U uv0;
    Float4 color;
};

static_assert(IsStandardLayout_V<CsgDeformVertex>, "CsgDeformVertex must stay layout-stable for deform rebuild");
static_assert(IsTriviallyCopyable_V<CsgDeformVertex>, "CsgDeformVertex must stay cheap to copy in deform rebuild");

struct CsgDeformTriangle{
    u32 indices[3u];
};

static_assert(IsStandardLayout_V<CsgDeformTriangle>, "CsgDeformTriangle must stay layout-stable for deform rebuild");
static_assert(IsTriviallyCopyable_V<CsgDeformTriangle>, "CsgDeformTriangle must stay cheap to copy in deform rebuild");

template<typename ArenaT>
using CsgDeformVertexVector = Vector<CsgDeformVertex, ArenaT>;
template<typename ArenaT>
using CsgDeformTriangleVector = Vector<CsgDeformTriangle, ArenaT>;

// Cutter shape mirrors the GPU SDF evals (plane/box/sphere/capsule).
// parameter0 packs the same fields as the shader cutter:
// - plane: xyz normal, w distance.
// - box: xyz half extents.
// - sphere: x radius.
// - capsule: x radius, y half height.
struct CsgDeformShape{
    Name shapeType = NAME_NONE;
    Float34 worldToShape = Float34Identity();
    Float4 parameter0 = s_DefaultShapeParameter;
};

static_assert(IsStandardLayout_V<CsgDeformShape>, "CsgDeformShape must stay layout-stable for deform rebuild");
static_assert(IsTriviallyCopyable_V<CsgDeformShape>, "CsgDeformShape must stay cheap to copy in deform rebuild");

struct CsgDeformCutDesc{
    CsgDeformShape shape;
    bool active = true;
};

static_assert(IsStandardLayout_V<CsgDeformCutDesc>, "CsgDeformCutDesc must stay layout-stable for deform rebuild");
static_assert(IsTriviallyCopyable_V<CsgDeformCutDesc>, "CsgDeformCutDesc must stay cheap to copy in deform rebuild");

struct CsgDeformBuildOptions{
    f32 distanceEpsilon = s_DefaultDistanceEpsilon;
    bool fillCaps = true;
};

static_assert(IsStandardLayout_V<CsgDeformBuildOptions>, "CsgDeformBuildOptions must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgDeformBuildOptions>, "CsgDeformBuildOptions must stay cheap to pass by value");

namespace CsgDeformViabilityReason{
    enum Enum : u8{
        Ok,
        EmptyInput,
        NonFiniteInput,
        InvalidTopology,
        InvalidCutter,
        CapLoopFailed,
        NoKeptGeometry,
        TooLarge,
    };
};

struct CsgDeformViability{
    bool viable = false;
    CsgDeformViabilityReason::Enum reason = CsgDeformViabilityReason::Ok;
};

static_assert(IsStandardLayout_V<CsgDeformViability>, "CsgDeformViability must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgDeformViability>, "CsgDeformViability must stay cheap to pass by value");

struct CsgDeformStats{
    u32 inputVertexCount = 0u;
    u32 inputTriangleCount = 0u;
    u32 outputVertexCount = 0u;
    u32 outputTriangleCount = 0u;
    u32 appliedCutCount = 0u;
    u32 capTriangleCount = 0u;
};

static_assert(IsStandardLayout_V<CsgDeformStats>, "CsgDeformStats must stay layout-stable");
static_assert(IsTriviallyCopyable_V<CsgDeformStats>, "CsgDeformStats must stay cheap to pass by value");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Single source of truth for rebuild thresholds. Every domain class
// (validator, cutter field, wall, cap, pipeline) shares these so preview and
// commit observe identical epsilon, capacity, and seam rules.
inline constexpr f32 s_MinEpsilon = 0.0000001f;
inline constexpr f32 s_SplitDenominatorEpsilon = 0.0000001f;
inline constexpr f32 s_NormalizeEpsilon = 0.000001f;
inline constexpr f32 s_NormalizeEpsilonSq = s_NormalizeEpsilon * s_NormalizeEpsilon;
inline constexpr f32 s_LoopAreaEpsilonSq = s_MinEpsilon * s_MinEpsilon;
inline constexpr f32 s_OptionEpsilonLow = 0.0f;
inline constexpr f32 s_OptionEpsilonHigh = 1.0f;
inline constexpr f32 s_KeepDistanceZero = 0.0f;
inline constexpr f32 s_OneWeight = 1.0f;
inline constexpr f32 s_AffineW = 1.0f;
inline constexpr f32 s_ShapeWMask = 0.0f;
inline constexpr f32 s_NegativeOne = -1.0f;
inline constexpr Float4 s_UpAxis = Float4(0.0f, 1.0f, 0.0f, 0.0f);
inline constexpr Float4 s_FallbackTangent = Float4(1.0f, 0.0f, 0.0f, 1.0f);
inline constexpr u32 s_DeformCapacityShift = 20u;
inline constexpr usize s_MaxDeformVertices = 1u << s_DeformCapacityShift;
inline constexpr usize s_MaxDeformTriangles = 1u << s_DeformCapacityShift;
inline constexpr u32 s_EdgeKeyHalfBits = 32u;
inline constexpr u32 s_EdgeHashShift = 33u;
inline constexpr u32 s_TriangleCornerCount = 3u;
inline constexpr u32 s_MinLoopVertices = 3u;
inline constexpr u32 s_EdgesPerTriangle = 3u;
inline constexpr u32 s_KeptReserveMultiplier = 2u;
inline constexpr u32 s_ReserveSlack = 1u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Canonical edge id keeps (a,b) and (b,a) identical without hashing pointers.
// Shared by wall splits and cap boundary collection so both observe one seam rule.
[[nodiscard]] inline u64 CsgDeformEdgeKey(const u32 first, const u32 second){
    const u32 lo = first < second ? first : second;
    const u32 hi = first < second ? second : first;
    return (static_cast<u64>(lo) << s_EdgeKeyHalfBits) | static_cast<u64>(hi);
}

struct CsgDeformEdgeSplitKeyHash{
    [[nodiscard]] usize operator()(const u64 key)const noexcept{ return static_cast<usize>(key ^ (key >> s_EdgeHashShift)); }
};

using CsgDeformEdgeSplitMap = HashMap<u64, u32, CsgDeformEdgeSplitKeyHash, EqualTo<u64>, Core::Alloc::ScratchArena>;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

