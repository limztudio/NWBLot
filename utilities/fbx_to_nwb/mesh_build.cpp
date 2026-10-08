// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "mesh_build.h"

#include <core/alloc/scratch.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SIMDVector FbxMeshBuild::ToVector(const ufbx_vec3 value, const f32 w){
    return VectorSet(
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
        static_cast<f32>(value.z),
        w
    );
}


PositionKey FbxMeshBuild::MakePositionKey(const SIMDVector position){
    return PositionKey{
        FloatHashBits(VectorGetX(position)),
        FloatHashBits(VectorGetY(position)),
        FloatHashBits(VectorGetZ(position)),
    };
}


SIMDVector FbxMeshBuild::BuildCornerOutputPositionVector(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex
){
    ufbx_vec3 position = {};
    if(options.bakeTransforms){
        position = ufbx_get_vertex_vec3(
            wantsSkinning ? &mesh.vertex_position : &mesh.skinned_position,
            cornerIndex
        );
        if(wantsSkinning || mesh.skinned_is_local)
            position = ufbx_transform_position(&node.geometry_to_world, position);
    }
    else{
        position = ufbx_get_vertex_vec3(&mesh.vertex_position, cornerIndex);
    }

    return VectorScale(ToVector(position), static_cast<f32>(options.scale));
}


SIMDVector FbxMeshBuild::BuildCornerOutputNormalVector(
    const ufbx_mesh& mesh,
    const ufbx_matrix& normalToWorld,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex
){
    ufbx_vec3 normal = {};
    if(options.bakeTransforms){
        normal = ufbx_get_vertex_vec3(
            wantsSkinning ? &mesh.vertex_normal : &mesh.skinned_normal,
            cornerIndex
        );
        if(wantsSkinning || mesh.skinned_is_local)
            normal = ufbx_transform_direction(&normalToWorld, normal);
    }
    else{
        normal = ufbx_get_vertex_vec3(&mesh.vertex_normal, cornerIndex);
    }

    const auto outputNormal = Vector3TryNormalize(ToVector(normal));
    return outputNormal ? *outputNormal : VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
}


Expected<SIMDVector> FbxMeshBuild::BuildCornerOutputTangentVector(
    const ufbx_mesh& mesh,
    const ufbx_matrix& normalToWorld,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex,
    const SIMDVector normal
){
    if(!mesh.vertex_tangent.exists)
        return MakeUnexpected(Failure{});

    ufbx_vec3 tangent = ufbx_get_vertex_vec3(&mesh.vertex_tangent, cornerIndex);
    if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
        tangent = ufbx_transform_direction(&normalToWorld, tangent);

    const auto outputTangent = Vector3TryNormalize(ToVector(tangent));
    if(!outputTangent)
        return MakeUnexpected(Failure{});

    SIMDVector sign = s_SIMDOne;
    if(mesh.vertex_bitangent.exists){
        ufbx_vec3 bitangent = ufbx_get_vertex_vec3(&mesh.vertex_bitangent, cornerIndex);
        if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
            bitangent = ufbx_transform_direction(&normalToWorld, bitangent);

        const auto outputBitangent = Vector3TryNormalize(ToVector(bitangent));
        if(outputBitangent){
            const SIMDVector tangentSpaceBitangent = Vector3Cross(normal, *outputTangent);
            const SIMDVector bitangentDot = Vector3Dot(tangentSpaceBitangent, *outputBitangent);
            sign = VectorSelect(s_SIMDOne, s_SIMDNegativeOne, VectorLess(bitangentDot, VectorZero()));
        }
    }

    return VectorSelect(*outputTangent, sign, s_SIMDMaskW);
}


bool FbxMeshBuild::IsFiniteSkinInfluence(const SIMDVector weights){
    return VectorIsFinite(weights, VectorComponentMask::s_XYZW);
}


bool FbxMeshBuild::IsFiniteSourceTriangleCorner(
    const SIMDVector position,
    const SIMDVector normal,
    const SIMDVector tangent,
    const SIMDVector uv0,
    const SIMDVector color,
    const bool hasTangent,
    const bool wantsSkinning,
    const SIMDVector skinWeights
){
    if(
        !VectorIsFinite(position, VectorComponentMask::s_XYZ)
        || !VectorIsFinite(normal, VectorComponentMask::s_XYZ)
        || !VectorIsFinite(uv0, VectorComponentMask::s_XY)
        || !VectorIsFinite(color, VectorComponentMask::s_XYZW)
    )
        return false;
    if(
        hasTangent
        && !VectorIsFinite(tangent, VectorComponentMask::s_XYZW)
    )
        return false;
    return !wantsSkinning || IsFiniteSkinInfluence(skinWeights);
}


