// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) TangentFrameRebuildVertex{
    Float3U position;
    Float2U uv0;
    Float3U normal;
    Float4U tangent;
};
static_assert(IsStandardLayout_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay layout-stable");
static_assert(IsTriviallyCopyable_V<TangentFrameRebuildVertex>, "TangentFrameRebuildVertex must stay cheap to copy");
static_assert(sizeof(TangentFrameRebuildVertex) == sizeof(f32) * 12u, "TangentFrameRebuildVertex layout drifted");
static_assert(alignof(TangentFrameRebuildVertex) >= alignof(Float4), "TangentFrameRebuildVertex must stay SIMD-aligned");
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

template<typename VertexAllocator, typename IndexAllocator>
[[nodiscard]] bool RebuildTangentFrames(
    Vector<TangentFrameRebuildVertex, VertexAllocator>& vertices,
    const Vector<u32, IndexAllocator>& indices,
    TangentFrameRebuildResult* outResult = nullptr){
    return RebuildTangentFrames(vertices.data(), vertices.size(), indices.data(), indices.size(), outResult);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

