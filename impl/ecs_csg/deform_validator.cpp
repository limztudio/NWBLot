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

bool CsgDeformValidator::FiniteVertexVec(const SIMDVectorSingle position, const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle uv0, const SIMDVectorSingle color)noexcept{
    return
        VectorIsFinite(position, VectorComponentMask::s_XYZ)
        && VectorIsFinite(normal, VectorComponentMask::s_XYZW)
        && VectorIsFinite(tangent, VectorComponentMask::s_XYZW)
        && VectorIsFinite(uv0, VectorComponentMask::s_XY)
        && VectorIsFinite(color, VectorComponentMask::s_XYZW)
    ;
}

f32 CsgDeformValidator::ShapeEpsilon(const CsgDeformBuildOptions& options)noexcept{
    return Max(options.distanceEpsilon, s_MinEpsilon);
}

bool CsgDeformValidator::ValidOptions(const CsgDeformBuildOptions& options)noexcept{
    return options.distanceEpsilon > s_OptionEpsilonLow && options.distanceEpsilon < s_OptionEpsilonHigh;
}

bool CsgDeformValidator::ValidTopology(NotNull<const CsgDeformTriangle*> triangles, const usize triangleCount, const usize vertexCount)noexcept{
    for(usize triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
        const CsgDeformTriangle& triangle = triangles.get()[triangleIndex];
        for(usize corner = 0u; corner < s_TriangleCornerCount; ++corner){
            if(triangle.indices[corner] >= vertexCount)
                return false;
        }
    }
    return true;
}

Expected<void, CsgDeformViabilityReason::Enum> CsgDeformValidator::FiniteInput(NotNull<const CsgDeformVertex*> vertices, const usize vertexCount)noexcept{
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices.get()[vertexIndex];
        if(!FiniteVertexVec(LoadFloat(vertex.position), LoadFloat(vertex.normal), LoadFloat(vertex.tangent), LoadFloat(vertex.uv0), LoadFloat(vertex.color))){
            return MakeUnexpected(CsgDeformViabilityReason::NonFiniteInput);
        }
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

