// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename VisitTriangle>
[[nodiscard]] bool VisitTriangulatedMeshTriangles(
    const ufbx_mesh& mesh,
    const bool flipWinding,
    UtilityVector<u32>& inOutTriangleIndices,
    VisitTriangle&& visitTriangle
){
    if(!EnsureTriangleIndexScratchCapacity(mesh, inOutTriangleIndices))
        return false;
    for(usize faceIndex = 0; faceIndex < mesh.num_faces; ++faceIndex){
        const ufbx_face face = mesh.faces.data[faceIndex];
        if(face.num_indices < 3u)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(
            inOutTriangleIndices.data(),
            inOutTriangleIndices.size(),
            &mesh,
            face
        );

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            u32 cornerIndices[3] = {
                inOutTriangleIndices[triangleIndex * 3u + 0u],
                inOutTriangleIndices[triangleIndex * 3u + 1u],
                inOutTriangleIndices[triangleIndex * 3u + 2u],
            };
            if(flipWinding)
                Swap(cornerIndices[1], cornerIndices[2]);

            for(const u32 cornerIndex : cornerIndices){
                NWB_ASSERT(cornerIndex < mesh.vertex_indices.count);
            }

            if(!visitTriangle(cornerIndices))
                return false;
        }
    }
    return true;
}

[[nodiscard]] bool BuildSmoothPositionNormals(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const bool wantsSkinning,
    UtilityVector<u32>& inOutTriangleIndices,
    PositionNormalMap& outNormals
){
    outNormals.clear();
    outNormals.reserve(mesh.num_vertices);

    if(!VisitTriangulatedMeshTriangles(mesh, options.flipWinding, inOutTriangleIndices, [&](const u32 (&cornerIndices)[3]){
        Vec3 positions[3] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
            StoreFloat(BuildCornerOutputPositionVector(
                mesh,
                node,
                options,
                wantsSkinning,
                cornerIndices[triangleCornerIndex]
            ), &positions[triangleCornerIndex]);
        }

        const TriangleAreaNormal64 areaNormal64 = BuildTriangleAreaNormal64(positions[0u], positions[1u], positions[2u]);
        const f64 areaLengthSquared = TriangleAreaNormalLengthSquared(areaNormal64);
        if(!IsFinite(areaLengthSquared) || areaLengthSquared <= options.triangleAreaLengthSquaredEpsilon)
            return true;

        const Vec3 areaNormal{
            static_cast<f32>(areaNormal64.x),
            static_cast<f32>(areaNormal64.y),
            static_cast<f32>(areaNormal64.z),
        };
        for(const Vec3& position : positions){
            const PositionKey key = MakePositionKey(position);
            auto result = outNormals.emplace(key, areaNormal);
            if(!result.second){
                Vec3& normal = result.first.value();
                StoreFloat(VectorAdd(LoadFloat(normal), LoadFloat(areaNormal)), &normal);
            }
        }
        return true;
    }))
        return false;

    for(auto it = outNormals.begin(); it != outNormals.end(); ++it){
        SIMDVector normal;
        if(!Normalize(LoadFloat(it.value()), normal)){
            NWB_LOGGER_WARNING(NWB_TEXT("Mesh build: degenerate accumulated vertex normal left un-normalized"));
            continue;
        }
        StoreFloat(normal, &it.value());
    }
    return true;
}

