// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/asset.h>
#include "skinned_geometry_runtime_mesh.h"


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


class SkinnedGeometryRuntimeMeshCache final : NoCopy{
private:
    struct SkinnedGeometrySource{
        Name sourceName = NAME_NONE;
        UniquePtr<Core::Assets::IAsset> asset;
        u32 referenceCount = 0;

        [[nodiscard]] const SkinnedGeometry* geometry()const;
    };


public:
    SkinnedGeometryRuntimeMeshCache(Core::Alloc::GlobalArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager);


public:
    void update(Core::ECS::World& world);
    void clear();

    [[nodiscard]] RuntimeMeshHandle handleForEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] SkinnedGeometryRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle);
    [[nodiscard]] const SkinnedGeometryRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle)const;
    [[nodiscard]] u32 editRevision(RuntimeMeshHandle handle)const;
    [[nodiscard]] bool bumpEditRevision(RuntimeMeshHandle handle, RuntimeMeshDirtyFlags dirtyFlags);

private:
    [[nodiscard]] bool ensureRuntimeMesh(Core::ECS::EntityID entity, SkinnedGeometryComponent& component);
    [[nodiscard]] bool ensureSourceLoaded(
        const Core::Assets::AssetRef<SkinnedGeometry>& sourceAsset,
        SkinnedGeometrySource*& outSource
    );
    [[nodiscard]] bool uploadRuntimeMeshBuffers(SkinnedGeometryRuntimeMeshInstance& instance);
    [[nodiscard]] RuntimeMeshHandle allocateHandle();
    void releaseRuntimeMesh(Core::ECS::EntityID entity);
    void releaseSource(const Name& sourceName);
    void eraseUnusedSource(const Name& sourceName);
    [[nodiscard]] Core::ECS::EntityID entityForHandle(RuntimeMeshHandle handle)const;
    [[nodiscard]] SkinnedGeometryRuntimeMeshInstance* findInstanceByEntity(Core::ECS::EntityID entity);
    [[nodiscard]] const SkinnedGeometryRuntimeMeshInstance* findInstanceByEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] Name deriveRuntimeBufferName(const SkinnedGeometryRuntimeMeshInstance& instance, AStringView suffix)const;
    [[nodiscard]] bool computePayloadBytes(
        const SkinnedGeometryRuntimeMeshInstance& instance,
        usize count,
        usize stride,
        usize& outBytes,
        const char* label
    )const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;

    HashMap<Name, SkinnedGeometrySource, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena> m_sources;
    HashMap<Core::ECS::EntityID, SkinnedGeometryRuntimeMeshInstance, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, Core::Alloc::GlobalArena> m_instances;
    HashMap<u64, Core::ECS::EntityID, Hasher<u64>, EqualTo<u64>, Core::Alloc::GlobalArena> m_handleToEntity;
    u64 m_nextHandleValue = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

