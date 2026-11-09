// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_build.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/simd/collision_triangle.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using PositionNormalMap = UtilityScratchMap<PositionKey, PositionNormalRecord>;

template<typename VisitTriangle>
[[nodiscard]] static b8 VisitTriangulatedMeshTriangles(const ufbx_mesh& mesh, const b8 flipWinding, VisitTriangle&& visitTriangle, UtilityScratchVector<u32>& inOutTriangleIndices){
    if(!FbxSourceMeshStreams::EnsureTriangleIndexScratchCapacity(mesh, inOutTriangleIndices))
        return false;
    for(usize faceIndex = 0u; faceIndex < mesh.num_faces; ++faceIndex){
        const ufbx_face face = mesh.faces.data[faceIndex];
        if(face.num_indices < Impl::s_MeshletTriangleIndexCount)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(inOutTriangleIndices.data(), inOutTriangleIndices.size(), &mesh, face);

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            const usize baseIndex = static_cast<usize>(triangleIndex) * Impl::s_MeshletTriangleIndexCount;
            Array<u32, Impl::s_MeshletTriangleIndexCount> cornerIndices = { inOutTriangleIndices[baseIndex + 0u], inOutTriangleIndices[baseIndex + 1u], inOutTriangleIndices[baseIndex + 2u] };
            if(flipWinding)
                Swap(cornerIndices[1u], cornerIndices[2u]);

#if defined(NWB_DEBUG)
            for(const u32 cornerIndex : cornerIndices)
                FOUNDATION_ASSERT(cornerIndex < mesh.vertex_indices.count);
#endif

            if(!visitTriangle(cornerIndices))
                return false;
        }
    }
    return true;
}

[[nodiscard]] static SIMDVectorSingle LoadCornerOutputPositionVector(const ufbx_mesh& mesh, const ufbx_node& node, const ImportOptions& options, const b8 wantsSkinning, const u32 cornerIndex)noexcept{
    ufbx_vec3 position = {};
    if(options.bakeTransforms){
        position = ufbx_get_vertex_vec3(wantsSkinning ? &mesh.vertex_position : &mesh.skinned_position, cornerIndex);
        if(wantsSkinning || mesh.skinned_is_local)
            position = ufbx_transform_position(&node.geometry_to_world, position);
    }
    else
        position = ufbx_get_vertex_vec3(&mesh.vertex_position, cornerIndex);

    return VectorScale(LoadUfbxVector(position), static_cast<f32>(options.scale));
}

[[nodiscard]] static Expected<SIMDVectorSingleResult> LoadCornerOutputNormalVector(const ufbx_mesh& mesh, const ufbx_matrix& normalToWorld, const ImportOptions& options, const b8 wantsSkinning, const u32 cornerIndex)noexcept{
    ufbx_vec3 normal = {};
    if(options.bakeTransforms){
        normal = ufbx_get_vertex_vec3(wantsSkinning ? &mesh.vertex_normal : &mesh.skinned_normal, cornerIndex);
        if(wantsSkinning || mesh.skinned_is_local)
            normal = ufbx_transform_direction(&normalToWorld, normal);
    }
    else
        normal = ufbx_get_vertex_vec3(&mesh.vertex_normal, cornerIndex);

    return Vector3TryNormalize(LoadUfbxVector(normal));
}

[[nodiscard]] static Expected<SIMDVectorSingleResult> LoadCornerOutputTangentVector(const ufbx_mesh& mesh, const ufbx_matrix& geometryToWorld, const ImportOptions& options, const b8 wantsSkinning, const u32 cornerIndex, const SIMDVectorSingle normal)noexcept{
    if(!mesh.vertex_tangent.exists)
        return MakeUnexpected(Failure{});

    ufbx_vec3 tangent = ufbx_get_vertex_vec3(&mesh.vertex_tangent, cornerIndex);
    if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
        tangent = ufbx_transform_direction(&geometryToWorld, tangent);

    const auto outputTangent = Vector3TryNormalize(LoadUfbxVector(tangent));
    if(!outputTangent)
        return MakeUnexpected(Failure{});

    b8 hasBitangent = false;
    SIMDVectorSingle outputBitangentVec = VectorZero();
    if(mesh.vertex_bitangent.exists){
        ufbx_vec3 bitangent = ufbx_get_vertex_vec3(&mesh.vertex_bitangent, cornerIndex);
        if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
            bitangent = ufbx_transform_direction(&geometryToWorld, bitangent);

        const auto outputBitangent = Vector3TryNormalize(LoadUfbxVector(bitangent));
        if(outputBitangent){
            hasBitangent = true;
            outputBitangentVec = outputBitangent->value;
        }
    }

    return SIMDVectorSingleResult{ ResolveImportedFbxTangent(normal, outputTangent->value, hasBitangent, outputBitangentVec) };
}

