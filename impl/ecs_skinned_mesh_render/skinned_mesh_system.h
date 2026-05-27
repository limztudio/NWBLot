// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <impl/ecs_mesh/runtime_mesh.h>
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
    struct RuntimeResources{
        RuntimeMeshHandle handle;
        u32 editRevision = 0;
        u32 positionCount = 0;
        u32 attributeCount = 0;
        u32 skinCount = 0;
        u32 jointCount = 0;
        Core::BufferHandle skinBuffer;
        Core::BufferHandle jointPaletteBuffer;
        Core::BindingSetHandle bindingSet;
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
    virtual void render(Core::IFramebuffer* framebuffer)override;
    virtual void invalidateResources()override;

    virtual bool resolveRuntimeMesh(Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh)override;
    virtual bool containsRuntimeMesh(const Name& meshKey, u64 version)override;

private:
    [[nodiscard]] bool ensurePipeline();
    [[nodiscard]] bool dispatchRuntimeMesh(
        Core::ICommandList& commandList,
        SkinnedMeshRuntimeMeshInstance& instance,
        const SkinnedMeshJointPaletteComponent* jointPalette,
        const SkinnedMeshSkeletonPoseComponent* skeletonPose
    );
    [[nodiscard]] bool copyRestToSkinned(Core::ICommandList& commandList, SkinnedMeshRuntimeMeshInstance& instance);
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
    Core::BindingLayoutHandle m_bindingLayout;
    Core::ShaderHandle m_computeShader;
    Core::ComputePipelineHandle m_computePipeline;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

