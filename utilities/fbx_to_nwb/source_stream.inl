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

struct MeshSkinInfluenceHasher{
    usize operator()(const MeshSkinInfluence& value)const{
        usize seed = Hasher<u16>{}(value.joint[0u]);
        for(usize i = 1u; i < 4u; ++i)
            HashCombine(seed, value.joint[i]);
        for(const f32 weight : value.weight)
            HashCombine(seed, FloatHashBits(weight));
        return seed;
    }
};

struct MeshSkinInfluenceEqual{
    bool operator()(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)const{
        for(usize i = 0u; i < 4u; ++i){
            if(lhs.joint[i] != rhs.joint[i] || FloatHashBits(lhs.weight[i]) != FloatHashBits(rhs.weight[i]))
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

using Vec2IndexMap = HashMap<Vec2, u32>;
using Vec3IndexMap = HashMap<Vec3, u32>;
using Vec4IndexMap = HashMap<Vec4, u32>;
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

[[nodiscard]] bool SourceMeshHasCompleteTangents(const SourceMeshStreams& mesh){
    if(mesh.vertexRefs.empty() || mesh.tangents.empty())
        return false;

    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.tangent >= mesh.tangents.size())
            return false;
    }
    return true;
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
    UtilityVector<u32>& inOutTriangleIndices
){
    if(mesh.max_face_triangles > Limit<usize>::s_Max / 3u){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh face triangulation scratch size overflows"));
        return false;
    }

    const usize triangleIndexCapacity = static_cast<usize>(mesh.max_face_triangles) * 3u;
    if(inOutTriangleIndices.size() < triangleIndexCapacity)
        inOutTriangleIndices.resize(triangleIndexCapacity);
    return true;
}

[[nodiscard]] PositionKey MakePositionKey(const Vec3& position){
    return PositionKey{
        FloatHashBits(position.x),
        FloatHashBits(position.y),
        FloatHashBits(position.z),
    };
}

[[nodiscard]] TriangleAreaNormal64 BuildTriangleAreaNormal64(const Vec3& a, const Vec3& b, const Vec3& c){
#if defined(NWB_HAS_AVX2)
    const __m256d av = _mm256_set_pd(0.0, static_cast<f64>(a.z), static_cast<f64>(a.y), static_cast<f64>(a.x));
    const __m256d bv = _mm256_set_pd(0.0, static_cast<f64>(b.z), static_cast<f64>(b.y), static_cast<f64>(b.x));
    const __m256d cv = _mm256_set_pd(0.0, static_cast<f64>(c.z), static_cast<f64>(c.y), static_cast<f64>(c.x));
    const __m256d ab = _mm256_sub_pd(bv, av);
    const __m256d ac = _mm256_sub_pd(cv, av);
    const __m256d abYzx = _mm256_permute4x64_pd(ab, _MM_SHUFFLE(3, 0, 2, 1));
    const __m256d abZxy = _mm256_permute4x64_pd(ab, _MM_SHUFFLE(3, 1, 0, 2));
    const __m256d acYzx = _mm256_permute4x64_pd(ac, _MM_SHUFFLE(3, 0, 2, 1));
    const __m256d acZxy = _mm256_permute4x64_pd(ac, _MM_SHUFFLE(3, 1, 0, 2));
    const __m256d cross = _mm256_sub_pd(_mm256_mul_pd(abYzx, acZxy), _mm256_mul_pd(abZxy, acYzx));

    alignas(32) f64 areaNormal[4];
    _mm256_store_pd(areaNormal, cross);
    return TriangleAreaNormal64{
        areaNormal[0],
        areaNormal[1],
        areaNormal[2],
    };
#else
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
#endif
}

[[nodiscard]] f64 TriangleAreaNormalLengthSquared(const TriangleAreaNormal64& areaNormal){
#if defined(NWB_HAS_AVX2)
    const __m256d normal = _mm256_set_pd(0.0, areaNormal.z, areaNormal.y, areaNormal.x);
    const __m256d squared = _mm256_mul_pd(normal, normal);
    const __m128d low = _mm256_castpd256_pd128(squared);
    const __m128d high = _mm256_extractf128_pd(squared, 1);
    __m128d sum = _mm_add_pd(low, high);
    sum = _mm_add_sd(sum, _mm_unpackhi_pd(sum, sum));
    return _mm_cvtsd_f64(sum);
#else
    return areaNormal.x * areaNormal.x + areaNormal.y * areaNormal.y + areaNormal.z * areaNormal.z;
#endif
}

[[nodiscard]] bool TriangleHasArea(
    const SourceTriangleCorner (&vertices)[3],
    const f64 triangleAreaLengthSquaredEpsilon
){
    const Vec3& a = vertices[0u].position;
    const Vec3& b = vertices[1u].position;
    const Vec3& c = vertices[2u].position;
    const TriangleAreaNormal64 areaNormal = BuildTriangleAreaNormal64(a, b, c);
    const f64 areaLengthSquared = TriangleAreaNormalLengthSquared(areaNormal);
    return areaLengthSquared > triangleAreaLengthSquaredEpsilon;
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
    const f32 scale = static_cast<f32>(options.scale);
    StoreFloat(VectorScale(VectorSetW(LoadFloat(outputPosition), 0.0f), scale), &outputPosition);
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
            const SIMDVector normalVector = VectorSetW(LoadFloat(normal), 0.0f);
            const SIMDVector tangentVector = VectorSetW(LoadFloat(outputTangent), 0.0f);
            const SIMDVector bitangentVector = VectorSetW(LoadFloat(outputBitangent), 0.0f);
            const SIMDVector tangentSpaceBitangent = Vector3Cross(normalVector, tangentVector);
            const f32 bitangentDot = VectorGetX(Vector3Dot(tangentSpaceBitangent, bitangentVector));
            sign = bitangentDot < 0.0f ? -1.0f : 1.0f;
        }
    }

    outTangent = Vec4{ outputTangent.x, outputTangent.y, outputTangent.z, sign };
    return true;
}