[[nodiscard]] static Expected<PositionNormalMap> BuildSmoothPositionNormals(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const b8 wantsSkinning,
    Core::Alloc::ScratchArena& scratchArena,
    UtilityScratchVector<u32>& inOutTriangleIndices
){
    PositionNormalMap outNormals(scratchArena);
    outNormals.reserve(mesh.num_vertices);

    if(!VisitTriangulatedMeshTriangles(mesh, options.flipWinding, [&](const Array<u32, Impl::s_MeshletTriangleIndexCount>& cornerIndices){
        alignas(SIMDVectorSingle) Array<SIMDVectorSingle, Impl::s_MeshletTriangleIndexCount> positions = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < Impl::s_MeshletTriangleIndexCount; ++triangleCornerIndex)
            positions[triangleCornerIndex] = LoadCornerOutputPositionVector(mesh, node, options, wantsSkinning, cornerIndices[triangleCornerIndex]);

        const SIMDVectorDouble areaNormal64 = BuildTriangleAreaNormal64(positions[0u], positions[1u], positions[2u]);
        const f64 areaLengthSquared = VectorDoubleGetX(TriangleAreaNormalLengthSquared(areaNormal64));
        if(!HasUsableSourceTriangleArea(areaLengthSquared, options.triangleAreaLengthSquaredEpsilon))
            return true;

        const SIMDVectorSingle areaNormal = ConvertVectorDoubleToSingle(areaNormal64);
        for(const SIMDVectorSingle position : positions){
            const PositionKey key = MakePositionKey(position);
            auto result = outNormals.emplace(key, PositionNormalRecord{});
            PositionNormalRecord& normal = result.first.value();
            StoreFloat(result.second ? areaNormal : VectorAdd(LoadFloat(normal.value), areaNormal), normal.value);
        }
        return true;
    }, inOutTriangleIndices))
        return MakeUnexpected(Failure{});

    for(auto it = outNormals.begin(); it != outNormals.end(); ++it){
        const auto normal = Vector3TryNormalize(LoadFloat(it.value().value));
        if(!normal){
            NWB_LOGGER_WARNING(FOUNDATION_TEXT("Mesh build: degenerate accumulated vertex normal left un-normalized"));
            continue;
        }
        StoreFloat(normal->value, it.value().value);
    }
    return outNormals;
}

