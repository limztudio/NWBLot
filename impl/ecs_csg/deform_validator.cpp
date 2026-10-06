// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using ScratchArena = Core::Alloc::ScratchArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CsgDeformValidator::FiniteFloat(const f32 value)noexcept{
    return value == value && value != Limit<f32>::s_Infinity && value != -Limit<f32>::s_Infinity;
}

bool CsgDeformValidator::FiniteVertex(const CsgDeformVertex& vertex)noexcept{
    return VectorIsFinite(LoadFloat(vertex.position), VectorComponentMask::s_XYZ)
        && VectorIsFinite(LoadFloat(vertex.normal), VectorComponentMask::s_XYZW)
        && VectorIsFinite(LoadFloat(vertex.tangent), VectorComponentMask::s_XYZW)
        && VectorIsFinite(LoadFloat(vertex.uv0), VectorComponentMask::s_XY)
        && VectorIsFinite(LoadFloat(vertex.color), VectorComponentMask::s_XYZW)
    ;
}

f32 CsgDeformValidator::SaturateFloat(const f32 value)noexcept{
    return VectorGetX(VectorSaturate(VectorReplicate(value)));
}

SIMDVector CsgDeformValidator::SaturateVec(const SIMDVector value)noexcept{
    return VectorSaturate(value);
}

SIMDVector CsgDeformValidator::AbsDivideVec(const SIMDVector numerator, const SIMDVector denominator)noexcept{
    return VectorAbs(VectorDivide(numerator, denominator));
}

f32 CsgDeformValidator::ShapeEpsilon(const CsgDeformBuildOptions& options)noexcept{
    return options.distanceEpsilon > s_MinEpsilon ? options.distanceEpsilon : s_MinEpsilon;
}

bool CsgDeformValidator::ValidOptions(const CsgDeformBuildOptions& options)noexcept{
    return options.distanceEpsilon > s_OptionEpsilonLow && options.distanceEpsilon < s_OptionEpsilonHigh;
}

bool CsgDeformValidator::ValidTopology(
    NotNull<const CsgDeformTriangle*> triangles,
    const usize triangleCount,
    const usize vertexCount
)noexcept{
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
)noexcept{
    outReason = CsgDeformViabilityReason::Ok;
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        if(!CsgDeformValidator::FiniteVertex(vertices.get()[vertexIndex])){
            outReason = CsgDeformViabilityReason::NonFiniteInput;
            return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

