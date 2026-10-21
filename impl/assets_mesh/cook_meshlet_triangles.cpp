// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_meshlet_triangles.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVectorSingle MeshletTriangleGeometry::MakeMeshletPositionVector(const SIMDVectorSingle position)noexcept{
    return VectorSetW(position, 0.0f);
}


MeshletTriangleVectors MeshletTriangleGeometry::MakeMeshletTriangleVectors(const SIMDVectorSingle position0, const SIMDVectorSingle position1, const SIMDVectorSingle position2, const SIMDVectorSingle centroid, const SIMDVectorSingle areaNormal)noexcept{
    return MeshletTriangleVectors{ { position0, position1, position2, }, centroid, areaNormal };
}


SIMDVectorSingle MeshletTriangleGeometry::NormalizeMeshletDirectionOrZero(const SIMDVectorSingle value)noexcept{
    return Vector3NormalizeOr(value, VectorZero(), ::s_FrameDirectionEpsilon);
}


Expected<u32> MeshletTriangleGeometry::FindNextUnvisitedMeshletTriangle(const MeshletTrianglePrecompute& trianglePrecompute, const usize searchOffset)noexcept{
    for(usize triangleIndex = searchOffset; triangleIndex < trianglePrecompute.triangles.size(); ++triangleIndex){
        if(trianglePrecompute.visitedTriangles[triangleIndex] != 0u)
            continue;

        return static_cast<u32>(triangleIndex);
    }

    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

