// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/alloc/general.h>
#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Deterministic CPU-side deformable CSG editing.
//
// Preview and commit share one rebuild path so viability always agrees:
// - Sequential cuts apply in order, one rebuild per active cut.
// - Rebuild splits triangles at the zero crossing, never welds source verts.
// - Walls are the kept split triangles with interpolated attributes.
// - Caps fill cut boundary loops with a deterministic fan.
// - One epsilon, one edge-cache rule, one cap orientation rule for both preview and commit.
// Default cutter/build values. Named so preview and commit observe one default.
inline constexpr f32 s_DefaultDistanceEpsilon = 0.00001f;
inline constexpr f32 s_DefaultShapeParameterX = 0.0f;
inline constexpr f32 s_DefaultShapeParameterY = 1.0f;
inline constexpr f32 s_DefaultShapeParameterZ = 0.0f;
inline constexpr f32 s_DefaultShapeParameterW = 0.0f;

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
    Float4 parameter0 = Float4(s_DefaultShapeParameterX, s_DefaultShapeParameterY, s_DefaultShapeParameterZ, s_DefaultShapeParameterW);
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


// Shared viability classifier used by both preview and commit.
[[nodiscard]] CsgDeformViability CheckCsgDeformCutsViability(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options
);

// Preview rebuild into scratch storage. Same code path as commit.
[[nodiscard]] bool PreviewCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::ScratchArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::ScratchArena>& outTriangles,
    CsgDeformStats& outStats
);

// Commit rebuild. Runs the same rebuild as preview into scratch, then copies
// into the commit arena, so preview viability always matches commit viability.
[[nodiscard]] bool CommitCsgDeformCuts(
    Core::Alloc::ScratchArena& scratchArena,
    Core::Alloc::GlobalArena& commitArena,
    NotNull<const CsgDeformVertex*> inputVertices,
    usize inputVertexCount,
    NotNull<const CsgDeformTriangle*> inputTriangles,
    usize inputTriangleCount,
    const CsgDeformCutDesc* cuts,
    usize cutCount,
    const CsgDeformBuildOptions& options,
    CsgDeformVertexVector<Core::Alloc::GlobalArena>& outVertices,
    CsgDeformTriangleVector<Core::Alloc::GlobalArena>& outTriangles,
    CsgDeformStats& outStats
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

