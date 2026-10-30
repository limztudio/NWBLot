// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SourceTriangleCorner{
    Vec3 position;
    Vec3 normal;
    Vec4 tangent;
    Vec2 uv0;
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    MeshSkinInfluence skin;
    b8 hasTangent = false;
};
static_assert(IsTriviallyCopyable_V<SourceTriangleCorner>);


struct MeshSkinInfluenceHasher{
    usize operator()(const MeshSkinInfluence& value)const noexcept{
        usize seed = Hasher<u16>{}(value.joint[0u]);
        for(usize i = 1u; i < s_MeshSkinInfluenceCount; ++i)
            HashCombine(seed, value.joint[i]);
        for(const f32 weight : value.weight.raw)
            HashCombine(seed, FloatingHashBits(weight));
        return seed;
    }
};


struct SourceVertexRefHasher{
    usize operator()(const SourceVertexRef& value)const noexcept{
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
    b8 operator()(const SourceVertexRef& lhs, const SourceVertexRef& rhs)const noexcept{
        return lhs.position == rhs.position && lhs.normal == rhs.normal && lhs.tangent == rhs.tangent && lhs.uv0 == rhs.uv0 && lhs.color == rhs.color && lhs.skin == rhs.skin;
    }
};

using Vec2IndexMap = UtilityScratchMap<Vec2, u32>;
using Vec3IndexMap = UtilityScratchMap<Vec3, u32>;
using Vec4IndexMap = UtilityScratchMap<Vec4, u32>;
using MeshSkinInfluenceIndexMap = UtilityScratchMap<MeshSkinInfluence, u32, MeshSkinInfluenceHasher, MeshSkinInfluenceEqual>;
using SourceVertexRefIndexMap = UtilityScratchMap<SourceVertexRef, u32, SourceVertexRefHasher, SourceVertexRefEqual>;


struct SourceMeshBuildContext{
    SourceMeshStreams& mesh;
    Core::Alloc::ScratchArena& scratchArena;
    Vec3IndexMap positions;
    Vec3IndexMap normals;
    Vec4IndexMap tangents;
    Vec2IndexMap uv0;
    Vec4IndexMap colors;
    MeshSkinInfluenceIndexMap skin;
    SourceVertexRefIndexMap vertexRefs;


public:
    SourceMeshBuildContext(SourceMeshStreams& sourceMesh, Core::Alloc::ScratchArena& sourceScratchArena)
        : mesh(sourceMesh)
        , scratchArena(sourceScratchArena)
        , positions(sourceScratchArena)
        , normals(sourceScratchArena)
        , tangents(sourceScratchArena)
        , uv0(sourceScratchArena)
        , colors(sourceScratchArena)
        , skin(sourceScratchArena)
        , vertexRefs(sourceScratchArena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FbxSourceMeshStreams final : NoCopy{
public:
    static void ReserveSourceMeshStreams(SourceMeshStreams& mesh, const usize estimatedTriangleCorners, const b8 wantsSkinning);
    static void ReserveSourceMeshBuildContext(SourceMeshBuildContext& context, const usize estimatedTriangleCorners, const b8 wantsSkinning);
    [[nodiscard]] static b8 SourceMeshHasCompleteTangents(const SourceMeshStreams& mesh)noexcept;
    [[nodiscard]] static b8 DropSourceMeshTangents(SourceMeshStreams& mesh, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static b8 EnsureTriangleIndexScratchCapacity(const ufbx_mesh& mesh, UtilityScratchVector<u32>& inOutTriangleIndices);
    [[nodiscard]] static Expected<SourceTangentReport> GenerateSourceMeshTangents(SourceMeshStreams& mesh, const b8 usedDefaultUvs, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] static Expected<u32> InternSourceCorner(SourceMeshBuildContext& context, const SourceTriangleCorner& corner, const b8 wantsSkinning);


public:
    FbxSourceMeshStreams() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

