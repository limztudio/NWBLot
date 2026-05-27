// limztudio@gmail.com
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
    MeshSkinInfluence skin;
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

[[nodiscard]] usize HashVec2(const Vec2& value){
    usize seed = Hasher<u32>{}(FloatBits(value.x));
    HashFloat(seed, value.y);
    return seed;
}

[[nodiscard]] usize HashVec3(const Vec3& value){
    usize seed = Hasher<u32>{}(FloatBits(value.x));
    HashFloat(seed, value.y);
    HashFloat(seed, value.z);
    return seed;
}

[[nodiscard]] usize HashVec4(const Vec4& value){
    usize seed = Hasher<u32>{}(FloatBits(value.x));
    HashFloat(seed, value.y);
    HashFloat(seed, value.z);
    HashFloat(seed, value.w);
    return seed;
}

[[nodiscard]] bool Vec2EqualFloatBits(const Vec2& lhs, const Vec2& rhs){
    return FloatEqual(lhs.x, rhs.x) && FloatEqual(lhs.y, rhs.y);
}

[[nodiscard]] bool Vec3EqualFloatBits(const Vec3& lhs, const Vec3& rhs){
    return FloatEqual(lhs.x, rhs.x) && FloatEqual(lhs.y, rhs.y) && FloatEqual(lhs.z, rhs.z);
}

[[nodiscard]] bool Vec4EqualFloatBits(const Vec4& lhs, const Vec4& rhs){
    return FloatEqual(lhs.x, rhs.x)
        && FloatEqual(lhs.y, rhs.y)
        && FloatEqual(lhs.z, rhs.z)
        && FloatEqual(lhs.w, rhs.w);
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
        return HashVec2(value);
    }
};

struct Vec2Equal{
    bool operator()(const Vec2& lhs, const Vec2& rhs)const{
        return Vec2EqualFloatBits(lhs, rhs);
    }
};

struct Vec3Hasher{
    usize operator()(const Vec3& value)const{
        return HashVec3(value);
    }
};

struct Vec3Equal{
    bool operator()(const Vec3& lhs, const Vec3& rhs)const{
        return Vec3EqualFloatBits(lhs, rhs);
    }
};

struct Vec4Hasher{
    usize operator()(const Vec4& value)const{
        return HashVec4(value);
    }
};

struct Vec4Equal{
    bool operator()(const Vec4& lhs, const Vec4& rhs)const{
        return Vec4EqualFloatBits(lhs, rhs);
    }
};

struct MeshSkinInfluenceHasher{
    usize operator()(const MeshSkinInfluence& value)const{
        usize seed = Hasher<u16>{}(value.joint[0u]);
        for(usize i = 1u; i < 4u; ++i)
            HashCombine(seed, value.joint[i]);
        for(const f32 weight : value.weight)
            HashFloat(seed, weight);
        return seed;
    }
};

