// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <impl/assets_mesh/payload_types.adhd.h>
#include <impl/assets_skeleton/joint_types.adhd.h>

#include <core/assets/paths.h>
#include <core/task/cpu/scheduler.h>

#include <global/allocation_size.h>
#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Vec2 = Float2U;
using Vec3 = Float3U;
using Vec4 = Float4U;

static constexpr usize s_Vector2ComponentCount = 2u;
static constexpr usize s_Vector3ComponentCount = 3u;
static constexpr usize s_Vector4ComponentCount = 4u;
static constexpr usize s_MaxSkeletonJointCount = static_cast<usize>(Limit<u16>::s_Max) + 1u;

using MeshSkinInfluence = Impl::SkinInfluence4;
static_assert(sizeof(MeshSkinInfluence) == sizeof(u16) * Impl::s_SkinInfluenceJointCount + sizeof(Float4U));
static_assert(alignof(MeshSkinInfluence) <= alignof(f32));
static_assert(IsTriviallyCopyable_V<MeshSkinInfluence>);

struct MeshSkinInfluenceEqual{
public:
    b8 operator()(const MeshSkinInfluence& lhs, const MeshSkinInfluence& rhs)const noexcept;
};

// Unaligned affine storage crosses the importer's persistent/SIMD boundary through LoadFloat()/StoreFloat().
using JointMatrix = Float34U;
static_assert(sizeof(JointMatrix) == sizeof(f32) * Impl::s_SkeletonJointMatrixFloatCount);
static_assert(alignof(JointMatrix) == alignof(f32));
static_assert(IsTriviallyCopyable_V<JointMatrix>);

struct SourceVertexRef{
    u32 position = Impl::s_MeshMissingStreamIndex;
    u32 normal = Impl::s_MeshMissingStreamIndex;
    u32 tangent = Impl::s_MeshMissingStreamIndex;
    u32 uv0 = Impl::s_MeshMissingStreamIndex;
    u32 color = Impl::s_MeshMissingStreamIndex;
    u32 skin = Impl::s_MeshMissingStreamIndex;
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

struct SourceMeshStreamCounts{
    usize positions = 0u;
    usize normals = 0u;
    usize tangents = 0u;
    usize uv0 = 0u;
    usize colors = 0u;
    usize skin = 0u;
    usize vertexRefs = 0u;
    usize indices = 0u;
};

struct SourceMeshCanonicalizeReport{
    SourceMeshStreamCounts before;
    SourceMeshStreamCounts after;
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
        Bunch,
        Mesh,
        Model,
        Skeleton,
        Skin
    };
};

struct SourceTangentReport{
    SourceTangentMode::Enum mode = SourceTangentMode::Imported;
    u32 degenerateUvTriangleCount = 0u;
    u32 fallbackTangentVertexCount = 0u;
};

