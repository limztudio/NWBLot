// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_to_nwb.h"

#include "fbx_skin.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_fbx_import{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

struct SourceTriangleCorner{
    Vec3 position;
    Vec3 normal;
    Vec4 tangent;
    Vec2 uv0;
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    GeometrySkinInfluence skin;
    bool hasTangent = false;
};
static_assert(IsTriviallyCopyable_V<SourceTriangleCorner>);

struct TriangleAreaNormal64{
    f64 x = 0.0;
    f64 y = 0.0;
    f64 z = 0.0;
};
static_assert(IsTriviallyCopyable_V<TriangleAreaNormal64>);

[[nodiscard]] u32 FloatBits(f32 value){
    if(value == 0.0f)
        value = 0.0f;

    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

inline void HashFloat(usize& seed, const f32 value){
    HashCombine(seed, FloatBits(value));
}

[[nodiscard]] bool FloatEqual(const f32 lhs, const f32 rhs){
    return FloatBits(lhs) == FloatBits(rhs);
}

struct PositionKey{
    u32 x = 0u;
    u32 y = 0u;
    u32 z = 0u;
};

struct PositionKeyHasher{
    usize operator()(const PositionKey& key)const{
        usize seed = Hasher<u32>{}(key.x);
        HashCombine(seed, key.y);
        HashCombine(seed, key.z);
        return seed;
    }
};

struct PositionKeyEqual{
    bool operator()(const PositionKey& lhs, const PositionKey& rhs)const{
        return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
    }
};

using PositionNormalMap = HashMap<PositionKey, Vec3, PositionKeyHasher, PositionKeyEqual>;

struct Vec2Hasher{
    usize operator()(const Vec2& value)const{
        usize seed = Hasher<u32>{}(FloatBits(value.x));
        HashFloat(seed, value.y);
        return seed;
    }
};

struct Vec2Equal{
    bool operator()(const Vec2& lhs, const Vec2& rhs)const{
        return FloatEqual(lhs.x, rhs.x) && FloatEqual(lhs.y, rhs.y);
    }
};

struct Vec3Hasher{
    usize operator()(const Vec3& value)const{
        usize seed = Hasher<u32>{}(FloatBits(value.x));
        HashFloat(seed, value.y);
        HashFloat(seed, value.z);
        return seed;
    }
};

struct Vec3Equal{
    bool operator()(const Vec3& lhs, const Vec3& rhs)const{
        return FloatEqual(lhs.x, rhs.x) && FloatEqual(lhs.y, rhs.y) && FloatEqual(lhs.z, rhs.z);
    }
};

struct Vec4Hasher{
    usize operator()(const Vec4& value)const{
        usize seed = Hasher<u32>{}(FloatBits(value.x));
        HashFloat(seed, value.y);
        HashFloat(seed, value.z);
        HashFloat(seed, value.w);
        return seed;
    }
};

struct Vec4Equal{
    bool operator()(const Vec4& lhs, const Vec4& rhs)const{
        return FloatEqual(lhs.x, rhs.x)
            && FloatEqual(lhs.y, rhs.y)
            && FloatEqual(lhs.z, rhs.z)
            && FloatEqual(lhs.w, rhs.w);
    }
};

struct GeometrySkinInfluenceHasher{
    usize operator()(const GeometrySkinInfluence& value)const{
        usize seed = Hasher<u16>{}(value.joint[0u]);
        for(usize i = 1u; i < 4u; ++i)
            HashCombine(seed, value.joint[i]);
        for(const f32 weight : value.weight)
            HashFloat(seed, weight);
        return seed;
    }
};

struct GeometrySkinInfluenceEqual{
    bool operator()(const GeometrySkinInfluence& lhs, const GeometrySkinInfluence& rhs)const{
        for(usize i = 0u; i < 4u; ++i){
            if(lhs.joint[i] != rhs.joint[i] || !FloatEqual(lhs.weight[i], rhs.weight[i]))
                return false;
        }
        return true;
    }
};

struct SourceVertexRefHasher{
    usize operator()(const SourceVertexRef& value)const{
        usize seed = Hasher<u32>{}(value.position);
        HashCombine(seed, value.normal);
        HashCombine(seed, value.tangent);
        HashCombine(seed, value.uv0);
        HashCombine(seed, value.color);
        HashCombine(seed, value.skin);
        return seed;
    }
};

struct SourceVertexRefEqual{
    bool operator()(const SourceVertexRef& lhs, const SourceVertexRef& rhs)const{
        return lhs.position == rhs.position
            && lhs.normal == rhs.normal
            && lhs.tangent == rhs.tangent
            && lhs.uv0 == rhs.uv0
            && lhs.color == rhs.color
            && lhs.skin == rhs.skin;
    }
};

using Vec2IndexMap = HashMap<Vec2, u32, Vec2Hasher, Vec2Equal>;
using Vec3IndexMap = HashMap<Vec3, u32, Vec3Hasher, Vec3Equal>;
using Vec4IndexMap = HashMap<Vec4, u32, Vec4Hasher, Vec4Equal>;
using GeometrySkinInfluenceIndexMap = HashMap<GeometrySkinInfluence, u32, GeometrySkinInfluenceHasher, GeometrySkinInfluenceEqual>;
using SourceVertexRefIndexMap = HashMap<SourceVertexRef, u32, SourceVertexRefHasher, SourceVertexRefEqual>;

struct SourceGeometryBuildContext{
    SourceGeometryStreams& geometry;
    Vec3IndexMap positions;
    Vec3IndexMap normals;
    Vec4IndexMap tangents;
    Vec2IndexMap uv0;
    Vec4IndexMap colors;
    GeometrySkinInfluenceIndexMap skin;
    SourceVertexRefIndexMap vertexRefs;

    explicit SourceGeometryBuildContext(SourceGeometryStreams& sourceGeometry)
        : geometry(sourceGeometry)
    {}
};

[[nodiscard]] bool EnsureTriangleIndexScratchCapacity(
    const ufbx_mesh& mesh,
    UtilityVector<u32>& inOutTriangleIndices,
    AString& outError
){
    if(mesh.max_face_triangles > Limit<usize>::s_Max / 3u){
        outError = "mesh face triangulation scratch size overflows";
        return false;
    }

    const usize triangleIndexCapacity = static_cast<usize>(mesh.max_face_triangles) * 3u;
    if(inOutTriangleIndices.size() < triangleIndexCapacity)
        inOutTriangleIndices.resize(triangleIndexCapacity);
    return true;
}

[[nodiscard]] PositionKey MakePositionKey(const Vec3& position){
    return PositionKey{
        FloatBits(position.x),
        FloatBits(position.y),
        FloatBits(position.z),
    };
}

[[nodiscard]] TriangleAreaNormal64 BuildTriangleAreaNormal64(const Vec3& a, const Vec3& b, const Vec3& c){
    const f64 abX = static_cast<f64>(b.x) - static_cast<f64>(a.x);
    const f64 abY = static_cast<f64>(b.y) - static_cast<f64>(a.y);
    const f64 abZ = static_cast<f64>(b.z) - static_cast<f64>(a.z);
    const f64 acX = static_cast<f64>(c.x) - static_cast<f64>(a.x);
    const f64 acY = static_cast<f64>(c.y) - static_cast<f64>(a.y);
    const f64 acZ = static_cast<f64>(c.z) - static_cast<f64>(a.z);
    return TriangleAreaNormal64{
        abY * acZ - abZ * acY,
        abZ * acX - abX * acZ,
        abX * acY - abY * acX,
    };
}

[[nodiscard]] bool TriangleHasArea(
    const SourceTriangleCorner (&vertices)[3],
    const f64 triangleAreaLengthSquaredEpsilon
){
    const Vec3& a = vertices[0u].position;
    const Vec3& b = vertices[1u].position;
    const Vec3& c = vertices[2u].position;
    const TriangleAreaNormal64 areaNormal = BuildTriangleAreaNormal64(a, b, c);
    const f64 areaLengthSquared =
        areaNormal.x * areaNormal.x
        + areaNormal.y * areaNormal.y
        + areaNormal.z * areaNormal.z
    ;
    return areaLengthSquared > triangleAreaLengthSquaredEpsilon;
}

[[nodiscard]] Vec3 TriangleAreaNormal(const Vec3& a, const Vec3& b, const Vec3& c){
    const TriangleAreaNormal64 areaNormal = BuildTriangleAreaNormal64(a, b, c);
    return Vec3{
        static_cast<f32>(areaNormal.x),
        static_cast<f32>(areaNormal.y),
        static_cast<f32>(areaNormal.z),
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

[[nodiscard]] f32 Dot(const Vec3& lhs, const Vec3& rhs){
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

[[nodiscard]] Vec3 Cross(const Vec3& lhs, const Vec3& rhs){
    return Vec3{
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

[[nodiscard]] bool LoadCornerOutputTangent(
    const ufbx_mesh& mesh,
    const ufbx_matrix& normalToWorld,
    const ImportOptions& options,
    const bool wantsSkinning,
    const u32 cornerIndex,
    const Vec3& normal,
    Vec4& outTangent
){
    if(!mesh.vertex_tangent.exists)
        return false;

    ufbx_vec3 tangent = ufbx_get_vertex_vec3(&mesh.vertex_tangent, cornerIndex);
    if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
        tangent = ufbx_transform_direction(&normalToWorld, tangent);

    Vec3 outputTangent = ToVec3(tangent);
    if(!Normalize(outputTangent))
        return false;

    f32 sign = 1.0f;
    if(mesh.vertex_bitangent.exists){
        ufbx_vec3 bitangent = ufbx_get_vertex_vec3(&mesh.vertex_bitangent, cornerIndex);
        if(options.bakeTransforms && (wantsSkinning || mesh.skinned_is_local))
            bitangent = ufbx_transform_direction(&normalToWorld, bitangent);

        Vec3 outputBitangent = ToVec3(bitangent);
        if(Normalize(outputBitangent)){
            const Vec3 tangentSpaceBitangent = Cross(normal, outputTangent);
            sign = Dot(tangentSpaceBitangent, outputBitangent) < 0.0f ? -1.0f : 1.0f;
        }
    }

    outTangent = Vec4{ outputTangent.x, outputTangent.y, outputTangent.z, sign };
    return true;
}

[[nodiscard]] bool IsFiniteVec2(const Vec2& value){
    return IsFinite(value.x) && IsFinite(value.y);
}

[[nodiscard]] bool IsFiniteVec3(const Vec3& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z);
}

[[nodiscard]] bool IsFiniteVec4(const Vec4& value){
    return IsFinite(value.x) && IsFinite(value.y) && IsFinite(value.z) && IsFinite(value.w);
}

[[nodiscard]] bool IsFiniteSkinInfluence(const GeometrySkinInfluence& value){
    for(const f32 weight : value.weight){
        if(!IsFinite(weight))
            return false;
    }
    return true;
}

[[nodiscard]] bool IsFiniteSourceTriangleCorner(const SourceTriangleCorner& corner, const bool wantsSkinning){
    if(!IsFiniteVec3(corner.position) || !IsFiniteVec3(corner.normal) || !IsFiniteVec2(corner.uv0) || !IsFiniteVec4(corner.color))
        return false;
    if(corner.hasTangent && !IsFiniteVec4(corner.tangent))
        return false;
    return !wantsSkinning || IsFiniteSkinInfluence(corner.skin);
}

template<typename Value, typename Lookup>
[[nodiscard]] bool InternSourceValue(
    UtilityVector<Value>& stream,
    Lookup& lookup,
    const Value& value,
    const char* streamName,
    u32& outIndex,
    AString& outError
){
    auto found = lookup.find(value);
    if(found != lookup.end()){
        outIndex = found.value();
        return true;
    }

    if(stream.size() >= static_cast<usize>(s_MissingSourceStreamIndex)){
        outError = AString(streamName) + " stream has too many unique values";
        return false;
    }

    outIndex = static_cast<u32>(stream.size());
    stream.push_back(value);
    lookup.emplace(value, outIndex);
    return true;
}

[[nodiscard]] bool InternSourceCorner(
    SourceGeometryBuildContext& context,
    const SourceTriangleCorner& corner,
    const bool wantsSkinning,
    u32& outVertexRefIndex,
    AString& outError
){
    SourceVertexRef ref;
    if(!InternSourceValue(context.geometry.positions, context.positions, corner.position, "position", ref.position, outError))
        return false;
    if(!InternSourceValue(context.geometry.normals, context.normals, corner.normal, "normal", ref.normal, outError))
        return false;
    if(corner.hasTangent){
        if(!InternSourceValue(context.geometry.tangents, context.tangents, corner.tangent, "tangent", ref.tangent, outError))
            return false;
    }
    if(!InternSourceValue(context.geometry.uv0, context.uv0, corner.uv0, "uv0", ref.uv0, outError))
        return false;
    if(!InternSourceValue(context.geometry.colors, context.colors, corner.color, "color", ref.color, outError))
        return false;
    if(wantsSkinning){
        if(!InternSourceValue(context.geometry.skin, context.skin, corner.skin, "skin", ref.skin, outError))
            return false;
    }

    return InternSourceValue(context.geometry.vertexRefs, context.vertexRefs, ref, "vertex_ref", outVertexRefIndex, outError);
}

template<typename VisitTriangle>
[[nodiscard]] bool VisitTriangulatedMeshTriangles(
    const ufbx_mesh& mesh,
    const bool flipWinding,
    UtilityVector<u32>& inOutTriangleIndices,
    VisitTriangle&& visitTriangle,
    AString& outError
){
    if(!EnsureTriangleIndexScratchCapacity(mesh, inOutTriangleIndices, outError))
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
                if(cornerIndex >= mesh.vertex_indices.count){
                    outError = "mesh corner references an out-of-range logical vertex";
                    return false;
                }
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
    PositionNormalMap& outNormals,
    AString& outError){
    outNormals.clear();
    outNormals.reserve(mesh.num_vertices);

    if(!VisitTriangulatedMeshTriangles(mesh, options.flipWinding, inOutTriangleIndices, [&](const u32 (&cornerIndices)[3]){
        Vec3 positions[3] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
            positions[triangleCornerIndex] = LoadCornerOutputPosition(
                mesh,
                node,
                options,
                wantsSkinning,
                cornerIndices[triangleCornerIndex]
            );
        }

        const Vec3 areaNormal = TriangleAreaNormal(positions[0u], positions[1u], positions[2u]);
        const f64 areaLengthSquared = LengthSquared(areaNormal);
        if(!IsFinite(areaLengthSquared) || areaLengthSquared <= options.triangleAreaLengthSquaredEpsilon)
            return true;

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
        return true;
    }, outError))
        return false;

    for(auto it = outNormals.begin(); it != outNormals.end(); ++it)
        static_cast<void>(Normalize(it.value()));
    return true;
}

bool AppendInstanceGeometry(
    const MeshInstance& instance,
    const ImportOptions& options,
    const bool wantsSkinning,
    const NormalMode::Enum normalMode,
    const Vec4& defaultColor,
    UtilityVector<u32>& inOutTriangleIndices,
    SourceGeometryBuildContext& inOutGeometry,
    FbxSkinDetail::ExportContext& inOutSkinContext,
    bool& inOutSawVertexColors,
    bool& inOutSawVertexUvs,
    bool& inOutSawVertexTangents,
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

    const ufbx_matrix normalToWorld = ufbx_matrix_for_normals(&node->geometry_to_world);
    const bool importUvs = mesh->vertex_uv.exists;
    const bool importColors = options.importColors && mesh->vertex_color.exists;
    const bool importTangents = mesh->vertex_tangent.exists;
    ufbx_skin_deformer* skin = nullptr;
    UtilityVector<u16> clusterJoints;
    if(wantsSkinning){
        if(mesh->skin_deformers.count != 1u){
            outError = "skinned geometry requires exactly one skin deformer per selected mesh";
            return false;
        }
        skin = mesh->skin_deformers.data[0u];
        if(!FbxSkinDetail::BuildClusterJointMap(instance, options, skin, inOutSkinContext, clusterJoints, outError))
            return false;
    }

    PositionNormalMap smoothNormals;
    if(normalMode != NormalMode::Imported && !BuildSmoothPositionNormals(*mesh, *node, options, wantsSkinning, inOutTriangleIndices, smoothNormals, outError))
        return false;

    return VisitTriangulatedMeshTriangles(*mesh, options.flipWinding, inOutTriangleIndices, [&](const u32 (&cornerIndices)[3]){
        SourceTriangleCorner triangleCorners[3] = {};
        for(usize triangleCornerIndex = 0u; triangleCornerIndex < 3u; ++triangleCornerIndex){
            const u32 cornerIndex = cornerIndices[triangleCornerIndex];
            const u32 logicalVertex = mesh->vertex_indices.data[cornerIndex];

            SourceTriangleCorner corner;
            corner.position = LoadCornerOutputPosition(*mesh, *node, options, wantsSkinning, cornerIndex);
            if(normalMode == NormalMode::Imported){
                corner.normal = LoadCornerOutputNormal(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);
            }
            else{
                auto foundNormal = smoothNormals.find(MakePositionKey(corner.position));
                if(foundNormal != smoothNormals.end())
                    corner.normal = foundNormal.value();
                if(!Normalize(corner.normal))
                    corner.normal = LoadCornerOutputNormal(*mesh, normalToWorld, options, wantsSkinning, cornerIndex);
            }

            if(importUvs){
                corner.uv0 = ToVec2(ufbx_get_vertex_vec2(&mesh->vertex_uv, cornerIndex));
                inOutSawVertexUvs = true;
            }

            corner.color = defaultColor;
            if(importColors){
                corner.color = ToVec4(ufbx_get_vertex_vec4(&mesh->vertex_color, cornerIndex));
                inOutSawVertexColors = true;
            }

            if(importTangents){
                corner.hasTangent = LoadCornerOutputTangent(
                    *mesh,
                    normalToWorld,
                    options,
                    wantsSkinning,
                    cornerIndex,
                    corner.normal,
                    corner.tangent
                );
                inOutSawVertexTangents = inOutSawVertexTangents || corner.hasTangent;
            }

            if(wantsSkinning){
                if(!FbxSkinDetail::BuildInfluence(skin, clusterJoints, logicalVertex, corner.skin, outError))
                    return false;
            }

            if(!IsFiniteSourceTriangleCorner(corner, wantsSkinning)){
                outError = "mesh contains non-finite vertex data";
                return false;
            }

            triangleCorners[triangleCornerIndex] = corner;
        }

        if(!TriangleHasArea(triangleCorners, options.triangleAreaLengthSquaredEpsilon))
            return true;

        for(const SourceTriangleCorner& corner : triangleCorners){
            u32 vertexRefIndex = 0u;
            if(!InternSourceCorner(inOutGeometry, corner, wantsSkinning, vertexRefIndex, outError))
                return false;
            inOutGeometry.geometry.indices.push_back(vertexRefIndex);
        }
        return true;
    }, outError);
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

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool BuildGeometry(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    SourceGeometryStreams& outGeometry,
    u32& outSkeletonJointCount,
    UtilityVector<GeometryJointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    bool& outSawVertexTangents,
    AString& outError
){
    outGeometry = SourceGeometryStreams{};
    outSkeletonJointCount = 0u;
    outInverseBindMatrices.clear();
    outSawVertexColors = false;
    outSawVertexUvs = false;
    outSawVertexTangents = false;
    outError.clear();

    usize estimatedTriangleCorners = 0u;
    if(!__hidden_fbx_import::EstimateSelectedTriangleCorners(
        instances,
        selection,
        estimatedTriangleCorners,
        outError
    ))
        return false;

    u32 geometryClass = 0u;
    if(!ParseGeometryClassText(options.geometryClass, geometryClass)){
        outError = GeometryClassErrorText();
        return false;
    }
    NormalMode::Enum normalMode = NormalMode::Imported;
    if(!ParseNormalModeText(options.normalMode, normalMode)){
        outError = NormalModeErrorText();
        return false;
    }

    outGeometry.positions.reserve(estimatedTriangleCorners);
    outGeometry.normals.reserve(estimatedTriangleCorners);
    outGeometry.uv0.reserve(estimatedTriangleCorners);
    outGeometry.colors.reserve(estimatedTriangleCorners);
    outGeometry.tangents.reserve(estimatedTriangleCorners);
    outGeometry.indices.reserve(estimatedTriangleCorners);
    outGeometry.vertexRefs.reserve(estimatedTriangleCorners);
    const bool wantsSkinning = GeometryClassUsesSkinning(geometryClass);
    if(wantsSkinning)
        outGeometry.skin.reserve(estimatedTriangleCorners);

    __hidden_fbx_import::SourceGeometryBuildContext geometryContext{ outGeometry };
    geometryContext.positions.reserve(estimatedTriangleCorners);
    geometryContext.normals.reserve(estimatedTriangleCorners);
    geometryContext.uv0.reserve(estimatedTriangleCorners);
    geometryContext.colors.reserve(estimatedTriangleCorners);
    geometryContext.tangents.reserve(estimatedTriangleCorners);
    geometryContext.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        geometryContext.skin.reserve(estimatedTriangleCorners);

    UtilityVector<u32> triangleIndices;
    FbxSkinDetail::ExportContext skinContext;
    for(const usize instanceIndex : selection){
        if(instanceIndex >= instances.size()){
            outError = "selected mesh index is out of range";
            return false;
        }
        if(
            !__hidden_fbx_import::AppendInstanceGeometry(
                instances[instanceIndex],
                options,
                wantsSkinning,
                normalMode,
                defaultColor,
                triangleIndices,
                geometryContext,
                skinContext,
                outSawVertexColors,
                outSawVertexUvs,
                outSawVertexTangents,
                outError
            )
        ){
            return false;
        }
    }

    if(outGeometry.indices.empty()){
        outError = "selected meshes produced no triangles";
        return false;
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

