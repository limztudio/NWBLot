
#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) TangentFrameRebuildVertex{
    Float4 position;
    Float4 normal;
    Float4 tangent;
    Float2U uv0;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay cheap to copy");
static_assert(sizeof(TangentFrameRebuildVertex) == sizeof(f32) * 16u, "TangentFrameRebuildVertex layout drifted");
static_assert(alignof(TangentFrameRebuildVertex) >= alignof(Float4), "TangentFrameRebuildVertex must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, position) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::position must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, normal) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::normal must stay SIMD-aligned");
static_assert((offsetof(TangentFrameRebuildVertex, tangent) % alignof(Float4)) == 0, "TangentFrameRebuildVertex::tangent must stay SIMD-aligned");

struct TangentFrameRebuildResult{
    u32 rebuiltVertexCount = 0;
    u32 degenerateUvTriangleCount = 0;
    u32 fallbackTangentVertexCount = 0;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildResult>, "TangentFrameRebuildResult must stay cheap to copy");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool RebuildTangentFrames(
    TangentFrameRebuildVertex* vertices,
    usize vertexCount,
    const u32* indices,
    usize indexCount,
    TangentFrameRebuildResult* outResult = nullptr
);

template<typename VertexVectorT, typename IndexVectorT>
[[nodiscard]] bool RebuildTangentFrames(
    VertexVectorT& vertices,
    const IndexVectorT& indices,
    TangentFrameRebuildResult* outResult = nullptr){
    return RebuildTangentFrames(vertices.data(), vertices.size(), indices.data(), indices.size(), outResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_MESH_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

