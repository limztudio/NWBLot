// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "runtime_cache.h"

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/ecs_mesh/runtime/mesh.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_skeleton/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
struct MeshSkinningRuntimeInstance;
struct MeshSkinningInfluenceGpu;

static_assert(
    SkeletonSkinningMode::LinearBlend == NWB_SKINNED_MESH_SKINNING_MODE_LINEAR_BLEND && SkeletonSkinningMode::DualQuaternion == NWB_SKINNED_MESH_SKINNING_MODE_DUAL_QUATERNION
    , "Skeleton skinning mode values must match the shader ABI"
);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSkinningSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public IRuntimeMeshProvider
{
private:
    struct MeshSkinningPushConstants{
        u32 meshletCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        u32 skinningMode = SkeletonSkinningMode::LinearBlend;
        u32 attributeCount = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
        u32 padding3 = 0;
    };
    static_assert(sizeof(MeshSkinningPushConstants) == NWB_SKINNED_MESH_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning push constants layout must match the shader ABI");
    static_assert(offsetof(MeshSkinningPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_MESHLET_COUNT, "MeshSkinning meshlet-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKIN_COUNT, "MeshSkinning skin-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, jointCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_JOINT_COUNT, "MeshSkinning joint-count push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, skinningMode) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_SKINNING_MODE, "MeshSkinning skinning-mode push offset drifted");
    static_assert(offsetof(MeshSkinningPushConstants, attributeCount) == sizeof(u32) * NWB_SKINNED_MESH_PUSH_ATTRIBUTE_COUNT, "MeshSkinning attribute-count push offset drifted");

    struct MeshletBoundsPushConstants{
        u32 meshletCount = 0;
        u32 padding0 = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletBoundsPushConstants) == NWB_SKINNED_MESH_BOUNDS_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning bounds push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletBoundsPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_BOUNDS_PUSH_MESHLET_COUNT, "MeshSkinning bounds meshlet-count push offset drifted");

    struct MeshletRepackPushConstants{
        u32 meshletCount = 0;
        u32 padding0 = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletRepackPushConstants) == NWB_SKINNED_MESH_REPACK_PUSH_CONSTANT_BYTE_SIZE, "MeshSkinning repack push constants layout must match the shader ABI");
    static_assert(offsetof(MeshletRepackPushConstants, meshletCount) == sizeof(u32) * NWB_SKINNED_MESH_REPACK_PUSH_MESHLET_COUNT, "MeshSkinning repack meshlet-count push offset drifted");

    struct RuntimeResources{
        RuntimeMeshHandle handle;
        u32 editRevision = 0;
        u32 positionCount = 0;
        u32 attributeCount = 0;
        u32 meshletCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        Core::BufferHandle skinBuffer;
        Core::BufferHandle jointPaletteBuffer;
        Core::BindingSetHandle skinningBindingSet;
        Core::BindingSetHandle boundsBindingSet;
        Core::BindingSetHandle repackBindingSet; // RT-only: per-frame skinned-normal -> RT attribute buffer repack (null when ray tracing is unsupported)

        [[nodiscard]] bool usesSkinning()const{ return skinCount != 0u && jointCount != 0u && skinningBindingSet != nullptr; }
    };

    struct RuntimePayloadViews{
        const MeshSkinningInfluenceGpu* skinInfluences = nullptr;
        const SkeletonJointMatrix* jointPalette = nullptr;
        usize skinInfluenceCount = 0;
        usize jointPaletteCount = 0;

        [[nodiscard]] bool hasActiveSkin()const{ return skinInfluenceCount != 0u && jointPaletteCount != 0u; }
    };


public:
    using ShaderPathResolveCallback = Function<
        bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)
    >;


public:
    MeshSkinningSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        IRuntimeMeshRegistry& runtimeMeshRegistry,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~MeshSkinningSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void invalidateResources()override;

    virtual bool resolveRuntimeMesh(Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh)override;
    virtual bool containsRuntimeMesh(const Name& meshKey, u64 version)override;

private:
    [[nodiscard]] bool ensureFrameCommandList();
    [[nodiscard]] bool ensureSkinningPipeline();
    [[nodiscard]] bool ensureBoundsPipeline();
    [[nodiscard]] bool ensureRepackPipeline();
    [[nodiscard]] bool dispatchRuntimeMesh(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const SkeletonJointPaletteComponent* jointPalette,
        const SkeletonPoseComponent* skeletonPose
    );
    [[nodiscard]] bool prepareRuntimeMeshResources(
        MeshSkinningRuntimeInstance& instance,
        const SkeletonJointPaletteComponent* jointPalette,
        const SkeletonPoseComponent* skeletonPose
    );
    [[nodiscard]] bool copyRestToSkinned(Core::CommandList& commandList, MeshSkinningRuntimeInstance& instance);
    [[nodiscard]] bool dispatchMeshletBounds(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const RuntimeResources& resources
    );
    [[nodiscard]] bool dispatchRepackNormals(
        Core::CommandList& commandList,
        MeshSkinningRuntimeInstance& instance,
        const RuntimeResources& resources
    );
    [[nodiscard]] bool ensureRuntimeResources(
        MeshSkinningRuntimeInstance& instance,
        const RuntimePayloadViews& payloadViews,
        RuntimeResources*& outResources,
        bool& outResourcesRebuilt
    );
    void pruneRuntimeResources();


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    IRuntimeMeshRegistry& m_runtimeMeshRegistry;
    ShaderPathResolveCallback m_shaderPathResolver;
    MeshSkinningRuntimeCache m_runtimeMeshCache;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_runtimeResources;
    Core::BindingLayoutHandle m_skinningBindingLayout;
    Core::ShaderHandle m_skinningComputeShader;
    Core::ComputePipelineHandle m_skinningComputePipeline;
    Core::BindingLayoutHandle m_boundsBindingLayout;
    Core::ShaderHandle m_boundsComputeShader;
    Core::ComputePipelineHandle m_boundsComputePipeline;
    Core::BindingLayoutHandle m_repackBindingLayout;
    Core::ShaderHandle m_repackComputeShader;
    Core::ComputePipelineHandle m_repackComputePipeline;
    Core::CommandListHandle m_renderCommandList;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