Expected<PositionNormalMap> FbxMeshBuild::BuildSmoothPositionNormals(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const bool wantsSkinning,
    UtilityVector<u32>& inOutTriangleIndices
){
    PositionNormalMap outNormals;
    outNormals.reserve(mesh.num_vertices);

    if(!VisitTriangulatedMeshTriangles(
        mesh,
        options.flipWinding,
        inOutTriangleIndices,
        [&](const u32 (&cornerIndices)[s_TriangleIndexCount]){
        SIMDVector positions[s_TriangleIndexCount] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < s_TriangleIndexCount; ++triangleCornerIndex){
            positions[triangleCornerIndex] = BuildCornerOutputPositionVector(
                mesh,
                node,
                options,
                wantsSkinning,
                cornerIndices[triangleCornerIndex]
            );
        }

        const TriangleAreaNormal64 areaNormal64 = BuildTriangleAreaNormal64(positions[0u], positions[1u], positions[2u]);
        const f64 areaLengthSquared = TriangleAreaNormalLengthSquared(areaNormal64);
        if(!IsFinite(areaLengthSquared) || areaLengthSquared <= options.triangleAreaLengthSquaredEpsilon)
            return true;

        const SIMDVector areaNormal = VectorSet(
            static_cast<f32>(areaNormal64.x),
            static_cast<f32>(areaNormal64.y),
            static_cast<f32>(areaNormal64.z),
            0.0f
        );
        for(const SIMDVector position : positions){
            const PositionKey key = MakePositionKey(position);
            auto result = outNormals.emplace(key, PositionNormalCalculation{ areaNormal });
            if(!result.second){
                PositionNormalCalculation& normal = result.first.value();
                normal.value = VectorAdd(normal.value, areaNormal);
            }
        }
        return true;
    }))
        return MakeUnexpected(Failure{});

    for(auto it = outNormals.begin(); it != outNormals.end(); ++it){
        const auto normal = Vector3TryNormalize(it.value().value);
        if(!normal){
            NWB_LOGGER_WARNING(NWB_TEXT("Mesh build: degenerate accumulated vertex normal left un-normalized"));
            continue;
        }
        it.value().value = *normal;
    }
    return outNormals;
}


