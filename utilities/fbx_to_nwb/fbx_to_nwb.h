// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Vec2 = Float2U;
using Vec3 = Float3U;
using Vec4 = Float4U;

struct GeometryVertex{
    Vec3 position;
    Vec3 normal;
    Vec2 uv0;
    Vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
};
static_assert(sizeof(GeometryVertex) == sizeof(f32) * 12u);
static_assert(alignof(GeometryVertex) == alignof(f32));
static_assert(IsTriviallyCopyable_V<GeometryVertex>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ImportOptions{
    AString inputPath;
    AString outputPath;
    AString assetKind = "static";
    AString meshSelector = "all";
    AString indexType = "auto";
    AString defaultColorText = "1,1,1,1";
    f64 scale = 1.0;
    bool preserveSpace = false;
    bool includeHidden = false;
    bool bakeTransforms = true;
    bool importColors = true;
    bool flipWinding = false;
    bool deduplicate = true;
    bool forceOverwrite = false;
    bool acceptDefaults = false;
    bool listMeshes = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct MeshInstance{
    ufbx_node* node = nullptr;
    ufbx_mesh* mesh = nullptr;
    usize index = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneHandle{
    ufbx_scene* scene = nullptr;

    SceneHandle() = default;
    ~SceneHandle();
    SceneHandle(const SceneHandle&) = delete;
    SceneHandle& operator=(const SceneHandle&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AString Trim(AString value);
AString UnquotePath(AString value);
AString ToLower(AString value);
AString NormalizeAssetKind(AString value);
bool IsNormalizedDeformableGeometryKind(AStringView value);
bool IsNormalizedSkinnedGeometryKind(AStringView value);
bool IsDeformableGeometryKind(const AString& value);
bool IsSkinnedGeometryKind(const AString& value);
bool ValidateAssetKind(AString& inOutValue, AString& outError);
bool ParseColorText(const AString& text, Vec4& outColor);
bool Normalize(Vec3& value);
Vec4 BuildFallbackTangent(const Vec3& normal);
bool IsFiniteVertex(const GeometryVertex& vertex);
Path PathFromUtf8(const AString& value);
AString PathToUtf8(const Path& path);
Path DefaultOutputPath(const AString& inputPath);

bool LoadScene(const ImportOptions& options, SceneHandle& outScene, AString& outError);
UtilityVector<MeshInstance> CollectMeshInstances(ufbx_scene* scene, bool includeHidden);
void PrintMeshInstances(const UtilityVector<MeshInstance>& instances);
bool SelectMeshInstances(
    const UtilityVector<MeshInstance>& instances,
    const AString& selector,
    UtilityVector<usize>& outSelection,
    AString& outError
);
bool BuildGeometry(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    const Vec4& defaultColor,
    UtilityVector<GeometryVertex>& outVertices,
    UtilityVector<u32>& outIndices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    AString& outError
);

bool WriteNwbGeometry(
    const Path& outputPath,
    const UtilityVector<GeometryVertex>& vertices,
    const UtilityVector<u32>& indices,
    const AString& requestedIndexType,
    const AString& assetKind,
    AString& outIndexType,
    AString& outError
);

int Run(int argc, char** argv, bool& prompted);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

