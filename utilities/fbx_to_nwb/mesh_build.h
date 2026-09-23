// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"
#include "source_mesh_streams.h"

#include "skin.h"

#include <core/common/log.h>
#include <global/math/frame.h>
#include <global/mesh/triangle_area.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// FBX corner conversion and triangulated mesh assembly.


class FbxMeshBuild final : NoCopy{
public:
    [[nodiscard]] static SIMDVector ToVector(const ufbx_vec3 value, const f32 w = 0.0f);
    [[nodiscard]] static PositionKey MakePositionKey(const Vec3& position);
    [[nodiscard]] static PositionKey MakePositionKey(const SIMDVector position);
    [[nodiscard]] static SIMDVector BuildCornerOutputPositionVector(
        const ufbx_mesh& mesh,
        const ufbx_node& node,
        const ImportOptions& options,
        const bool wantsSkinning,
        const u32 cornerIndex
    );
    [[nodiscard]] static SIMDVector BuildCornerOutputNormalVector(
        const ufbx_mesh& mesh,
        const ufbx_matrix& normalToWorld,
        const ImportOptions& options,
        const bool wantsSkinning,
        const u32 cornerIndex
    );
    [[nodiscard]] static bool BuildCornerOutputTangentVector(
        const ufbx_mesh& mesh,
        const ufbx_matrix& normalToWorld,
        const ImportOptions& options,
        const bool wantsSkinning,
        const u32 cornerIndex,
        const SIMDVector normal,
        SIMDVector& outTangent
    );
    [[nodiscard]] static bool IsFiniteSkinInfluence(const SIMDVector weights);
    [[nodiscard]] static bool IsFiniteSourceTriangleCorner(
        const SIMDVector position,
        const SIMDVector normal,
        const SIMDVector tangent,
        const SIMDVector uv0,
        const SIMDVector color,
        const bool hasTangent,
        const bool wantsSkinning,
        const SIMDVector skinWeights
    );
    template<typename VisitTriangle>
    [[nodiscard]] static bool VisitTriangulatedMeshTriangles(
        const ufbx_mesh& mesh,
        const bool flipWinding,
        UtilityVector<u32>& inOutTriangleIndices,
        VisitTriangle&& visitTriangle
    );
    [[nodiscard]] static bool BuildSmoothPositionNormals(
        const ufbx_mesh& mesh,
        const ufbx_node& node,
        const ImportOptions& options,
        const bool wantsSkinning,
        UtilityVector<u32>& inOutTriangleIndices,
        PositionNormalMap& outNormals
    );
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
    static bool EstimateSelectedTriangleCorners(
        const UtilityVector<MeshInstance>& instances,
        const UtilityVector<usize>& selection,
        usize& outTriangleCorners
    );


public:
    FbxMeshBuild() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VisitTriangle>
[[nodiscard]] bool FbxMeshBuild::VisitTriangulatedMeshTriangles(
    const ufbx_mesh& mesh,
    const bool flipWinding,
    UtilityVector<u32>& inOutTriangleIndices,
    VisitTriangle&& visitTriangle
){
    if(!FbxSourceMeshStreams::EnsureTriangleIndexScratchCapacity(mesh, inOutTriangleIndices))
        return false;
    for(usize faceIndex = 0; faceIndex < mesh.num_faces; ++faceIndex){
        const ufbx_face face = mesh.faces.data[faceIndex];
        if(face.num_indices < s_TriangleIndexCount)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(
            inOutTriangleIndices.data(),
            inOutTriangleIndices.size(),
            &mesh,
            face
        );

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            u32 cornerIndices[s_TriangleIndexCount] = {
                inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 0u],
                inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 1u],
                inOutTriangleIndices[triangleIndex * s_TriangleIndexCount + 2u],
            };
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

