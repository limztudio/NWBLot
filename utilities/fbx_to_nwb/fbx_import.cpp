// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_import{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString FromUfbxString(const ufbx_string value){
    if(!value.data || value.length == 0u)
        return {};
    return AString(value.data, value.length);
}

char ToLowerAscii(const char value){
    if(value >= 'A' && value <= 'Z')
        return static_cast<char>(value - 'A' + 'a');
    return value;
}

AStringView UfbxStringView(const ufbx_string value){
    if(!value.data || value.length == 0u)
        return {};
    return AStringView(value.data, value.length);
}

bool NormalizedAsciiEqual(const ufbx_string value, const AStringView normalized){
    const AStringView text = UfbxStringView(value);
    if(text.size() != normalized.size())
        return false;

    for(usize i = 0u; i < normalized.size(); ++i){
        if(ToLowerAscii(text[i]) != normalized[i])
            return false;
    }
    return true;
}

bool NormalizedAsciiContains(const ufbx_string value, const AStringView normalized){
    const AStringView text = UfbxStringView(value);
    if(normalized.empty())
        return true;
    if(text.size() < normalized.size())
        return false;

    const usize lastBegin = text.size() - normalized.size();
    for(usize begin = 0u; begin <= lastBegin; ++begin){
        bool matched = true;
        for(usize i = 0u; i < normalized.size(); ++i){
            if(ToLowerAscii(text[begin + i]) != normalized[i]){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}

AString MeshDisplayName(const MeshInstance& instance){
    AString nodeName = FromUfbxString(instance.node->name);
    AString meshName = FromUfbxString(instance.mesh->name);
    if(nodeName.empty())
        nodeName = "<unnamed node>";
    if(meshName.empty())
        meshName = "<unnamed mesh>";

    AStringStream out;
    out << "[" << instance.index << "] node=\"" << nodeName << "\" mesh=\"" << meshName
        << "\" triangles=" << instance.mesh->num_triangles;
    if(!instance.node->visible)
        out << " hidden";
    return out.str();
}

AString FormatUfbxError(const ufbx_error& error){
    char buffer[4096] = {};
    ufbx_format_error(buffer, sizeof(buffer), &error);
    return buffer;
}

bool ParseIndexSelector(const AString& text, usize& outIndex){
    const AString trimmed = Trim(text);
    if(trimmed.empty())
        return false;

    u64 parsed = 0u;
    if(!ParseU64(trimmed, parsed))
        return false;
    if(parsed > static_cast<u64>(Limit<usize>::s_Max))
        return false;

    outIndex = static_cast<usize>(parsed);
    return true;
}

Vec3 ToVec3(const ufbx_vec3 value){
    return Vec3{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
        static_cast<f32>(value.z),
    };
}

Vec2 ToVec2(const ufbx_vec2 value){
    return Vec2{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
    };
}

Vec4 ToVec4(const ufbx_vec4 value){
    return Vec4{
        static_cast<f32>(value.x),
        static_cast<f32>(value.y),
        static_cast<f32>(value.z),
        static_cast<f32>(value.w),
    };
}

struct FlatGeometryVertex{
    GeometryVertex vertex;
    GeometrySkinInfluence skin;
};
static_assert(IsTriviallyCopyable_V<FlatGeometryVertex>);

struct SkinExportContext{
    UtilityVector<ufbx_node*> joints;
    UtilityVector<GeometryJointMatrix> inverseBindMatrices;
};

struct PositionKey{
    u32 x = 0u;
    u32 y = 0u;
    u32 z = 0u;
};

struct PositionKeyHasher{
    usize operator()(const PositionKey& key)const{
        usize seed = Hasher<u32>{}(key.x);
        seed ^= Hasher<u32>{}(key.y) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        seed ^= Hasher<u32>{}(key.z) + 0x9e3779b9u + (seed << 6u) + (seed >> 2u);
        return seed;
    }
};

struct PositionKeyEqual{
    bool operator()(const PositionKey& lhs, const PositionKey& rhs)const{
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
};

using PositionNormalMap = HashMap<PositionKey, Vec3, PositionKeyHasher, PositionKeyEqual>;

[[nodiscard]] u32 FloatPositionBits(f32 value){
    if(value == 0.0f)
        value = 0.0f;

    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

[[nodiscard]] PositionKey MakePositionKey(const Vec3& position){
    return PositionKey{
        FloatPositionBits(position.x),
        FloatPositionBits(position.y),
        FloatPositionBits(position.z),
    };
}

[[nodiscard]] bool TriangleHasArea(
    const FlatGeometryVertex (&vertices)[3],
    const f64 triangleAreaLengthSquaredEpsilon
){
    const Vec3& a = vertices[0u].vertex.position;
    const Vec3& b = vertices[1u].vertex.position;
    const Vec3& c = vertices[2u].vertex.position;
    const f64 abX = static_cast<f64>(b.x) - static_cast<f64>(a.x);
    const f64 abY = static_cast<f64>(b.y) - static_cast<f64>(a.y);
    const f64 abZ = static_cast<f64>(b.z) - static_cast<f64>(a.z);
    const f64 acX = static_cast<f64>(c.x) - static_cast<f64>(a.x);
    const f64 acY = static_cast<f64>(c.y) - static_cast<f64>(a.y);
    const f64 acZ = static_cast<f64>(c.z) - static_cast<f64>(a.z);
    const f64 crossX = abY * acZ - abZ * acY;
    const f64 crossY = abZ * acX - abX * acZ;
    const f64 crossZ = abX * acY - abY * acX;
    const f64 areaLengthSquared = crossX * crossX + crossY * crossY + crossZ * crossZ;
    return areaLengthSquared > triangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] Vec3 TriangleAreaNormal(const Vec3& a, const Vec3& b, const Vec3& c){
    const f64 abX = static_cast<f64>(b.x) - static_cast<f64>(a.x);
    const f64 abY = static_cast<f64>(b.y) - static_cast<f64>(a.y);
    const f64 abZ = static_cast<f64>(b.z) - static_cast<f64>(a.z);
    const f64 acX = static_cast<f64>(c.x) - static_cast<f64>(a.x);
    const f64 acY = static_cast<f64>(c.y) - static_cast<f64>(a.y);
    const f64 acZ = static_cast<f64>(c.z) - static_cast<f64>(a.z);
    return Vec3{
        static_cast<f32>(abY * acZ - abZ * acY),
        static_cast<f32>(abZ * acX - abX * acZ),
        static_cast<f32>(abX * acY - abY * acX),
    };
}

[[nodiscard]] f64 LengthSquared(const Vec3& value){
    return
        static_cast<f64>(value.x) * static_cast<f64>(value.x)
        + static_cast<f64>(value.y) * static_cast<f64>(value.y)
        + static_cast<f64>(value.z) * static_cast<f64>(value.z)
    ;
}

[[nodiscard]] Vec3 LoadCornerOutputPosition(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex){
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

    Vec3 outputPosition = ToVec3(position);
    outputPosition.x = static_cast<f32>(static_cast<f64>(outputPosition.x) * options.scale);
    outputPosition.y = static_cast<f32>(static_cast<f64>(outputPosition.y) * options.scale);
    outputPosition.z = static_cast<f32>(static_cast<f64>(outputPosition.z) * options.scale);
    return outputPosition;
}

[[nodiscard]] Vec3 LoadCornerOutputNormal(
    const ufbx_mesh& mesh,
    const ufbx_matrix& normalToWorld,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex){
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

    Vec3 outputNormal = ToVec3(normal);
    if(!Normalize(outputNormal))
        outputNormal = Vec3{ 0.0f, 0.0f, 1.0f };
    return outputNormal;
}

[[nodiscard]] bool BuildSmoothPositionNormals(
    const ufbx_mesh& mesh,
    const ufbx_node& node,
    const ImportOptions& options,
    const bool wantsSkinning,
    UtilityVector<u32>& inOutTriangleIndices,
    PositionNormalMap& outNormals,
    AString& outError){
    outNormals.clear();
    outNormals.reserve(mesh.num_vertices);

    inOutTriangleIndices.resize(static_cast<usize>(mesh.max_face_triangles) * 3u);
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
            if(options.flipWinding)
                Swap(cornerIndices[1], cornerIndices[2]);

            Vec3 positions[3] = {};
            for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
                const u32 cornerIndex = cornerIndices[triangleCornerIndex];
                if(cornerIndex >= mesh.vertex_indices.count){
                    outError = "mesh corner references an out-of-range logical vertex";
                    return false;
                }

                positions[triangleCornerIndex] = LoadCornerOutputPosition(
                    mesh,
                    node,
                    options,
                    wantsSkinning,
                    cornerIndex
                );
            }

            const Vec3 areaNormal = TriangleAreaNormal(positions[0u], positions[1u], positions[2u]);
            const f64 areaLengthSquared = LengthSquared(areaNormal);
            if(!IsFinite(areaLengthSquared) || areaLengthSquared <= options.triangleAreaLengthSquaredEpsilon)
                continue;

            for(const Vec3& position : positions){
                const PositionKey key = MakePositionKey(position);
                auto result = outNormals.emplace(key, areaNormal);
                if(!result.second){
                    Vec3& normal = result.first.value();
                    normal.x += areaNormal.x;
                    normal.y += areaNormal.y;
                    normal.z += areaNormal.z;
                }
            }
        }
    }

    for(auto it = outNormals.begin(); it != outNormals.end(); ++it)
        static_cast<void>(Normalize(it.value()));
    return true;
}

ufbx_matrix MakeInverseUniformScaleMatrix(const f64 scale){
    ufbx_matrix matrix = {};
    const f64 inverseScale = 1.0 / scale;
    matrix.m00 = inverseScale;
    matrix.m11 = inverseScale;
    matrix.m22 = inverseScale;
    return matrix;
}

bool FiniteUfbxMatrix(const ufbx_matrix& matrix){
    for(usize i = 0u; i < 12u; ++i){
        if(!IsFinite(static_cast<f64>(matrix.v[i])))
            return false;
    }
    return true;
}

GeometryJointMatrix ToGeometryJointMatrix(const ufbx_matrix& matrix){
    GeometryJointMatrix result{};
    result.columns[0] = Vec4{
        static_cast<f32>(matrix.m00),
        static_cast<f32>(matrix.m10),
        static_cast<f32>(matrix.m20),
        0.0f,
    };
    result.columns[1] = Vec4{
        static_cast<f32>(matrix.m01),
        static_cast<f32>(matrix.m11),
        static_cast<f32>(matrix.m21),
        0.0f,
    };
    result.columns[2] = Vec4{
        static_cast<f32>(matrix.m02),
        static_cast<f32>(matrix.m12),
        static_cast<f32>(matrix.m22),
        0.0f,
    };
    result.columns[3] = Vec4{
        static_cast<f32>(matrix.m03),
        static_cast<f32>(matrix.m13),
        static_cast<f32>(matrix.m23),
        1.0f,
    };
    return result;
}

bool NearlyEqualJointMatrix(const GeometryJointMatrix& lhs, const GeometryJointMatrix& rhs){
    static constexpr f32 s_Epsilon = 0.0001f;
    for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex){
        const Vec4& a = lhs.columns[columnIndex];
        const Vec4& b = rhs.columns[columnIndex];
        if(
            Abs(a.x - b.x) > s_Epsilon
            || Abs(a.y - b.y) > s_Epsilon
            || Abs(a.z - b.z) > s_Epsilon
            || Abs(a.w - b.w) > s_Epsilon
        ){
            return false;
        }
    }
    return true;
}

AString NodeDisplayName(const ufbx_node* node){
    if(!node)
        return "<null>";
    AString name = FromUfbxString(node->name);
    if(name.empty())
        return "<unnamed node>";
    return name;
}

ufbx_matrix BuildGeometryFromOutputMatrix(const MeshInstance& instance, const ImportOptions& options){
    const ufbx_matrix scaleInverse = MakeInverseUniformScaleMatrix(options.scale);
    if(!options.bakeTransforms)
        return scaleInverse;

    const ufbx_matrix nodeGeometryToWorldInverse = ufbx_matrix_invert(&instance.node->geometry_to_world);
    return ufbx_matrix_mul(&nodeGeometryToWorldInverse, &scaleInverse);
}

ufbx_matrix BuildOutputInverseBindMatrix(
    const ufbx_skin_cluster& cluster,
    const MeshInstance& instance,
    const ImportOptions& options
){
    const ufbx_matrix geometryFromOutput = BuildGeometryFromOutputMatrix(instance, options);
    return ufbx_matrix_mul(&cluster.geometry_to_bone, &geometryFromOutput);
}

bool FindOrAddJoint(
    SkinExportContext& context,
    ufbx_skin_cluster* cluster,
    const ufbx_matrix& inverseBind,
    u16& outJoint,
    AString& outError
){
    outJoint = 0u;
    if(!cluster || !cluster->bone_node){
        outError = "skin cluster is missing a bone node";
        return false;
    }
    if(!FiniteUfbxMatrix(inverseBind) || Abs(static_cast<f64>(ufbx_matrix_determinant(&inverseBind))) <= 0.00000001){
        outError = "skin cluster inverse bind matrix is not finite and invertible";
        return false;
    }

    const GeometryJointMatrix convertedMatrix = ToGeometryJointMatrix(inverseBind);
    for(usize jointIndex = 0u; jointIndex < context.joints.size(); ++jointIndex){
        if(context.joints[jointIndex] != cluster->bone_node)
            continue;
        if(!NearlyEqualJointMatrix(context.inverseBindMatrices[jointIndex], convertedMatrix)){
            outError = "selected meshes bind skeleton joint \"" + NodeDisplayName(cluster->bone_node)
                + "\" with different inverse bind matrices";
            return false;
        }

        outJoint = static_cast<u16>(jointIndex);
        return true;
    }

    if(context.joints.size() > static_cast<usize>(Limit<u16>::s_Max)){
        outError = "skeleton has more than 65536 joints";
        return false;
    }

    outJoint = static_cast<u16>(context.joints.size());
    context.joints.push_back(cluster->bone_node);
    context.inverseBindMatrices.push_back(convertedMatrix);
    return true;
}

bool BuildClusterJointMap(
    const MeshInstance& instance,
    const ImportOptions& options,
    ufbx_skin_deformer* skin,
    SkinExportContext& context,
    UtilityVector<u16>& outClusterJoints,
    AString& outError
){
    outClusterJoints.clear();
    if(!skin){
        outError = "skinned geometry requires a skin deformer";
        return false;
    }
    if(skin->clusters.count == 0u){
        outError = "skin deformer contains no clusters";
        return false;
    }
    if(skin->clusters.count > static_cast<usize>(Limit<u32>::s_Max)){
        outError = "skin deformer has too many clusters";
        return false;
    }

    outClusterJoints.reserve(skin->clusters.count);
    for(usize clusterIndex = 0u; clusterIndex < skin->clusters.count; ++clusterIndex){
        ufbx_skin_cluster* cluster = skin->clusters.data[clusterIndex];
        if(!cluster){
            outError = "skin deformer contains a null cluster";
            return false;
        }

        const ufbx_matrix inverseBind = BuildOutputInverseBindMatrix(*cluster, instance, options);
        u16 joint = 0u;
        if(!FindOrAddJoint(context, cluster, inverseBind, joint, outError))
            return false;
        outClusterJoints.push_back(joint);
    }

    return true;
}

bool BuildSkinInfluence(
    ufbx_skin_deformer* skin,
    const UtilityVector<u16>& clusterJoints,
    const u32 logicalVertex,
    GeometrySkinInfluence& outInfluence,
    AString& outError
){
    outInfluence = GeometrySkinInfluence{};
    if(!skin || logicalVertex >= skin->vertices.count){
        outError = "skin deformer does not contain weights for every logical vertex";
        return false;
    }

    const ufbx_skin_vertex skinVertex = skin->vertices.data[logicalVertex];
    if(
        skinVertex.weight_begin > skin->weights.count
        || skinVertex.num_weights > skin->weights.count - skinVertex.weight_begin
    ){
        outError = "skin vertex weight range is out of bounds";
        return false;
    }

    f64 weightSum = 0.0;
    u32 writtenInfluenceCount = 0u;
    for(u32 weightOffset = 0u; weightOffset < skinVertex.num_weights; ++weightOffset){
        if(writtenInfluenceCount == 4u)
            break;

        const ufbx_skin_weight& weight = skin->weights.data[skinVertex.weight_begin + weightOffset];
        const f64 value = static_cast<f64>(weight.weight);
        if(!IsFinite(value)){
            outError = "skin contains a non-finite weight";
            return false;
        }
        if(value <= 0.0)
            continue;
        if(weight.cluster_index >= clusterJoints.size()){
            outError = "skin weight references an out-of-range cluster";
            return false;
        }

        outInfluence.joint[writtenInfluenceCount] = clusterJoints[weight.cluster_index];
        outInfluence.weight[writtenInfluenceCount] = static_cast<f32>(value);
        weightSum += value;
        ++writtenInfluenceCount;
    }

    if(!IsFinite(weightSum) || weightSum <= 0.0){
        outError = "skinned geometry contains a vertex with no positive skin weights";
        return false;
    }

    const f32 inverseWeightSum = static_cast<f32>(1.0 / weightSum);
    for(f32& weight : outInfluence.weight)
        weight *= inverseWeightSum;

    return true;
}

bool AppendInstanceGeometry(
    const MeshInstance& instance,
    const ImportOptions& options,
    const bool wantsSkinnedGeometry,
    const bool wantsSkinning,
    const Vec4& defaultColor,
    UtilityVector<u32>& inOutTriangleIndices,
    UtilityVector<FlatGeometryVertex>& outFlatVertices,
    SkinExportContext& inOutSkinContext,
    bool& inOutSawVertexColors,
    bool& inOutSawVertexUvs,
    AString& outError
){
    ufbx_mesh* mesh = instance.mesh;
    ufbx_node* node = instance.node;
    if(!mesh || !node){
        outError = "internal mesh instance is null";
        return false;
    }
    if(!mesh->vertex_position.exists){
        outError = "mesh is missing positions";
        return false;
    }
    if(!mesh->vertex_normal.exists){
        outError = "mesh is missing normals after ufbx import";
        return false;
    }

    if(mesh->max_face_triangles > Limit<usize>::s_Max / 3u){
        outError = "mesh face triangulation scratch size overflows";
        return false;
    }

    const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&node->geometry_to_world);
    const bool importUvs = wantsSkinnedGeometry && mesh->vertex_uv.exists;
    const bool importColors = options.importColors && mesh->vertex_color.exists;
    ufbx_skin_deformer* skin = nullptr;
    UtilityVector<u16> clusterJoints;
    if(wantsSkinning){
        if(mesh->skin_deformers.count != 1u){
            outError = "skinned geometry requires exactly one skin deformer per selected mesh";
            return false;
        }
        skin = mesh->skin_deformers.data[0u];
        if(!BuildClusterJointMap(instance, options, skin, inOutSkinContext, clusterJoints, outError))
            return false;
    }

    PositionNormalMap smoothNormals;
    if(!BuildSmoothPositionNormals(*mesh, *node, options, wantsSkinning, inOutTriangleIndices, smoothNormals, outError))
        return false;

    inOutTriangleIndices.resize(static_cast<usize>(mesh->max_face_triangles) * 3u);
    for(usize faceIndex = 0; faceIndex < mesh->num_faces; ++faceIndex){
        const ufbx_face face = mesh->faces.data[faceIndex];
        if(face.num_indices < 3u)
            continue;

        const u32 triangleCount = ufbx_triangulate_face(
            inOutTriangleIndices.data(),
            inOutTriangleIndices.size(),
            mesh,
            face
        );

        for(u32 triangleIndex = 0u; triangleIndex < triangleCount; ++triangleIndex){
            u32 cornerIndices[3] = {
                inOutTriangleIndices[triangleIndex * 3u + 0u],
                inOutTriangleIndices[triangleIndex * 3u + 1u],
                inOutTriangleIndices[triangleIndex * 3u + 2u],
            };
            if(options.flipWinding)
                Swap(cornerIndices[1], cornerIndices[2]);

            FlatGeometryVertex triangleVertices[3] = {};
            for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
                const u32 cornerIndex = cornerIndices[triangleCornerIndex];
                if(cornerIndex >= mesh->vertex_indices.count){
                    outError = "mesh corner references an out-of-range logical vertex";
                    return false;
                }
                const u32 logicalVertex = mesh->vertex_indices.data[cornerIndex];

                GeometryVertex vertex;
                vertex.position = LoadCornerOutputPosition(*mesh, *node, options, wantsSkinning, cornerIndex);
                vertex.normal = Vec3{ 0.0f, 0.0f, 0.0f };
                auto foundNormal = smoothNormals.find(MakePositionKey(vertex.position));
                if(foundNormal != smoothNormals.end())
                    vertex.normal = foundNormal.value();
                if(!Normalize(vertex.normal))
                    vertex.normal = LoadCornerOutputNormal(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);

                vertex.uv0 = Vec2{};
                if(importUvs){
                    vertex.uv0 = ToVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex));
                    inOutSawVertexUvs = true;
                }

                vertex.color = defaultColor;
                if(importColors){
                    vertex.color = ToVec4(ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex));
                    inOutSawVertexColors = true;
                }

                if(!IsFiniteVertex(vertex)){
                    outError = "mesh contains non-finite vertex data";
                    return false;
                }

                FlatGeometryVertex flatVertex{};
                flatVertex.vertex = vertex;
                if(wantsSkinning){
                    if(!BuildSkinInfluence(skin, clusterJoints, logicalVertex, flatVertex.skin, outError))
                        return false;
                }

                triangleVertices[triangleCornerIndex] = flatVertex;
            }

            if(!TriangleHasArea(triangleVertices, options.triangleAreaLengthSquaredEpsilon))
                continue;

            for(const FlatGeometryVertex& flatVertex : triangleVertices)
                outFlatVertices.push_back(flatVertex);
        }
    }

    return true;
}