[[nodiscard]] bool IsFiniteSkinInfluence(const MeshSkinInfluence& value){
    for(const f32 weight : value.weight){
        if(!IsFinite(weight))
            return false;
    }
    return true;
}

[[nodiscard]] bool IsFiniteSourceTriangleCorner(const SourceTriangleCorner& corner, const bool wantsSkinning){
    if(
        !VectorIsFinite(LoadFloat(corner.position), 0x7u)
        || !VectorIsFinite(LoadFloat(corner.normal), 0x7u)
        || !VectorIsFinite(LoadFloat(corner.uv0), 0x3u)
        || !VectorIsFinite(LoadFloat(corner.color), 0xFu)
    )
        return false;
    if(corner.hasTangent && !VectorIsFinite(LoadFloat(corner.tangent), 0xFu))
        return false;
    return !wantsSkinning || IsFiniteSkinInfluence(corner.skin);
}

template<typename Value, typename Lookup>
[[nodiscard]] bool InternSourceValue(
    UtilityVector<Value>& stream,
    Lookup& lookup,
    const Value& value,
    const char* streamName,
    u32& outIndex
){
    auto found = lookup.find(value);
    if(found != lookup.end()){
        outIndex = found.value();
        return true;
    }

    if(stream.size() >= static_cast<usize>(s_MissingSourceStreamIndex)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: {} stream has too many unique values"), StringConvert(streamName));
        return false;
    }

    outIndex = static_cast<u32>(stream.size());
    stream.push_back(value);
    lookup.emplace(value, outIndex);
    return true;
}

