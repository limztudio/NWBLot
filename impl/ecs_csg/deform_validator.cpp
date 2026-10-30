// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "deform_validator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 CsgDeformValidator::FiniteVertexVec(const SIMDVectorSingle position, const SIMDVectorSingle normal, const SIMDVectorSingle tangent, const SIMDVectorSingle uv0, const SIMDVectorSingle color)noexcept{
    return
        VectorIsFinite(position, VectorComponentMask::s_XYZ)
        && VectorIsFinite(normal, VectorComponentMask::s_XYZW)
        && VectorIsFinite(tangent, VectorComponentMask::s_XYZW)
        && VectorIsFinite(uv0, VectorComponentMask::s_XY)
        && VectorIsFinite(color, VectorComponentMask::s_XYZW)
    ;
}

SIMDVectorSingle CsgDeformValidator::ShapeEpsilon(const SIMDVectorSingle distanceEpsilon)noexcept{
    return VectorMax(distanceEpsilon, VectorReplicate(s_MinEpsilon));
}

Expected<void, CsgDeformViabilityReason::Enum> CsgDeformValidator::FiniteInput(NotNull<const CsgDeformVertex*> vertices, const usize vertexCount)noexcept{
    for(usize vertexIndex = 0u; vertexIndex < vertexCount; ++vertexIndex){
        const CsgDeformVertex& vertex = vertices.get()[vertexIndex];
        if(!FiniteVertexVec(LoadFloat(vertex.position), LoadFloat(vertex.normal), LoadFloat(vertex.tangent), LoadFloat(vertex.uv0), LoadFloat(vertex.color)))
            return MakeUnexpected(CsgDeformViabilityReason::NonFiniteInput);
    }
    return {};
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

