// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static_assert(IsTriviallyCopyable_V<SourceTriangleCorner>);

struct MeshSkinInfluenceHasher{
public:
    usize operator()(const MeshSkinInfluence& value)const noexcept;
};

using Vec2IndexMap = UtilityScratchMap<Vec2, u32>;
using Vec3IndexMap = UtilityScratchMap<Vec3, u32>;
using Vec4IndexMap = UtilityScratchMap<Vec4, u32>;
using MeshSkinInfluenceIndexMap = UtilityScratchMap<MeshSkinInfluence, u32, MeshSkinInfluenceHasher, MeshSkinInfluenceEqual>;
using SourceVertexRefIndexMap = UtilityScratchMap<SourceVertexRef, u32>;

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


namespace FbxSourceMeshStreams{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ReserveSourceMeshStreams(usize estimatedTriangleCorners, b8 wantsSkinning, SourceMeshStreams& outMesh);
void ReserveSourceMeshBuildContext(usize estimatedTriangleCorners, b8 wantsSkinning, SourceMeshBuildContext& inOutContext);
[[nodiscard]] b8 DropSourceMeshTangents(Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh);
[[nodiscard]] b8 EnsureTriangleIndexScratchCapacity(const ufbx_mesh& mesh, UtilityScratchVector<u32>& inOutTriangleIndices);
[[nodiscard]] Expected<SourceTangentReport> GenerateSourceMeshTangents(b8 usedDefaultUvs, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh);
[[nodiscard]] Expected<u32> InternSourceCorner(const SourceTriangleCorner& corner, b8 wantsSkinning, SourceMeshBuildContext& inOutContext);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

