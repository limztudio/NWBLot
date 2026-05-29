// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <impl/ecs_mesh_runtime/mesh.h>
#include <impl/ecs_skinned_mesh/components.h>


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
class SkinnedMeshRuntimeMeshCache;
struct SkinnedMeshRuntimeMeshInstance;
struct SkinnedMeshSkinInfluenceGpu;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedMeshSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public IRuntimeMeshProvider
{
private:
    struct SkinnedMeshPushConstants{
        u32 meshletCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        u32 skinningMode = SkinnedMeshSkinningMode::LinearBlend;
        u32 attributeCount = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
        u32 padding3 = 0;
    };
    static_assert(sizeof(SkinnedMeshPushConstants) == 32, "SkinnedMesh push constants layout must match the shader ABI");

    struct MeshletBoundsPushConstants{
        u32 meshletCount = 0;
        u32 padding0 = 0;
        u32 padding1 = 0;
        u32 padding2 = 0;
    };
    static_assert(sizeof(MeshletBoundsPushConstants) == 16, "SkinnedMesh bounds push constants layout must match the shader ABI");

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

        [[nodiscard]] bool usesSkinning()const{ return skinCount != 0u && jointCount != 0u && skinningBindingSet != nullptr; }
    };

    struct RuntimePayloadViews{
        const SkinnedMeshSkinInfluenceGpu* skinInfluences = nullptr;
        const SkinnedMeshJointMatrix* jointPalette = nullptr;
        usize skinInfluenceCount = 0;
        usize jointPaletteCount = 0;

        [[nodiscard]] bool hasActiveSkin()const{ return skinInfluenceCount != 0u && jointPaletteCount != 0u; }
    };


public:
    using ShaderPathResolveCallback = Function<
        bool(const Name& shaderName, AStringView variantName, const Name& stageName, Name& outVirtualPath)
    >;


public:
    SkinnedMeshSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        IRuntimeMeshRegistry& runtimeMeshRegistry,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~SkinnedMeshSystem()override;


public:
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void invalidateResources()override;

    virtual bool resolveRuntimeMesh(Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh)override;
    virtual bool containsRuntimeMesh(const Name& meshKey, u64 version)override;

private:
    [[nodiscard]] bool ensureSkinningPipeline();
    [[nodiscard]] bool ensureBoundsPipeline();
    [[nodiscard]] bool dispatchRuntimeMesh(
        Core::CommandList& commandList,
        SkinnedMeshRuntimeMeshInstance& instance,
        const SkinnedMeshJointPaletteComponent* jointPalette,
        const SkinnedMeshSkeletonPoseComponent* skeletonPose
    );
    [[nodiscard]] bool copyRestToSkinned(Core::CommandList& commandList, SkinnedMeshRuntimeMeshInstance& instance);
    [[nodiscard]] bool dispatchMeshletBounds(
        Core::CommandList& commandList,
        SkinnedMeshRuntimeMeshInstance& instance,
        const RuntimeResources& resources
    );
    [[nodiscard]] bool ensureRuntimeResources(
        SkinnedMeshRuntimeMeshInstance& instance,
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
    Core::GlobalUniquePtr<SkinnedMeshRuntimeMeshCache> m_runtimeMeshCache;

    HashMap<u64, RuntimeResources, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_runtimeResources;
    Core::BindingLayoutHandle m_skinningBindingLayout;
    Core::ShaderHandle m_skinningComputeShader;
    Core::ComputePipelineHandle m_skinningComputePipeline;
    Core::BindingLayoutHandle m_boundsBindingLayout;
    Core::ShaderHandle m_boundsComputeShader;
    Core::ComputePipelineHandle m_boundsComputePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

