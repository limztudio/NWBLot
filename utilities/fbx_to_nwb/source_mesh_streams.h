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
public:
    usize operator()(const MeshSkinInfluence& value)const noexcept;
};

struct SourceVertexRefHasher{
public:
    usize operator()(const SourceVertexRef& value)const noexcept;
};

struct SourceVertexRefEqual{
public:
    b8 operator()(const SourceVertexRef& lhs, const SourceVertexRef& rhs)const noexcept;
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
    SourceMeshBuildContext(SourceMeshStreams& sourceMesh, Core::Alloc::ScratchArena& sourceScratchArena);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class FbxSourceMeshStreams final : NoCopy{
public:
    static void ReserveSourceMeshStreams(usize estimatedTriangleCorners, b8 wantsSkinning, SourceMeshStreams& outMesh);
    static void ReserveSourceMeshBuildContext(usize estimatedTriangleCorners, b8 wantsSkinning, SourceMeshBuildContext& inOutContext);
    [[nodiscard]] static b8 SourceMeshHasCompleteTangents(const SourceMeshStreams& mesh)noexcept;
    [[nodiscard]] static b8 DropSourceMeshTangents(Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh);
    [[nodiscard]] static b8 EnsureTriangleIndexScratchCapacity(const ufbx_mesh& mesh, UtilityScratchVector<u32>& inOutTriangleIndices);
    [[nodiscard]] static Expected<SourceTangentReport> GenerateSourceMeshTangents(b8 usedDefaultUvs, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh);
    [[nodiscard]] static Expected<u32> InternSourceCorner(const SourceTriangleCorner& corner, b8 wantsSkinning, SourceMeshBuildContext& inOutContext);
    [[nodiscard]] static b8 ValidateMeshGeometry(const SourceMeshStreams& mesh, AStringView context);


public:
    FbxSourceMeshStreams() = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

