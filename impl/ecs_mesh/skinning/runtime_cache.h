// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/alloc/scratch.h>
#include <core/assets/module.h>

#include "runtime_instance.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GraphicsRuntime;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MeshSkinningRuntimeCache final : NoCopy{
private:
    struct MeshSkinningSource{
        UniquePtr<Core::Assets::IAsset> meshAsset;
        UniquePtr<Core::Assets::IAsset> skinAsset;
        u32 referenceCount = 0;


    public:
        [[nodiscard]] const Mesh& mesh()const noexcept;
        [[nodiscard]] const Skin& skin()const noexcept;
    };


public:
    MeshSkinningRuntimeCache(Core::Alloc::GlobalArena& arena, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager);


public:
    void prepareResources(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena);
    void clear()noexcept;

    [[nodiscard]] MeshSkinningRuntimeInstance* findInstance(RuntimeMeshHandle handle)noexcept;
    [[nodiscard]] const MeshSkinningRuntimeInstance* findInstance(RuntimeMeshHandle handle)const noexcept;


private:
    [[nodiscard]] bool ensureRuntimeMesh(Core::ECS::EntityID entity, Core::Alloc::ScratchArena& scratchArena, SkinnedMeshBindingComponent& component);
    [[nodiscard]] Expected<NotNull<MeshSkinningSource*>> ensureSourceLoaded(const Core::Assets::AssetRef<Skin>& skinAsset);
    [[nodiscard]] bool uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance, Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Expected<RuntimeMeshHandle> allocateHandle();
    void releaseRuntimeMesh(Core::ECS::EntityID entity)noexcept;
    void releaseSource(const Name& sourceName)noexcept;
    void eraseUnusedSource(const Name& sourceName)noexcept;
    [[nodiscard]] Core::ECS::EntityID entityForHandle(RuntimeMeshHandle handle)const noexcept;
    [[nodiscard]] MeshSkinningRuntimeInstance* findInstanceByEntity(Core::ECS::EntityID entity)noexcept;
    [[nodiscard]] const MeshSkinningRuntimeInstance* findInstanceByEntity(Core::ECS::EntityID entity)const noexcept;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::GraphicsRuntime& m_graphics;
    Core::Assets::AssetManager& m_assetManager;

    HashMap<Name, MeshSkinningSource, Core::Alloc::GlobalArena, Hasher<Name>, EqualTo<Name>> m_sources;
    HashMap<Core::ECS::EntityID, MeshSkinningRuntimeInstance, Core::Alloc::GlobalArena, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>> m_instances;
    HashMap<u64, Core::ECS::EntityID, Core::Alloc::GlobalArena, Hasher<u64>, EqualTo<u64>> m_handleToEntity;
    u64 m_nextHandleValue = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

