// limztudio@gmail.com
//////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_validator.h"

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_BEGIN


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

using ScratchArena = Core::Alloc::ScratchArena;

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

bool CsgDeformValidator::FiniteFloat(const f32 value){
    return value == value && value != Limit<f32>::s_Infinity && value != -Limit<f32>::s_Infinity;
}

bool CsgDeformValidator::FiniteVertex(const CsgDeformVertex& vertex){
    return CsgDeformValidator::FiniteFloat(vertex.position.x)
        && CsgDeformValidator::FiniteFloat(vertex.position.y)
        && CsgDeformValidator::FiniteFloat(vertex.position.z)
        && CsgDeformValidator::FiniteFloat(vertex.normal.x)
        && CsgDeformValidator::FiniteFloat(vertex.normal.y)
        && CsgDeformValidator::FiniteFloat(vertex.normal.z)
        && CsgDeformValidator::FiniteFloat(vertex.normal.w)
        && CsgDeformValidator::FiniteFloat(vertex.tangent.x)
        && CsgDeformValidator::FiniteFloat(vertex.tangent.y)
        && CsgDeformValidator::FiniteFloat(vertex.tangent.z)
        && CsgDeformValidator::FiniteFloat(vertex.tangent.w)
        && CsgDeformValidator::FiniteFloat(vertex.uv0.x)
        && CsgDeformValidator::FiniteFloat(vertex.uv0.y)
        && CsgDeformValidator::FiniteFloat(vertex.color.x)
        && CsgDeformValidator::FiniteFloat(vertex.color.y)
        && CsgDeformValidator::FiniteFloat(vertex.color.z)
        && CsgDeformValidator::FiniteFloat(vertex.color.w)
    ;
}

f32 CsgDeformValidator::SaturateFloat(const f32 value){
    // SIMD clamp keeps the scalar weight on vector lanes (min/max, no branches).
    return VectorGetX(VectorSaturate(VectorReplicate(value)));
}

f32 CsgDeformValidator::ShapeEpsilon(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_MinEpsilon ? options.distanceEpsilon : s_MinEpsilon;
}

bool CsgDeformValidator::ValidOptions(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_OptionEpsilonLow && options.distanceEpsilon < s_OptionEpsilonHigh;
}

bool CsgDeformValidator::ValidTopology(
    NotNull<const CsgDeformVertex*> vertices,
    const usize vertexCount,
    NotNull<const CsgDeformTriangle*> triangles,
    const usize triangleCount
){
    static_cast<void>(vertices);
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle& triangle = triangles.get()[triangleIndex];
        for(usize corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(triangle.indices[corner] >= vertexCount)
                return false;
        }
    }
    return true;
}

bool CsgDeformValidator::FiniteInput(
    NotNull<const CsgDeformVertex*> vertices,
    const usize vertexCount,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!CsgDeformValidator::FiniteVertex(vertices.get()[vertexIndex])){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

NWB_IMPL_END


//////////////////////////////////////////////////////////////////////////////////////////////////////////////////

