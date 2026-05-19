// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "tangent_frame_rebuild.h"

#include "frame_math.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_tangent_frame_rebuild{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_Epsilon = 0.000001f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct alignas(Float4) TangentFrameAccumulator{
    SIMDVector normal;
    SIMDVector tangent;
    SIMDVector bitangent;
};
static_assert(
    alignof(TangentFrameAccumulator) >= alignof(Float4),
    "TangentFrameAccumulator must keep SIMD accumulator lanes aligned"
);
static_assert(
    (sizeof(TangentFrameAccumulator) % alignof(TangentFrameAccumulator)) == 0,
    "TangentFrameAccumulator array stride must keep every element SIMD-aligned"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] f32 ResolveTangentHandedness(const f32 handedness){
    if(IsFinite(handedness) && Abs(handedness) > s_Epsilon)
        return handedness < 0.0f ? -1.0f : 1.0f;
    return 1.0f;
}

[[nodiscard]] bool ValidInputVertex(const TangentFrameRebuildVertex& vertex){
    return
        FrameFiniteVector(LoadFloat(vertex.position), 0x7u)
        && FrameFiniteVector(LoadFloat(vertex.uv0), 0x3u)
    ;
}

[[nodiscard]] bool AccumulateTriangleTangentFrame(
    const TangentFrameRebuildVertex& vertex0,
    const TangentFrameRebuildVertex& vertex1,
    const TangentFrameRebuildVertex& vertex2,
    SIMDVector edge01,
    SIMDVector edge02,
    SIMDVector& outTangent,
    SIMDVector& outBitangent
){
    const f32 du1 = vertex1.uv0.x - vertex0.uv0.x;
    const f32 dv1 = vertex1.uv0.y - vertex0.uv0.y;
    const f32 du2 = vertex2.uv0.x - vertex0.uv0.x;
    const f32 dv2 = vertex2.uv0.y - vertex0.uv0.y;
    const f32 determinant = (du1 * dv2) - (dv1 * du2);
    if(!IsFinite(determinant) || Abs(determinant) <= s_Epsilon)
        return false;

    const f32 inverseDeterminant = 1.0f / determinant;
    outTangent = VectorScale(
        VectorSubtract(VectorScale(edge01, dv2), VectorScale(edge02, dv1)),
        inverseDeterminant
    );
    outBitangent = VectorScale(
        VectorSubtract(VectorScale(edge02, du1), VectorScale(edge01, du2)),
        inverseDeterminant
    );
    return FrameValidDirection(outTangent) && FrameValidDirection(outBitangent);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RebuildTangentFrames(
    TangentFrameRebuildVertex* vertices,
    const usize vertexCount,
    const u32* indices,
    const usize indexCount,
    TangentFrameRebuildResult* outResult
){
    using namespace __hidden_geometry_tangent_frame_rebuild;

    if(outResult)
        *outResult = TangentFrameRebuildResult{};
    if(
        !vertices
        || !indices
        || vertexCount == 0u
        || indexCount == 0u
        || (indexCount % 3u) != 0u
        || vertexCount > static_cast<usize>(Limit<u32>::s_Max)
    )
        return false;

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!ValidInputVertex(vertices[vertexIndex]))
            return false;
    }

    Core::Alloc::ScratchArena<> scratchArena;
    Vector<TangentFrameAccumulator, Core::Alloc::ScratchArena<>> accumulators(
        vertexCount,
        TangentFrameAccumulator{
            VectorZero(),
            VectorZero(),
            VectorZero(),
        },
        scratchArena
    );

    TangentFrameRebuildResult result;
    const usize triangleCount = indexCount / 3u;
    for(usize triangle = 0u; triangle < triangleCount; ++triangle){
        const usize indexBase = triangle * 3u;
        const u32 i0 = indices[indexBase + 0u];
        const u32 i1 = indices[indexBase + 1u];
        const u32 i2 = indices[indexBase + 2u];
        if(i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount)
            return false;
        if(i0 == i1 || i0 == i2 || i1 == i2)
            return false;

        const TangentFrameRebuildVertex& vertex0 = vertices[i0];
        const TangentFrameRebuildVertex& vertex1 = vertices[i1];
        const TangentFrameRebuildVertex& vertex2 = vertices[i2];
        const SIMDVector p0 = LoadFloat(vertex0.position);
        const SIMDVector edge01 = VectorSubtract(LoadFloat(vertex1.position), p0);
        const SIMDVector edge02 = VectorSubtract(LoadFloat(vertex2.position), p0);
        const SIMDVector faceNormal = Vector3Cross(edge01, edge02);
        if(!FrameValidDirection(faceNormal))
            return false;

        TangentFrameAccumulator& accumulator0 = accumulators[i0];
        TangentFrameAccumulator& accumulator1 = accumulators[i1];
        TangentFrameAccumulator& accumulator2 = accumulators[i2];
        accumulator0.normal = VectorAdd(accumulator0.normal, faceNormal);
        accumulator1.normal = VectorAdd(accumulator1.normal, faceNormal);
        accumulator2.normal = VectorAdd(accumulator2.normal, faceNormal);

        SIMDVector tangent = VectorZero();
        SIMDVector bitangent = VectorZero();
        if(AccumulateTriangleTangentFrame(vertex0, vertex1, vertex2, edge01, edge02, tangent, bitangent)){
            accumulator0.tangent = VectorAdd(accumulator0.tangent, tangent);
            accumulator1.tangent = VectorAdd(accumulator1.tangent, tangent);
            accumulator2.tangent = VectorAdd(accumulator2.tangent, tangent);
            accumulator0.bitangent = VectorAdd(accumulator0.bitangent, bitangent);
            accumulator1.bitangent = VectorAdd(accumulator1.bitangent, bitangent);
            accumulator2.bitangent = VectorAdd(accumulator2.bitangent, bitangent);
        }
        else{
            ++result.degenerateUvTriangleCount;
        }
    }

    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        TangentFrameRebuildVertex& vertex = vertices[vertexIndex];
        const TangentFrameAccumulator& accumulator = accumulators[vertexIndex];
        const SIMDVector previousNormal = FrameNormalizeDirection(LoadFloat(vertex.normal), VectorSet(0.0f, 0.0f, 1.0f, 0.0f));
        const SIMDVector normal = FrameNormalizeDirection(accumulator.normal, previousNormal);
        if(!FrameValidDirection(normal))
            return false;

        const SIMDVector previousTangent = VectorSetW(LoadFloat(vertex.tangent), 0.0f);
        SIMDVector tangentSource = accumulator.tangent;
        if(!FrameValidDirection(tangentSource)){
            tangentSource = previousTangent;
            ++result.fallbackTangentVertexCount;
        }

        const SIMDVector tangent = FrameResolveTangent(normal, tangentSource, previousTangent);
        if(!FrameValidDirection(tangent) || Abs(VectorGetX(Vector3Dot(normal, tangent))) > 0.001f)
            return false;

        f32 handedness = ResolveTangentHandedness(vertex.tangent.w);
        const SIMDVector bitangent = accumulator.bitangent;
        if(FrameValidDirection(bitangent)){
            const f32 bitangentSign = VectorGetX(Vector3Dot(Vector3Cross(normal, tangent), bitangent));
            if(IsFinite(bitangentSign) && Abs(bitangentSign) > s_Epsilon)
                handedness = bitangentSign < 0.0f ? -1.0f : 1.0f;
        }

        StreamFloat(VectorSetW(normal, 0.0f), &vertex.normal);
        StreamFloat(VectorSetW(tangent, handedness), &vertex.tangent);
        ++result.rebuiltVertexCount;
    }
    StreamFloatFence();

    if(outResult)
        *outResult = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_GEOMETRY_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

