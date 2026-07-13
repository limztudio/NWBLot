// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/module.h>
#include "runtime_instance.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Graphics;


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
        Name sourceName = NAME_NONE;
        UniquePtr<Core::Assets::IAsset> meshAsset;
        UniquePtr<Core::Assets::IAsset> skinAsset;
        u32 referenceCount = 0;

        [[nodiscard]] const Mesh* mesh()const;
        [[nodiscard]] const Skin* skin()const;
    };


public:
    MeshSkinningRuntimeCache(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager);


public:
    void prepareResources(Core::ECS::World& world);
    void clear();

    [[nodiscard]] RuntimeMeshHandle handleForEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] MeshSkinningRuntimeInstance* findInstance(RuntimeMeshHandle handle);
    [[nodiscard]] const MeshSkinningRuntimeInstance* findInstance(RuntimeMeshHandle handle)const;
    [[nodiscard]] u32 editRevision(RuntimeMeshHandle handle)const;
    [[nodiscard]] bool bumpEditRevision(RuntimeMeshHandle handle, RuntimeMeshDirtyFlags dirtyFlags);

private:
    [[nodiscard]] bool ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component);
    [[nodiscard]] bool ensureSourceLoaded(
        const Core::Assets::AssetRef<Mesh>& meshAsset,
        const Core::Assets::AssetRef<Skin>& skinAsset,
        MeshSkinningSource*& outSource
    );
    [[nodiscard]] bool uploadRuntimeMeshBuffers(MeshSkinningRuntimeInstance& instance);
    [[nodiscard]] RuntimeMeshHandle allocateHandle();
    void releaseRuntimeMesh(Core::ECS::EntityID entity);
    void releaseSource(const Name& sourceName);
    void eraseUnusedSource(const Name& sourceName);
    [[nodiscard]] Core::ECS::EntityID entityForHandle(RuntimeMeshHandle handle)const;
    [[nodiscard]] MeshSkinningRuntimeInstance* findInstanceByEntity(Core::ECS::EntityID entity);
    [[nodiscard]] const MeshSkinningRuntimeInstance* findInstanceByEntity(Core::ECS::EntityID entity)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;

    HashMap<Name, MeshSkinningSource, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_sources;
    HashMap<Core::ECS::EntityID, MeshSkinningRuntimeInstance, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instances;
    HashMap<u64, Core::ECS::EntityID, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_handleToEntity;
    u64 m_nextHandleValue = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

