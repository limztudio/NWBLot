// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryVertex;

static constexpr u32 s_DeformableOperatorFootprintMaxVertexCount = 32u;
static constexpr u32 s_DeformableOperatorProfileMaxSampleCount = 8u;
static constexpr f32 s_DeformableOperatorProfileDepthEpsilon = 0.00001f;

struct DeformableOperatorFootprint{
    u32 vertexCount = 0u;
    Float2U vertices[s_DeformableOperatorFootprintMaxVertexCount] = {};
};
static_assert(IsStandardLayout_V<DeformableOperatorFootprint>, "DeformableOperatorFootprint must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableOperatorFootprint>, "DeformableOperatorFootprint must stay cheap to copy");

struct DeformableOperatorProfileSample{
    // Normalized inward distance from the operator top plane: 0 = surface, 1 = params.depth.
    f32 depth = 0.0f;
    // Radial scale relative to the operator's top cross-section.
    f32 scale = 1.0f;
    // Cross-section center in normalized operator XY.
    Float2U center = Float2U(0.0f, 0.0f);
};
static_assert(IsStandardLayout_V<DeformableOperatorProfileSample>, "DeformableOperatorProfileSample must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableOperatorProfileSample>, "DeformableOperatorProfileSample must stay cheap to copy");

struct DeformableOperatorProfile{
    u32 sampleCount = 0u;
    DeformableOperatorProfileSample samples[s_DeformableOperatorProfileMaxSampleCount] = {};
};
static_assert(IsStandardLayout_V<DeformableOperatorProfile>, "DeformableOperatorProfile must stay layout-stable");
static_assert(IsTriviallyCopyable_V<DeformableOperatorProfile>, "DeformableOperatorProfile must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f32 OperatorFootprintSignedArea(const DeformableOperatorFootprint& footprint);
[[nodiscard]] bool ValidateOperatorFootprint(const DeformableOperatorFootprint& footprint);
[[nodiscard]] bool ValidateOperatorProfile(const DeformableOperatorProfile& profile);
[[nodiscard]] bool ValidateHoleOperatorShape(
    const DeformableOperatorFootprint& footprint,
    const DeformableOperatorProfile& profile
);
[[nodiscard]] bool PointInsideOperatorFootprint(
    const DeformableOperatorFootprint& footprint,
    f32 x,
    f32 y
);
[[nodiscard]] bool SampleOperatorProfile(
    const DeformableOperatorProfile& profile,
    f32 rawDepth,
    Float2U& outCenter,
    f32& outScale
);
[[nodiscard]] bool BuildOperatorFootprintFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint
);
[[nodiscard]] bool BuildOperatorProfileFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorProfile& outProfile
);
[[nodiscard]] bool BuildOperatorShapeFromGeometry(
    const Vector<GeometryVertex>& vertices,
    DeformableOperatorFootprint& outFootprint,
    DeformableOperatorProfile& outProfile
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