[[nodiscard]] bool GenerateSourceMeshTangents(
    SourceMeshStreams& mesh,
    const bool usedDefaultUvs,
    SourceTangentReport& outTangentReport
){
    using RebuildVertex = Core::Mesh::TangentFrameRebuildVertex;

    outTangentReport = SourceTangentReport{};
    if(mesh.vertexRefs.empty() || mesh.indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh has no source vertices for tangent generation"));
        return false;
    }
    if((mesh.indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh index stream must contain whole triangles for tangent generation"));
        return false;
    }

    UtilityVector<RebuildVertex> rebuildVertices;
    rebuildVertices.reserve(mesh.vertexRefs.size());
    for(const SourceVertexRef& ref : mesh.vertexRefs){
        if(ref.position >= mesh.positions.size() || ref.normal >= mesh.normals.size() || ref.uv0 >= mesh.uv0.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh vertex_ref references an out-of-range stream while generating tangents"));
            return false;
        }

        const Vec3& position = mesh.positions[ref.position];
        const Vec3& normal = mesh.normals[ref.normal];
        Float4 rebuildPosition;
        Float4 rebuildNormal;
        StoreFloat(VectorSetW(LoadFloat(position), 0.0f), &rebuildPosition);
        StoreFloat(VectorSetW(LoadFloat(normal), 0.0f), &rebuildNormal);
        rebuildVertices.push_back(RebuildVertex{
            rebuildPosition,
            rebuildNormal,
            Float4(1.0f, 0.0f, 0.0f, 1.0f),
            mesh.uv0[ref.uv0],
        });
    }

    UtilityVector<u32> rebuildIndices;
    rebuildIndices.reserve(mesh.indices.size());
    for(usize indexBase = 0u; indexBase < mesh.indices.size(); indexBase += 3u){
        const u32 i0 = mesh.indices[indexBase + 0u];
        const u32 i1 = mesh.indices[indexBase + 1u];
        const u32 i2 = mesh.indices[indexBase + 2u];
        if(i0 >= rebuildVertices.size() || i1 >= rebuildVertices.size() || i2 >= rebuildVertices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh index stream references an out-of-range vertex_ref while generating tangents"));
            return false;
        }
        if(i0 == i1 || i0 == i2 || i1 == i2)
            continue;

        const SIMDVector p0 = LoadFloat(rebuildVertices[i0].position);
        const SIMDVector p1 = LoadFloat(rebuildVertices[i1].position);
        const SIMDVector p2 = LoadFloat(rebuildVertices[i2].position);
        if(!Core::Mesh::FrameValidDirection(TriangleTests::AreaNormal(p0, p1, p2)))
            continue;

        rebuildIndices.push_back(i0);
        rebuildIndices.push_back(i1);
        rebuildIndices.push_back(i2);
    }
    if(rebuildIndices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: mesh has no valid triangles for tangent generation"));
        return false;
    }

    Core::Mesh::TangentFrameRebuildResult rebuildResult;
    if(!Core::Mesh::RebuildTangentFrames(rebuildVertices, rebuildIndices, &rebuildResult)){
        NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to generate source tangent stream"));
        return false;
    }

    Vec4IndexMap tangentLookup;
    tangentLookup.reserve(mesh.vertexRefs.size());
    mesh.tangents.clear();
    for(usize vertexRefIndex = 0u; vertexRefIndex < mesh.vertexRefs.size(); ++vertexRefIndex){
        SourceVertexRef& ref = mesh.vertexRefs[vertexRefIndex];
        const SIMDVector normal = Core::Mesh::FrameNormalizeDirection(
            VectorSetW(LoadFloat(mesh.normals[ref.normal]), 0.0f),
            VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        );
        const SIMDVector tangent = Core::Mesh::FrameResolveTangent(
            normal,
            VectorSetW(LoadFloat(rebuildVertices[vertexRefIndex].tangent), 0.0f),
            Core::Mesh::FrameFallbackTangent(normal)
        );
        if(!Core::Mesh::FrameValidDirection(tangent)){
            NWB_LOGGER_ERROR(NWB_TEXT("Failed to build mesh: failed to resolve generated source tangent"));
            return false;
        }

        const f32 handedness = Core::Mesh::FrameTangentHandedness(rebuildVertices[vertexRefIndex].tangent.w, 1.0f);
        Vec4 generatedTangent;
        StoreFloat(VectorSetW(tangent, handedness), &generatedTangent);
        if(!InternSourceValue(mesh.tangents, tangentLookup, generatedTangent, "tangent", ref.tangent))
            return false;
    }
    outTangentReport.degenerateUvTriangleCount = rebuildResult.degenerateUvTriangleCount;
    outTangentReport.fallbackTangentVertexCount = rebuildResult.fallbackTangentVertexCount;
    outTangentReport.mode =
        usedDefaultUvs || rebuildResult.degenerateUvTriangleCount != 0u || rebuildResult.fallbackTangentVertexCount != 0u
        ? SourceTangentMode::GeneratedFallback
        : SourceTangentMode::GeneratedUv
    ;
    return true;
}

[[nodiscard]] bool InternSourceCorner(
    SourceMeshBuildContext& context,
    const SourceTriangleCorner& corner,
    const bool wantsSkinning,
    u32& outVertexRefIndex
){
    SourceVertexRef ref;
    if(!InternSourceValue(context.mesh.positions, context.positions, corner.position, "position", ref.position))
        return false;
    if(!InternSourceValue(context.mesh.normals, context.normals, corner.normal, "normal", ref.normal))
        return false;
    if(corner.hasTangent){
        if(!InternSourceValue(context.mesh.tangents, context.tangents, corner.tangent, "tangent", ref.tangent))
            return false;
    }
    if(!InternSourceValue(context.mesh.uv0, context.uv0, corner.uv0, "uv0", ref.uv0))
        return false;
    if(!InternSourceValue(context.mesh.colors, context.colors, corner.color, "color", ref.color))
        return false;
    if(wantsSkinning){
        if(!InternSourceValue(context.mesh.skin, context.skin, corner.skin, "skin", ref.skin))
            return false;
    }

    return InternSourceValue(context.mesh.vertexRefs, context.vertexRefs, ref, "vertex_ref", outVertexRefIndex);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