struct MeshSkinInfluenceEqual{
    bool operator()(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)const{
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
using MeshSkinInfluenceIndexMap = HashMap<MeshSkinInfluence, u32, MeshSkinInfluenceHasher, MeshSkinInfluenceEqual>;
using SourceVertexRefIndexMap = HashMap<SourceVertexRef, u32, SourceVertexRefHasher, SourceVertexRefEqual>;

struct SourceMeshBuildContext{
    SourceMeshStreams& mesh;
    Vec3IndexMap positions;
    Vec3IndexMap normals;
    Vec4IndexMap tangents;
    Vec2IndexMap uv0;
    Vec4IndexMap colors;
    MeshSkinInfluenceIndexMap skin;
    SourceVertexRefIndexMap vertexRefs;

    explicit SourceMeshBuildContext(SourceMeshStreams& sourceMesh)
        : mesh(sourceMesh)
    {}
};

void ReserveSourceMeshStreams(
    SourceMeshStreams& mesh,
    const usize estimatedTriangleCorners,
    const bool wantsSkinning
){
    mesh.positions.reserve(estimatedTriangleCorners);
    mesh.normals.reserve(estimatedTriangleCorners);
    mesh.uv0.reserve(estimatedTriangleCorners);
    mesh.colors.reserve(estimatedTriangleCorners);
    mesh.tangents.reserve(estimatedTriangleCorners);
    mesh.indices.reserve(estimatedTriangleCorners);
    mesh.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        mesh.skin.reserve(estimatedTriangleCorners);
}

void ReserveSourceMeshBuildContext(
    SourceMeshBuildContext& context,
    const usize estimatedTriangleCorners,
    const bool wantsSkinning
){
    context.positions.reserve(estimatedTriangleCorners);
    context.normals.reserve(estimatedTriangleCorners);
    context.uv0.reserve(estimatedTriangleCorners);
    context.colors.reserve(estimatedTriangleCorners);
    context.tangents.reserve(estimatedTriangleCorners);
    context.vertexRefs.reserve(estimatedTriangleCorners);
    if(wantsSkinning)
        context.skin.reserve(estimatedTriangleCorners);
}

[[nodiscard]] bool SourceMeshHasPartialTangents(const SourceMeshStreams& mesh){
    if(mesh.tangents.empty())
        return false;

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.tangent == s_MissingSourceStreamIndex)
            return true;
    }
    return false;
}

void DropSourceMeshTangents(SourceMeshStreams& mesh){
    mesh.tangents.clear();

    SourceVertexRefIndexMap compactLookup;
    compactLookup.reserve(mesh.vertexRefs.size());
    UtilityVector<SourceVertexRef> compactVertexRefs;
    compactVertexRefs.reserve(mesh.vertexRefs.size());
    UtilityVector<u32> vertexRefRemap;
    vertexRefRemap.reserve(mesh.vertexRefs.size());

    for(SourceVertexRef ref : mesh.vertexRefs){
        ref.tangent = s_MissingSourceStreamIndex;

        auto found = compactLookup.find(ref);
        if(found != compactLookup.end()){
            vertexRefRemap.push_back(found.value());
            continue;
        }

        const u32 compactIndex = static_cast<u32>(compactVertexRefs.size());
        compactVertexRefs.push_back(ref);
        compactLookup.emplace(ref, compactIndex);
        vertexRefRemap.push_back(compactIndex);
    }

    for(u32& index : mesh.indices){
        NWB_ASSERT(index < vertexRefRemap.size());
        index = vertexRefRemap[index];
    }
    mesh.vertexRefs = Move(compactVertexRefs);
}

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
    const f64 areaLengthSquared = areaNormal.x * areaNormal.x + areaNormal.y * areaNormal.y + areaNormal.z * areaNormal.z;
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

[[nodiscard]] bool IsFiniteSkinInfluence(const MeshSkinInfluence& value){
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
    SourceMeshBuildContext& context,
    const SourceTriangleCorner& corner,
    const bool wantsSkinning,
    u32& outVertexRefIndex,
    AString& outError
){
    SourceVertexRef ref;
    if(!InternSourceValue(context.mesh.positions, context.positions, corner.position, "position", ref.position, outError))
        return false;
    if(!InternSourceValue(context.mesh.normals, context.normals, corner.normal, "normal", ref.normal, outError))
        return false;
    if(corner.hasTangent){
        if(!InternSourceValue(context.mesh.tangents, context.tangents, corner.tangent, "tangent", ref.tangent, outError))
            return false;
    }
    if(!InternSourceValue(context.mesh.uv0, context.uv0, corner.uv0, "uv0", ref.uv0, outError))
        return false;
    if(!InternSourceValue(context.mesh.colors, context.colors, corner.color, "color", ref.color, outError))
        return false;
    if(wantsSkinning){
        if(!InternSourceValue(context.mesh.skin, context.skin, corner.skin, "skin", ref.skin, outError))
            return false;
    }

    return InternSourceValue(context.mesh.vertexRefs, context.vertexRefs, ref, "vertex_ref", outVertexRefIndex, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

