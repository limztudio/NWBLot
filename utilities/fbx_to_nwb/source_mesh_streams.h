// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"

#include <core/common/log.h>
#include <global/mesh/tangent_frame_rebuild.h>
#include <global/mesh/triangle_area.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr StringView s_SourcePositionLabel = "position";
inline constexpr StringView s_SourceNormalLabel = "normal";
inline constexpr StringView s_SourceTangentLabel = "tangent";
inline constexpr StringView s_SourceUv0Label = "uv0";
inline constexpr StringView s_SourceColorLabel = "color";
inline constexpr StringView s_SourceVertexRefLabel = "vertex_ref";


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

// SIMD calculation scratch; serialized normals stay in SourceMesh's Float/Vec streams.
struct alignas(Float4) PositionNormalCalculation{
    SIMDVector value = {};
};

using PositionNormalMap = HashMap<PositionKey, PositionNormalCalculation, PositionKeyHasher, PositionKeyEqual>;


struct MeshSkinInfluenceHasher{
    usize operator()(const MeshSkinInfluence& value)const{
        usize seed = Hasher<u16>{}(value.joint[0u]);
        for(usize i = 1u; i < s_MeshSkinInfluenceCount; ++i)
            HashCombine(seed, value.joint[i]);
        for(const f32 weight : value.weight.raw)
            HashCombine(seed, FloatHashBits(weight));
        return seed;
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FbxSourceMeshStreams final : NoCopy{
public:
    static void reserveSourceMeshStreams(
        SourceMeshStreams& mesh,
        const usize estimatedTriangleCorners,
        const bool wantsSkinning
    );
    static void reserveSourceMeshBuildContext(
        SourceMeshBuildContext& context,
        const usize estimatedTriangleCorners,
        const bool wantsSkinning
    );
    [[nodiscard]] static bool sourceMeshHasCompleteTangents(const SourceMeshStreams& mesh);
    static void dropSourceMeshTangents(SourceMeshStreams& mesh);
    [[nodiscard]] static bool ensureTriangleIndexScratchCapacity(
        const ufbx_mesh& mesh,
        UtilityVector<u32>& inOutTriangleIndices
    );
    template<typename Value, typename Lookup>
    [[nodiscard]] static bool internSourceValue(
        UtilityVector<Value>& stream,
        Lookup& lookup,
        const Value& value,
        const AStringView streamName,
        u32& outIndex
    );
    [[nodiscard]] static bool generateSourceMeshTangents(
        SourceMeshStreams& mesh,
        const bool usedDefaultUvs,
        SourceTangentReport& outTangentReport
    );
    [[nodiscard]] static bool internSourceCorner(
        SourceMeshBuildContext& context,
        const SourceTriangleCorner& corner,
        const bool wantsSkinning,
        u32& outVertexRefIndex
    );


public:
    FbxSourceMeshStreams() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Value, typename Lookup>
[[nodiscard]] bool FbxSourceMeshStreams::internSourceValue(
    UtilityVector<Value>& stream,
    Lookup& lookup,
    const Value& value,
    const AStringView streamName,
    u32& outIndex
){
    auto found = lookup.find(value);
    if(found != lookup.end()){
        outIndex = found.value();
        return true;
    }

    if(stream.size() >= static_cast<usize>(s_MissingSourceStreamIndex)){
        NWB_LOGGER_ERROR(GLB_TEXT("Failed to build mesh: {} stream has too many unique values"), StringConvert(streamName));
        return false;
    }

    outIndex = static_cast<u32>(stream.size());
    stream.push_back(value);
    lookup.emplace(value, outIndex);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

