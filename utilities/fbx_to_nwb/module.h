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

struct MeshJointMatrix{
    Vec4 columns[4];
};
static_assert(sizeof(MeshJointMatrix) == sizeof(f32) * 16u);
static_assert(alignof(MeshJointMatrix) == alignof(f32));
static_assert(IsTriviallyCopyable_V<MeshJointMatrix>);

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
    AString meshClass = "mesh";
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
AString NormalizeMeshClassText(AString value);
AStringView MeshClassText(u32 meshClass);
AString MeshClassOptionsText();
AString MeshClassErrorText();
bool ParseMeshClassText(const AString& value, u32& outMeshClass);
bool ParseNormalizedMeshClassText(AStringView value, u32& outMeshClass);
bool MeshClassUsesSkinning(u32 meshClass);
bool IsNormalizedSkinnedMeshClass(AStringView value);
bool IsSkinnedMeshClass(const AString& value);
bool ValidateMeshClassText(AString& inOutValue);
AStringView NormalModeText(NormalMode::Enum normalMode);
AString NormalModeOptionsText();
AString NormalModeErrorText();
bool ParseNormalModeText(const AString& value, NormalMode::Enum& outNormalMode);
bool ParseNormalizedNormalModeText(AStringView value, NormalMode::Enum& outNormalMode);
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
    const Vec4& defaultColor,
    SourceMeshStreams& outMesh,
    u32& outSkeletonJointCount,
    UtilityVector<MeshJointMatrix>& outInverseBindMatrices,
    bool& outSawVertexColors,
    bool& outSawVertexUvs,
    SourceTangentReport& outTangentReport
);

bool WriteNwbMesh(
    const Path& outputPath,
    const SourceMeshStreams& mesh,
    const u32 skeletonJointCount,
    const UtilityVector<MeshJointMatrix>& inverseBindMatrices,
    const AString& meshClassText
);

int Run(int argc, char** argv, bool& prompted);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

