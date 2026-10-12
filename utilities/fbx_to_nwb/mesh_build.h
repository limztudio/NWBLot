// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "source_mesh_streams.h"
#include "skin.h"

#include <core/common/log.h>
#include <global/simd/frame.h>
#include <global/mesh/triangle_area.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FbxMeshBuild final : NoCopy{
public:
    [[nodiscard]] static SIMDVectorSingle ToVector(const ufbx_vec3 value, const f32 w = 0.0f);
    [[nodiscard]] static PositionKey MakePositionKey(const SIMDVectorSingle position);
    [[nodiscard]] static SIMDVectorSingle BuildCornerOutputPositionVector(const ufbx_mesh& mesh, const ufbx_node& node, const ImportOptions& options, const bool wantsSkinning, const u32 cornerIndex);
    [[nodiscard]] static SIMDVectorSingle BuildCornerOutputNormalVector(const ufbx_mesh& mesh, const ufbx_matrix& normalToWorld, const ImportOptions& options, const bool wantsSkinning, const u32 cornerIndex);
    [[nodiscard]] static Expected<SIMDVectorSingle> BuildCornerOutputTangentVector(const ufbx_mesh& mesh, const ufbx_matrix& normalToWorld, const ImportOptions& options, const bool wantsSkinning, const u32 cornerIndex, const SIMDVectorSingle normal);
    [[nodiscard]] static bool IsFiniteSkinInfluence(const SIMDVectorSingle weights);
    [[nodiscard]] static bool IsFiniteSourceTriangleCorner(
        const SIMDVectorSingle position,
        const SIMDVectorSingle normal,
        const SIMDVectorSingle tangent,
        const SIMDVectorSingle uv0,
        const SIMDVectorSingle color,
        const bool hasTangent,
        const bool wantsSkinning,
        const SIMDVectorSingle skinWeights
    );
    template<typename VisitTriangle>
    [[nodiscard]] static bool VisitTriangulatedMeshTriangles(const ufbx_mesh& mesh, const bool flipWinding, UtilityVector<u32>& inOutTriangleIndices, VisitTriangle&& visitTriangle);
    [[nodiscard]] static Expected<PositionNormalMap> BuildSmoothPositionNormals(const ufbx_mesh& mesh, const ufbx_node& node, const ImportOptions& options, const bool wantsSkinning, UtilityVector<u32>& inOutTriangleIndices);
    static bool AppendInstanceMesh(
        const MeshInstance& instance,
        const ImportOptions& options,
        const bool wantsSkinning,
        const NormalMode::Enum normalMode,
        const Vec4& defaultColor,
        UtilityVector<u32>& inOutTriangleIndices,
        SourceMeshBuildContext& inOutMesh,
        FbxSkinDetail::ExportContext& inOutSkinContext,
        bool& inOutSawVertexColors,
        bool& inOutSawVertexUvs,
        bool& inOutUsedDefaultUvs
    );
    static Expected<usize> EstimateSelectedTriangleCorners(const UtilityVector<MeshInstance>& instances, const UtilityVector<usize>& selection);


public:
    FbxMeshBuild() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VisitTriangle>
[[nodiscard]] bool FbxMeshBuild::VisitTriangulatedMeshTriangles(const ufbx_mesh& mesh, const bool flipWinding, UtilityVector<u32>& inOutTriangleIndices, VisitTriangle&& visitTriangle){
    if(!FbxSourceMeshStreams::EnsureTriangleIndexScratchCapacity(mesh, inOutTriangleIndices))
        return false;
    for(usize faceIndex = 0u; faceIndex < mesh.num_faces; ++faceIndex){
        const ufbx_face face = mesh.faces.data[faceIndex];
        if(face.num_indices < s_TriangleIndexCount)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(inOutTriangleIndices.data(), inOutTriangleIndices.size(), &mesh, face);

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            u32 cornerIndices[s_TriangleIndexCount] = { inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 0u], inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 1u], inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 2u], };
            if(flipWinding)
                Swap(cornerIndices[1], cornerIndices[2]);

            for([[maybe_unused]] const u32 cornerIndex : cornerIndices){
                NWB_ASSERT(cornerIndex < mesh.vertex_indices.count);
            }

            if(!visitTriangle(cornerIndices))
                return false;
        }
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