bool FbxMeshBuild::AppendInstanceMesh(
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
){
    ufbx_mesh* mesh = instance.mesh;
    ufbx_node* node = instance.node;
    if(!mesh || !node){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh instance is missing mesh or node"));
        return false;
    }
    if(!mesh->vertex_position.exists){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh is missing positions"));
        return false;
    }
    if(normalMode == NormalMode::Imported && !mesh->vertex_normal.exists){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: imported normal mode requires mesh normals after ufbx import"));
        return false;
    }

    const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&node->geometry_to_world);
    const bool importUvs = mesh->vertex_uv.exists;
    const bool importColors = options.importColors && mesh->vertex_color.exists;
    const bool importTangents = normalMode == NormalMode::Imported && mesh->vertex_tangent.exists;
    const SIMDVector defaultColorVector = LoadFloat(defaultColor);
    ufbx_skin_deformer* skin = nullptr;
    UtilityVector<u16> clusterJoints;
    if(wantsSkinning){
        if(mesh->skin_deformers.count != 1u){
            NWB_LOGGER_ERROR(
                NWB_TEXT("Failed to build mesh: skinned mesh requires exactly one skin deformer per selected mesh")
            );
            return false;
        }
        skin = mesh->skin_deformers.data[0u];
        auto jointMap = FbxSkinDetail::BuildClusterJointMap(instance, options, skin, inOutSkinContext);
        if(!jointMap)
            return false;
        clusterJoints = Move(*jointMap);
    }

    PositionNormalMap smoothNormals;
    if(normalMode == NormalMode::Smooth){
        auto normals = BuildSmoothPositionNormals(*mesh, *node, options, wantsSkinning, inOutTriangleIndices);
        if(!normals)
            return false;
        smoothNormals = Move(*normals);
    }

    return VisitTriangulatedMeshTriangles(
        *mesh,
        options.flipWinding,
        inOutTriangleIndices,
        [&](const u32 (&cornerIndices)[s_TriangleIndexCount]){
        SourceTriangleCorner triangleCorners[s_TriangleIndexCount] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < s_TriangleIndexCount; ++triangleCornerIndex){
            const u32 cornerIndex = cornerIndices[triangleCornerIndex];
            const u32 logicalVertex = mesh->vertex_indices.data[cornerIndex];

            SourceTriangleCorner corner;
            const SIMDVector position = BuildCornerOutputPositionVector(*mesh, *node, options, wantsSkinning, cornerIndex);
            StoreFloat(position, corner.position);

            SIMDVector normal = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            if(normalMode == NormalMode::Imported){
                normal = BuildCornerOutputNormalVector(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);
            }
            else if(normalMode == NormalMode::Smooth){
                auto foundNormal = smoothNormals.find(MakePositionKey(position));
                if(foundNormal == smoothNormals.end()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
                const auto generatedNormal = Vector3TryNormalize(foundNormal.value().value);
                if(!generatedNormal){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
                normal = *generatedNormal;
            }
            StoreFloat(normal, corner.normal);

            SIMDVector uv0 = VectorZero();
            if(importUvs){
                const ufbx_vec2 sourceUv0 = ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex);
                uv0 = VectorSet(
                    static_cast<f32>(sourceUv0.x),
                    static_cast<f32>(sourceUv0.y),
                    0.0f,
                    0.0f
                );
                inOutSawVertexUvs = true;
            }
            else{
                inOutUsedDefaultUvs = true;
            }
            StoreFloat(uv0, corner.uv0);

            SIMDVector color = defaultColorVector;
            if(importColors){
                const ufbx_vec4 sourceColor = ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex);
                color = VectorSet(
                    static_cast<f32>(sourceColor.x),
                    static_cast<f32>(sourceColor.y),
                    static_cast<f32>(sourceColor.z),
                    static_cast<f32>(sourceColor.w)
                );
                inOutSawVertexColors = true;
            }
            StoreFloat(color, corner.color);

            SIMDVector tangent = VectorZero();
            if(importTangents){
                const auto importedTangent = BuildCornerOutputTangentVector(
                    *mesh,
                    normalToWorld,
                    options,
                    wantsSkinning,
                    cornerIndex,
                    normal
                );
                corner.hasTangent = importedTangent.has_value();
                if(importedTangent){
                    tangent = *importedTangent;
                    StoreFloat(tangent, corner.tangent);
                }
            }

            SIMDVector skinWeights = VectorZero();
            if(wantsSkinning){
                const auto influence = FbxSkinDetail::BuildInfluence(skin, clusterJoints, logicalVertex);
                if(!influence)
                    return false;
                corner.skin = influence->influence;
                skinWeights = influence->weights;
                StoreFloat(skinWeights, corner.skin.weight);
            }

            if(!IsFiniteSourceTriangleCorner(
                position,
                normal,
                tangent,
                uv0,
                color,
                corner.hasTangent,
                wantsSkinning,
                skinWeights
            )){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh contains non-finite vertex data"));
                return false;
            }

            triangleCorners[triangleCornerIndex] = corner;
        }

        if(!::TriangleHasArea(
            LoadFloat(triangleCorners[0u].position),
            LoadFloat(triangleCorners[1u].position),
            LoadFloat(triangleCorners[2u].position),
            options.triangleAreaLengthSquaredEpsilon
        ))
            return true;

        if(normalMode == NormalMode::Regenerate){
            const TriangleAreaNormal64 faceNormal64 = BuildTriangleAreaNormal64(
                LoadFloat(triangleCorners[0u].position),
                LoadFloat(triangleCorners[1u].position),
                LoadFloat(triangleCorners[2u].position)
            );
            const SIMDVector faceNormal = VectorSet(
                static_cast<f32>(faceNormal64.x),
                static_cast<f32>(faceNormal64.y),
                static_cast<f32>(faceNormal64.z),
                0.0f
            );
            const auto normalizedFaceNormal = Vector3TryNormalize(faceNormal);
            if(!normalizedFaceNormal){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to regenerate mesh face normal"));
                return false;
            }
            for(SourceTriangleCorner& corner : triangleCorners)
                StoreFloat(*normalizedFaceNormal, corner.normal);
        }

        for(const SourceTriangleCorner& corner : triangleCorners){
            const auto vertexRefIndex = FbxSourceMeshStreams::InternSourceCorner(inOutMesh, corner, wantsSkinning);
            if(!vertexRefIndex)
                return false;
            inOutMesh.mesh.indices.push_back(*vertexRefIndex);
        }
        return true;
    });
}


Expected<usize> FbxMeshBuild::EstimateSelectedTriangleCorners(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection
){
    usize outTriangleCorners = 0u;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected mesh index is out of range"));
            return MakeUnexpected(Failure{});
        }

        const ufbx_mesh* const mesh = instances[instanceIndex].mesh;
        if(!mesh){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected mesh instance is missing mesh"));
            return MakeUnexpected(Failure{});
        }
        if(mesh->num_triangles > (Limit<usize>::s_Max - outTriangleCorners) / s_TriangleIndexCount){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes have too many triangle corners"));
            return MakeUnexpected(Failure{});
        }

        outTriangleCorners += static_cast<usize>(mesh->num_triangles) * s_TriangleIndexCount;
    }
    return outTriangleCorners;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