struct MeshBuildResult{
    SourceMeshStreams mesh;
    UtilityVector<ufbx_node*> skeletonJoints;
    UtilityVector<JointMatrix> skeletonBindPoseMatrices;
    UtilityVector<JointMatrix> inverseBindMatrices;
    b8 sawVertexColors = false;
    b8 sawVertexUvs = false;
    SourceTangentReport tangentReport;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f64 s_DefaultTriangleAreaLengthSquaredEpsilon = 1.0e-20;
static constexpr f64 s_DefaultImportScale = 1.0;
inline constexpr AStringView s_DefaultOutputAssetTypeText = "bunch";
inline constexpr AStringView s_MeshAssetTypeText = "mesh";
inline constexpr AStringView s_ModelAssetTypeText = "model";
inline constexpr AStringView s_SkeletonAssetTypeText = "skeleton";
inline constexpr AStringView s_SkinAssetTypeText = "skin";
inline constexpr AStringView s_DefaultVirtualRootText = "project";
inline constexpr AStringView s_DefaultMeshSelectorText = "all";
inline constexpr AStringView s_FirstMeshSelectorText = "first";
inline constexpr AStringView s_DefaultNormalModeText = "imported";
inline constexpr AStringView s_SmoothNormalModeText = "smooth";
inline constexpr AStringView s_RegenerateNormalModeText = "regenerate";
inline constexpr AStringView s_DefaultColorText = "1,1,1,1";
inline constexpr AStringView s_GeneratedUvTangentModeText = "generated_uv";
inline constexpr AStringView s_GeneratedFallbackTangentModeText = "generated_fallback";
inline constexpr AStringView s_SkinStreamLabel = "skin";
inline constexpr AStringView s_ImportedSourceLabel = "imported";
inline constexpr AStringView s_DefaultSourceLabel = "default";
inline constexpr AStringView s_SeparateAssetLayoutLabel = "separate";
inline constexpr AStringView s_ReportLineEndText = "\n";

struct ImportOptions{
    AString inputPath;
    AString outputPath;
    AString assetType = AString(s_DefaultOutputAssetTypeText);
    AString virtualRoot = AString(s_DefaultVirtualRootText);
    AString meshSelector = AString(s_DefaultMeshSelectorText);
    AString normalMode = AString(s_DefaultNormalModeText);
    AString defaultColorText = AString(s_DefaultColorText);
    f64 scale = s_DefaultImportScale;
    f64 triangleAreaLengthSquaredEpsilon = s_DefaultTriangleAreaLengthSquaredEpsilon;
    b8 preserveSpace = false;
    b8 includeHidden = false;
    b8 bakeTransforms = true;
    b8 importColors = true;
    b8 flipWinding = false;
    b8 separateAssets = false;
    b8 refreshNwb = false;
    b8 forceOverwrite = false;
    b8 acceptDefaults = false;
    b8 listMeshes = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct MeshInstance{
    ufbx_node* node = nullptr;
    ufbx_mesh* mesh = nullptr;
    usize index = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SceneHandle{
public:
    ufbx_scene* scene = nullptr;


public:
    SceneHandle()noexcept = default;
    SceneHandle(SceneHandle&& other)noexcept
        : scene(Exchange(other.scene, nullptr))
    {}
    SceneHandle(const SceneHandle&) = delete;
    ~SceneHandle()noexcept;


public:
    SceneHandle& operator=(const SceneHandle&) = delete;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] constexpr AStringView UfbxStringView(const ufbx_string value)noexcept{
    return (value.data && value.length != 0u) ? AStringView(value.data, value.length) : AStringView{};
}

[[nodiscard]] ScratchString OutputAssetTypeOptionsText(Core::Alloc::ScratchArena& scratchArena);
ScratchString OutputAssetTypeErrorText(Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<OutputAssetType::Enum> ParseAssetTypeText(AStringView value)noexcept;
b8 ValidateAssetTypeText(AString& inOutValue);
[[nodiscard]] ScratchString NormalModeOptionsText(Core::Alloc::ScratchArena& scratchArena);
ScratchString NormalModeErrorText(Core::Alloc::ScratchArena& scratchArena);
[[nodiscard]] Expected<NormalMode::Enum> ParseNormalModeText(AStringView value)noexcept;
b8 ValidateNormalModeText(AString& inOutValue);
AStringView SourceTangentModeText(SourceTangentMode::Enum mode)noexcept;
Expected<Vec4> ParseColorText(const AStringView text, Core::Alloc::ScratchArena& scratchArena);
ScratchPath DefaultOutputPath(const AStringView inputPath, Core::Alloc::ScratchArena& scratchArena);

Expected<SceneHandle> LoadScene(const ImportOptions& options, Core::Alloc::ScratchArena& scratchArena);
UtilityVector<MeshInstance> CollectMeshInstances(const ufbx_scene& scene, b8 includeHidden);
void PrintMeshInstances(Span<const MeshInstance> instances, Core::Alloc::ScratchArena& scratchArena);
Expected<UtilityVector<usize>> SelectMeshInstances(Span<const MeshInstance> instances, AStringView selector);
Expected<MeshBuildResult> BuildMesh(
    Span<const MeshInstance> instances,
    Span<const usize> selection,
    const ImportOptions& options,
    b8 wantsSkinning,
    const Vec4& defaultColor,
    Core::CpuTaskScheduler& cpuScheduler,
    Core::Alloc::ScratchArena& scratchArena
);

[[nodiscard]] SourceMeshStreamCounts CountSourceMeshStreams(const SourceMeshStreams& mesh)noexcept;
[[nodiscard]] Expected<SourceMeshCanonicalizeReport> CanonicalizeSourceMeshStreams(Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, SourceMeshStreams& inOutMesh);
[[nodiscard]] Expected<SourceMeshCanonicalizeReport> RefreshNwbMeshAsset(const ScratchPath& inputPath, const ScratchPath& outputPath, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena);

b8 WriteNwbAsset(
    const ScratchPath& outputPath,
    const SourceMeshStreams& mesh,
    const AStringView assetTypeText,
    const AStringView virtualRoot,
    b8 separateAssets,
    Span<ufbx_node* const> skeletonJoints,
    Span<const JointMatrix> skeletonBindPoseMatrices,
    Span<const JointMatrix> inverseBindMatrices,
    Core::Alloc::ScratchArena& scratchArena
);

i32 Run(i32 argc, char** argv, Core::CpuTaskScheduler& cpuScheduler, Core::Alloc::ScratchArena& scratchArena, b8& prompted);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