b8 FbxMeshBuild::AppendInstanceMesh(
    const MeshInstance& instance,
    const ImportOptions& options,
    const b8 wantsSkinning,
    const NormalMode::Enum normalMode,
    const Vec4& defaultColor,
    Core::Alloc::ScratchArena& scratchArena,
    UtilityScratchVector<u32>& inOutTriangleIndices,
    SourceMeshBuildContext& inOutMesh,
    FbxSkinDetail::ExportContext& inOutSkinContext,
    b8& inOutSawVertexColors,
    b8& inOutSawVertexUvs,
    b8& inOutUsedDefaultUvs
){
    ufbx_mesh* mesh = instance.mesh;
    ufbx_node* node = instance.node;
    if(!mesh || !node){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: mesh instance is missing mesh or node"));
        return false;
    }
    if(!ValidateFbxMeshAttributes(mesh->vertex_position.exists, normalMode == NormalMode::Imported, mesh->vertex_normal.exists))
        return false;

    const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&node->geometry_to_world);
    const b8 importUvs = mesh->vertex_uv.exists;
    const b8 importColors = options.importColors && mesh->vertex_color.exists;
    const b8 importTangents = normalMode == NormalMode::Imported && mesh->vertex_tangent.exists;
    const ufbx_skin_deformer* skin = nullptr;
    ::Vector<u16, Core::Alloc::ScratchArena> clusterJoints(scratchArena);
    if(wantsSkinning){
        if(!ValidateFbxSkinDeformerCount(mesh->skin_deformers.count))
            return false;
        skin = mesh->skin_deformers.data[0u];
        if(!skin){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: skinned mesh requires a skin deformer"));
            return false;
        }
        auto jointMap = FbxSkinDetail::BuildClusterJointMap(instance, options, *skin, scratchArena, inOutSkinContext);
        if(!jointMap)
            return false;
        clusterJoints = Move(*jointMap);
    }

    PositionNormalMap smoothNormals(scratchArena);
    if(normalMode == NormalMode::Smooth){
        auto normals = BuildSmoothPositionNormals(*mesh, *node, options, wantsSkinning, scratchArena, inOutTriangleIndices);
        if(!normals)
            return false;
        smoothNormals = Move(*normals);
    }

    return VisitTriangulatedMeshTriangles(*mesh, options.flipWinding, [&](const Array<u32, Impl::s_MeshletTriangleIndexCount>& cornerIndices){
        Array<SourceTriangleCorner, Impl::s_MeshletTriangleIndexCount> triangleCorners = {};
        alignas(SIMDVectorSingle) Array<SIMDVectorSingle, Impl::s_MeshletTriangleIndexCount> trianglePositions = {};
        alignas(SIMDVectorSingle) Array<SIMDVectorSingle, Impl::s_MeshletTriangleIndexCount> triangleNormals = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < Impl::s_MeshletTriangleIndexCount; ++triangleCornerIndex){
            const u32 cornerIndex = cornerIndices[triangleCornerIndex];
            const u32 logicalVertex = mesh->vertex_indices.data[cornerIndex];

            SourceTriangleCorner corner;
            const SIMDVectorSingle position = LoadCornerOutputPositionVector(*mesh, *node, options, wantsSkinning, cornerIndex);
            trianglePositions[triangleCornerIndex] = position;

            SIMDVectorSingle normal = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            if(normalMode == NormalMode::Imported){
                const auto importedNormal = LoadCornerOutputNormalVector(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);
                if(!importedNormal){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: imported normal is invalid"));
                    return false;
                }
                normal = importedNormal->value;
            }
            else if(normalMode == NormalMode::Smooth){
                auto foundNormal = smoothNormals.find(MakePositionKey(position));
                if(foundNormal == smoothNormals.end()){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
                const auto generatedNormal = Vector3TryNormalize(LoadFloat(foundNormal.value().value));
                if(!generatedNormal){
                    NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
                normal = generatedNormal->value;
            }
            triangleNormals[triangleCornerIndex] = normal;

            if(importUvs){
                const ufbx_vec2 sourceUv0 = ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex);
                corner.uv0 = Vec2(static_cast<f32>(sourceUv0.x), static_cast<f32>(sourceUv0.y));
                inOutSawVertexUvs = true;
            }
            else
                inOutUsedDefaultUvs = true;

            if(importColors){
                const ufbx_vec4 sourceColor = ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex);
                corner.color = Vec4(static_cast<f32>(sourceColor.x), static_cast<f32>(sourceColor.y), static_cast<f32>(sourceColor.z), static_cast<f32>(sourceColor.w));
                inOutSawVertexColors = true;
            }
            else
                corner.color = defaultColor;

            if(importTangents){
                const auto importedTangent = LoadCornerOutputTangentVector(*mesh, node->geometry_to_world, options, wantsSkinning, cornerIndex, normal);
                corner.hasTangent = importedTangent.has_value();
                if(importedTangent)
                    StoreFloat(importedTangent->value, corner.tangent);
            }

            if(wantsSkinning){
                const auto influence = FbxSkinDetail::LoadInfluence(*skin, clusterJoints, logicalVertex);
                if(!influence)
                    return false;
                corner.skin = *influence;
            }

            if(!IsFiniteSourceTriangleCornerValues(position, normal, corner, wantsSkinning)){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: mesh contains non-finite vertex data"));
                return false;
            }

            triangleCorners[triangleCornerIndex] = corner;
        }

        if(!::TriangleHasArea(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u], options.triangleAreaLengthSquaredEpsilon))
            return true;

        if(normalMode == NormalMode::Regenerate){
            const SIMDVectorDouble faceNormal64 = BuildTriangleAreaNormal64(trianglePositions[0u], trianglePositions[1u], trianglePositions[2u]);
            const SIMDVectorSingle faceNormal = ConvertVectorDoubleToSingle(faceNormal64);
            const auto normalizedFaceNormal = Vector3TryNormalize(faceNormal);
            if(!normalizedFaceNormal){
                NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: failed to regenerate mesh face normal"));
                return false;
            }
            for(SIMDVectorSingle& normal : triangleNormals)
                normal = normalizedFaceNormal->value;
        }

        for(usize triangleCornerIndex = 0u; triangleCornerIndex < Impl::s_MeshletTriangleIndexCount; ++triangleCornerIndex){
            SourceTriangleCorner& corner = triangleCorners[triangleCornerIndex];
            StoreFloat(trianglePositions[triangleCornerIndex], corner.position);
            StoreFloat(triangleNormals[triangleCornerIndex], corner.normal);
            const auto vertexRefIndex = FbxSourceMeshStreams::InternSourceCorner(corner, wantsSkinning, inOutMesh);
            if(!vertexRefIndex)
                return false;
            inOutMesh.mesh.indices.push_back(*vertexRefIndex);
        }
        return true;
    }, inOutTriangleIndices);
}

Expected<usize> FbxMeshBuild::EstimateSelectedTriangleCorners(const Span<const MeshInstance> instances, const Span<const usize> selection){
    usize outTriangleCorners = 0u;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: selected mesh index is out of range"));
            return MakeUnexpected(Failure{});
        }

        const ufbx_mesh* const mesh = instances[instanceIndex].mesh;
        if(!mesh){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("Failed to build mesh: selected mesh instance is missing mesh"));
            return MakeUnexpected(Failure{});
        }
        if(!TryAccumulateFbxSelectedTriangleCorners(static_cast<usize>(mesh->num_triangles), outTriangleCorners))
            return MakeUnexpected(Failure{});
    }
    return outTriangleCorners;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

