// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgDeformValidator::finiteFloat(const f32 value){
    return value == value && value != Limit<f32>::s_Infinity && value != -Limit<f32>::s_Infinity;
}

bool CsgDeformValidator::finiteVertex(const CsgDeformVertex& vertex){
    return VectorIsFinite(LoadFloat(vertex.position), VectorComponentMask::s_XYZ)
        && VectorIsFinite(LoadFloat(vertex.normal), VectorComponentMask::s_XYZW)
        && VectorIsFinite(LoadFloat(vertex.tangent), VectorComponentMask::s_XYZW)
        && VectorIsFinite(LoadFloat(vertex.uv0), VectorComponentMask::s_XY)
        && VectorIsFinite(LoadFloat(vertex.color), VectorComponentMask::s_XYZW)
    ;
}

f32 CsgDeformValidator::saturateFloat(const f32 value){
    return VectorGetX(VectorSaturate(VectorReplicate(value)));
}

SIMDVector CsgDeformValidator::saturateVec(const SIMDVector value){
    return VectorSaturate(value);
}

SIMDVector CsgDeformValidator::absDivideVec(const SIMDVector numerator, const SIMDVector denominator){
    return VectorAbs(VectorDivide(numerator, denominator));
}

f32 CsgDeformValidator::shapeEpsilon(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_MinEpsilon ? options.distanceEpsilon : s_MinEpsilon;
}

bool CsgDeformValidator::validOptions(const CsgDeformBuildOptions& options){
    return options.distanceEpsilon > s_OptionEpsilonLow && options.distanceEpsilon < s_OptionEpsilonHigh;
}

bool CsgDeformValidator::validTopology(
    NotNull<const CsgDeformTriangle*> triangles,
    const usize triangleCount,
    const usize vertexCount
){
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle& triangle = triangles.get()[triangleIndex];
        for(usize corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(triangle.indices[corner] >= vertexCount)
                return false;
        }
    }
    return true;
}

bool CsgDeformValidator::finiteInput(
    NotNull<const CsgDeformVertex*> vertices,
    const usize vertexCount,
    CsgDeformViabilityReason::Enum& outReason
){
    outReason = CsgDeformViabilityReason::Ok;
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!CsgDeformValidator::finiteVertex(vertices.get()[vertexIndex])){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