bool EstimateSelectedTriangleCorners(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    usize& outTriangleCorners,
    AString& outError
){
    outTriangleCorners = 0u;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }

        const ufbx_mesh* const mesh = instances[instanceIndex].mesh;
        if(!mesh)
            continue;
        if(mesh->num_triangles > (Limit<usize>::s_Max - outTriangleCorners) / 3u){
            outError = "selected meshes have too many triangle corners";
            return false;
        }

        outTriangleCorners += static_cast<usize>(mesh->num_triangles) * 3u;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SceneHandle::~SceneHandle(){
    if(scene){
        ufbx_free_scene(scene);
        scene = nullptr;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool LoadScene(const ImportOptions& options, SceneHandle& outScene, AString& outError){
    ufbx_load_opts loadOptions = {};
    loadOptions.load_external_files = true;
    loadOptions.ignore_missing_external_files = true;
    loadOptions.generate_missing_normals = true;
    loadOptions.normalize_normals = true;
    loadOptions.clean_skin_weights = true;
    loadOptions.evaluate_skinning = true;
    loadOptions.evaluate_caches = true;
    loadOptions.geometry_transform_handling = UFBX_GEOMETRY_TRANSFORM_HANDLING_PRESERVE;

    if(!options.preserveSpace){
        loadOptions.target_axes.right = UFBX_COORDINATE_AXIS_POSITIVE_X;
        loadOptions.target_axes.up = UFBX_COORDINATE_AXIS_POSITIVE_Y;
        loadOptions.target_axes.front = UFBX_COORDINATE_AXIS_POSITIVE_Z;
        loadOptions.target_unit_meters = 1.0f;
    }

    const AString inputPath = PathToUtf8(PathFromUtf8(options.inputPath));
    ufbx_error error = {};
    outScene.scene = ufbx_load_file_len(inputPath.data(), inputPath.size(), &loadOptions, &error);
    if(!outScene.scene){
        outError = "failed to load FBX: " + __hidden_fbx_import::FormatUfbxError(error);
        return false;
    }

    return true;
}

UtilityVector<MeshInstance> CollectMeshInstances(ufbx_scene* scene, const bool includeHidden){
    UtilityVector<MeshInstance> instances;
    if(!scene)
        return instances;

    instances.reserve(scene->nodes.count);
    for(usize i = 0; i < scene->nodes.count; ++i){
        ufbx_node* node = scene->nodes.data[i];
        if(!node || !node->mesh)
            continue;
        if(!includeHidden && !node->visible)
            continue;

        MeshInstance instance;
        instance.node = node;
        instance.mesh = node->mesh;
        instance.index = instances.size();
        instances.push_back(instance);
    }

    return instances;
}

void PrintMeshInstances(const UtilityVector<MeshInstance>& instances){
    if(instances.empty()){
        NWB_COUT << "No mesh instances found.\n";
        return;
    }

    NWB_COUT << "Mesh instances:\n";
    for(const MeshInstance& instance : instances)
        NWB_COUT << "  " << __hidden_fbx_import::MeshDisplayName(instance) << "\n";
}

bool SelectMeshInstances(
    const UtilityVector<MeshInstance>& instances,
    const AString& selector,
    UtilityVector<usize>& outSelection,
    AString& outError
){
    outSelection.clear();
    outError.clear();

    const AString normalized = ToLower(Trim(selector));
    if(normalized.empty() || normalized == "all"){
        outSelection.reserve(instances.size());
        for(usize instanceIndex = 0u; instanceIndex < instances.size(); ++instanceIndex)
            outSelection.push_back(instanceIndex);
        return true;
    }
    if(normalized == "first"){
        if(instances.empty()){
            outError = "no mesh instances are available";
            return false;
        }
        outSelection.push_back(0u);
        return true;
    }

    usize parsedIndex = 0u;
    if(__hidden_fbx_import::ParseIndexSelector(normalized, parsedIndex)){
        if(parsedIndex >= instances.size()){
            outError = "mesh index is out of range";
            return false;
        }
        outSelection.push_back(parsedIndex);
        return true;
    }

    UtilityVector<usize> partialSelection;
    outSelection.reserve(instances.size());
    for(const MeshInstance& instance : instances){
        if(
            __hidden_fbx_import::NormalizedAsciiEqual(instance.node->name, normalized)
            || __hidden_fbx_import::NormalizedAsciiEqual(instance.mesh->name, normalized)
        ){
            outSelection.push_back(instance.index);
        }
        else if(
            outSelection.empty()
            && (
                __hidden_fbx_import::NormalizedAsciiContains(instance.node->name, normalized)
                || __hidden_fbx_import::NormalizedAsciiContains(instance.mesh->name, normalized)
            )
        ){
            if(partialSelection.empty())
                partialSelection.reserve(instances.size());
            partialSelection.push_back(instance.index);
        }
    }
    if(!outSelection.empty())
        return true;
    if(!partialSelection.empty()){
        outSelection = Move(partialSelection);
        return true;
    }

    outError = "mesh selector did not match any node or mesh";
    return false;
}

bool BuildGeometry(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    UtilityVector<GeometryVertex>& outVertices,
    UtilityVector<u32>& outIndices,
    UtilityVector<GeometrySkinInfluence>& outSkin,
    u32& outSkeletonJointCount,
    UtilityVector<GeometryJointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    AString& outError
){
    outVertices.clear();
    outIndices.clear();
    outSkin.clear();
    outSkeletonJointCount = 0u;
    outInverseBindMatrices.clear();
    outSawVertexColors = false;
    outSawVertexUvs = false;
    outError.clear();

    usize estimatedTriangleCorners = 0u;
    if(!__hidden_fbx_import::EstimateSelectedTriangleCorners(
        instances,
        selection,
        estimatedTriangleCorners,
        outError
    ))
        return false;

    UtilityVector<__hidden_fbx_import::FlatGeometryVertex> flatVertices;
    flatVertices.reserve(estimatedTriangleCorners);
    outIndices.reserve(estimatedTriangleCorners);
    UtilityVector<u32> triangleIndices;
    u32 geometryClass = 0u;
    if(!ParseGeometryClassText(options.geometryClass, geometryClass)){
        outError = GeometryClassErrorText();
        return false;
    }
    const bool wantsSkinnedGeometry = GeometryClassUsesSkinnedGeometryRuntime(geometryClass);
    const bool wantsSkinning = GeometryClassUsesSkinning(geometryClass);
    __hidden_fbx_import::SkinExportContext skinContext;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }
        if(
            !__hidden_fbx_import::AppendInstanceGeometry(
                instances[instanceIndex],
                options,
                wantsSkinnedGeometry,
                wantsSkinning,
                defaultColor,
                triangleIndices,
                flatVertices,
                skinContext,
                outSawVertexColors,
                outSawVertexUvs,
                outError
            )
        ){
            return false;
        }
    }

    if(flatVertices.empty()){
        outError = "selected meshes produced no triangles";
        return false;
    }
    if(flatVertices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        outError = "geometry has more than u32-addressable vertices";
        return false;
    }

    if(options.deduplicate){
        outIndices.resize(flatVertices.size());
        ufbx_vertex_stream stream = {};
        stream.data = flatVertices.data();
        stream.vertex_count = flatVertices.size();
        stream.vertex_size = sizeof(__hidden_fbx_import::FlatGeometryVertex);

        ufbx_error error = {};
        const usize uniqueVertexCount = static_cast<usize>(ufbx_generate_indices(
            &stream,
            1u,
            outIndices.data(),
            outIndices.size(),
            nullptr,
            &error
        ));
        if(error.type != UFBX_ERROR_NONE){
            outError = "ufbx failed to generate an index buffer: " + __hidden_fbx_import::FormatUfbxError(error);
            return false;
        }
        if(uniqueVertexCount > static_cast<usize>(Limit<u32>::s_Max)){
            outError = "deduplicated geometry has more than u32-addressable vertices";
            return false;
        }

        flatVertices.resize(uniqueVertexCount);
    }
    else{
        outIndices.resize(flatVertices.size());
        Iota(outIndices.begin(), outIndices.end(), 0u);
    }

    outVertices.reserve(flatVertices.size());
    if(wantsSkinning)
        outSkin.reserve(flatVertices.size());
    for(const __hidden_fbx_import::FlatGeometryVertex& flatVertex : flatVertices){
        outVertices.push_back(flatVertex.vertex);
        if(wantsSkinning)
            outSkin.push_back(flatVertex.skin);
    }
    if(wantsSkinning){
        if(skinContext.joints.empty()){
            outError = "skinned geometry did not produce any skeleton joints";
            return false;
        }
        outSkeletonJointCount = static_cast<u32>(skinContext.joints.size());
        outInverseBindMatrices = Move(skinContext.inverseBindMatrices);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

