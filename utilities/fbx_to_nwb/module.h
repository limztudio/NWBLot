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

struct MeshSkinInfluence{
    u16 joint[4] = {};
    f32 weight[4] = {};
};
static_assert(sizeof(MeshSkinInfluence) == sizeof(u16) * 4u + sizeof(f32) * 4u);
static_assert(alignof(MeshSkinInfluence) == alignof(f32));
static_assert(IsTriviallyCopyable_V<MeshSkinInfluence>);

struct JointMatrix{
    Vec4 rows[3];
};
static_assert(sizeof(JointMatrix) == sizeof(f32) * 12u);
static_assert(alignof(JointMatrix) == alignof(f32));
static_assert(IsTriviallyCopyable_V<JointMatrix>);

static constexpr u32 s_MissingSourceStreamIndex = Limit<u32>::s_Max;

struct SourceVertexRef{
    u32 position = s_MissingSourceStreamIndex;
    u32 normal = s_MissingSourceStreamIndex;
    u32 tangent = s_MissingSourceStreamIndex;
    u32 uv0 = s_MissingSourceStreamIndex;
    u32 color = s_MissingSourceStreamIndex;
    u32 skin = s_MissingSourceStreamIndex;
};
static_assert(IsTriviallyCopyable_V<SourceVertexRef>);

struct SourceMeshStreams{
    UtilityVector<Vec3> positions;
    UtilityVector<Vec3> normals;
    UtilityVector<Vec4> tangents;
    UtilityVector<Vec2> uv0;
    UtilityVector<Vec4> colors;
    UtilityVector<MeshSkinInfluence> skin;
    UtilityVector<SourceVertexRef> vertexRefs;
    UtilityVector<u32> indices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NormalMode{
    enum Enum : u8{
        Imported,
        Smooth,
        Regenerate
    };
};

namespace SourceTangentMode{
    enum Enum : u8{
        Imported,
        GeneratedUv,
        GeneratedFallback
    };
};

namespace OutputAssetType{
    enum Enum : u8{
        Mesh,
        Model
    };
};

struct SourceTangentReport{
    SourceTangentMode::Enum mode = SourceTangentMode::Imported;
    u32 degenerateUvTriangleCount = 0u;
    u32 fallbackTangentVertexCount = 0u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_DefaultTriangleAreaLengthSquaredEpsilon = 1.0e-20;

struct ImportOptions{
    AString inputPath;
    AString outputPath;
    AString assetType = "mesh";
    AString virtualRoot = "project";
    AString meshSelector = "all";
    AString normalMode = "imported";
    AString defaultColorText = "1,1,1,1";
    f64 scale = 1.0;
    f64 triangleAreaLengthSquaredEpsilon = s_DefaultTriangleAreaLengthSquaredEpsilon;
    bool preserveSpace = false;
    bool includeHidden = false;
    bool bakeTransforms = true;
    bool importColors = true;
    bool flipWinding = false;
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
AString OutputAssetTypeOptionsText();
AString OutputAssetTypeErrorText();
bool ParseAssetTypeText(const AString& value, OutputAssetType::Enum& outAssetType);
bool ValidateAssetTypeText(AString& inOutValue);
AString NormalModeOptionsText();
AString NormalModeErrorText();
bool ParseNormalModeText(const AString& value, NormalMode::Enum& outNormalMode);
bool ValidateNormalModeText(AString& inOutValue);
AStringView SourceTangentModeText(SourceTangentMode::Enum mode);
bool ParseColorText(const AString& text, Vec4& outColor);
bool Normalize(Vec3& value);
Path PathFromUtf8(const AString& value);
AString PathToUtf8(const Path& path);
Path DefaultOutputPath(const AString& inputPath);

bool LoadScene(const ImportOptions& options, SceneHandle& outScene);
UtilityVector<MeshInstance> CollectMeshInstances(ufbx_scene* scene, bool includeHidden);
void PrintMeshInstances(const UtilityVector<MeshInstance>& instances);
bool SelectMeshInstances(
    const UtilityVector<MeshInstance>& instances,
    const AString& selector,
    UtilityVector<usize>& outSelection
);
bool BuildMesh(
    const UtilityVector<MeshInstance>& instances,
    const UtilityVector<usize>& selection,
    const ImportOptions& options,
    bool wantsSkinning,
    const Vec4& defaultColor,
    SourceMeshStreams& outMesh,
    UtilityVector<ufbx_node*>& outSkeletonJoints,
    UtilityVector<JointMatrix>& outSkeletonBindPoseMatrices,
    UtilityVector<JointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    SourceTangentReport& outTangentReport
);

bool WriteNwbAsset(
    const Path& outputPath,
    const SourceMeshStreams& mesh,
    const AString& assetTypeText,
    const AString& virtualRoot,
    const UtilityVector<ufbx_node*>& skeletonJoints,
    const UtilityVector<JointMatrix>& skeletonBindPoseMatrices,
    const UtilityVector<JointMatrix>& inverseBindMatrices
);

int Run(int argc, char** argv, bool& prompted);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