bool AppendInstanceMesh(
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
    NWB_ASSERT(mesh != nullptr);
    NWB_ASSERT(node != nullptr);
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
    ufbx_skin_deformer* skin = nullptr;
    UtilityVector<u16> clusterJoints;
    if(wantsSkinning){
        if(mesh->skin_deformers.count != 1u){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: skinned mesh requires exactly one skin deformer per selected mesh"));
            return false;
        }
        skin = mesh->skin_deformers.data[0u];
        if(!FbxSkinDetail::BuildClusterJointMap(instance, options, skin, inOutSkinContext, clusterJoints))
            return false;
    }

    PositionNormalMap smoothNormals;
    if(normalMode == NormalMode::Smooth && !BuildSmoothPositionNormals(*mesh, *node, options, wantsSkinning, inOutTriangleIndices, smoothNormals))
        return false;

    return VisitTriangulatedMeshTriangles(*mesh, options.flipWinding, inOutTriangleIndices, [&](const u32 (&cornerIndices)[3]){
        SourceTriangleCorner triangleCorners[3] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
            const u32 cornerIndex = cornerIndices[triangleCornerIndex];
            const u32 logicalVertex = mesh->vertex_indices.data[cornerIndex];

            SourceTriangleCorner corner;
            const SIMDVector position = BuildCornerOutputPositionVector(*mesh, *node, options, wantsSkinning, cornerIndex);
            StoreFloat(position, &corner.position);

            SIMDVector normal = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
            if(normalMode == NormalMode::Imported){
                normal = BuildCornerOutputNormalVector(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);
            }
            else if(normalMode == NormalMode::Smooth){
                auto foundNormal = smoothNormals.find(MakePositionKey(corner.position));
                if(foundNormal == smoothNormals.end()){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
                if(!Normalize(LoadFloat(foundNormal.value()), normal)){
                    NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate smooth mesh normal"));
                    return false;
                }
            }
            StoreFloat(normal, &corner.normal);

            if(importUvs){
                corner.uv0 = ToVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex));
                inOutSawVertexUvs = true;
            }
            else{
                inOutUsedDefaultUvs = true;
            }

            corner.color = defaultColor;
            if(importColors){
                corner.color = ToVec4(ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex));
                inOutSawVertexColors = true;
            }

            if(importTangents){
                SIMDVector tangent;
                corner.hasTangent = BuildCornerOutputTangentVector(
                    *mesh,
                    normalToWorld,
                    options,
                    wantsSkinning,
                    cornerIndex,
                    normal,
                    tangent
                );
                if(corner.hasTangent)
                    StoreFloat(tangent, &corner.tangent);
            }

            if(wantsSkinning){
                if(!FbxSkinDetail::BuildInfluence(skin, clusterJoints, logicalVertex, corner.skin))
                    return false;
            }

            if(!IsFiniteSourceTriangleCorner(corner, wantsSkinning)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh contains non-finite vertex data"));
                return false;
            }

            triangleCorners[triangleCornerIndex] = corner;
        }

        if(!TriangleHasArea(triangleCorners, options.triangleAreaLengthSquaredEpsilon))
            return true;

        if(normalMode == NormalMode::Regenerate){
            const TriangleAreaNormal64 faceNormal64 = BuildTriangleAreaNormal64(
                triangleCorners[0u].position,
                triangleCorners[1u].position,
                triangleCorners[2u].position
            );
            Vec3 faceNormal{
                static_cast<f32>(faceNormal64.x),
                static_cast<f32>(faceNormal64.y),
                static_cast<f32>(faceNormal64.z),
            };
            SIMDVector normalizedFaceNormal;
            if(!Normalize(LoadFloat(faceNormal), normalizedFaceNormal)){
                NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to regenerate mesh face normal"));
                return false;
            }
            for(SourceTriangleCorner& corner : triangleCorners)
                StoreFloat(normalizedFaceNormal, &corner.normal);
        }

        for(const SourceTriangleCorner& corner : triangleCorners){
            u32 vertexRefIndex = 0u;
            if(!InternSourceCorner(inOutMesh, corner, wantsSkinning, vertexRefIndex))
                return false;
            inOutMesh.mesh.indices.push_back(vertexRefIndex);
        }
        return true;
    });
}

bool EstimateSelectedTriangleCorners(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    usize& outTriangleCorners
){
    outTriangleCorners = 0u;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected mesh index is out of range"));
            return false;
        }

        const ufbx_mesh* const mesh = instances[instanceIndex].mesh;
        NWB_ASSERT(mesh != nullptr);
        if(mesh->num_triangles > (Limit<usize>::s_Max - outTriangleCorners) / 3u){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: selected meshes have too many triangle corners"));
            return false;
        }

        outTriangleCorners += static_cast<usize>(mesh->num_triangles) * 3u;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

