// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/asset_manager.h>
#include <core/graphics/graphics.h>
#include <impl/ecs_deformable/deformable_runtime_mesh.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformableRuntimeMeshCache final : NoCopy, public IDeformableRuntimeMeshProvider{
private:
    struct DeformableGeometrySource{
        Name sourceName = NAME_NONE;
        UniquePtr<Core::Assets::IAsset> asset;
        u32 referenceCount = 0;

        [[nodiscard]] const DeformableGeometry* geometry()const;
    };

    using SourceMapAllocator = Core::Alloc::CustomAllocator<Pair<const Name, DeformableGeometrySource>>;
    using InstanceMapAllocator = Core::Alloc::CustomAllocator<Pair<const Core::ECS::EntityID, DeformableRuntimeMeshInstance>>;
    using HandleMapAllocator = Core::Alloc::CustomAllocator<Pair<const u64, Core::ECS::EntityID>>;


public:
    DeformableRuntimeMeshCache(Core::Alloc::CustomArena& arena, Core::Graphics& graphics, Core::Assets::AssetManager& assetManager);


public:
    void update(Core::ECS::World& world);

    [[nodiscard]] RuntimeMeshHandle handleForEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] DeformableRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle);
    [[nodiscard]] const DeformableRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle)const;
    [[nodiscard]] u32 editRevision(RuntimeMeshHandle handle)const;
    [[nodiscard]] bool bumpEditRevision(RuntimeMeshHandle handle, RuntimeMeshDirtyFlags dirtyFlags);

    [[nodiscard]] virtual RuntimeMeshHandle deformableRuntimeMeshHandle(Core::ECS::EntityID entity)const override;
    [[nodiscard]] virtual u32 deformableRuntimeMeshEditRevision(RuntimeMeshHandle handle)const override;
    [[nodiscard]] virtual bool bumpDeformableRuntimeMeshRevision(
        RuntimeMeshHandle handle,
        RuntimeMeshDirtyFlags dirtyFlags
    )override;
    [[nodiscard]] virtual DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(RuntimeMeshHandle handle)override;
    [[nodiscard]] virtual const DeformableRuntimeMeshInstance* findDeformableRuntimeMesh(
        RuntimeMeshHandle handle
    )const override;


private:
    [[nodiscard]] bool ensureRuntimeMesh(Core::ECS::EntityID entity, DeformableRendererComponent& component);
    [[nodiscard]] bool ensureSourceLoaded(
        const Core::Assets::AssetRef<DeformableGeometry>& sourceAsset,
        DeformableGeometrySource*& outSource
    );
    [[nodiscard]] bool uploadRuntimeMeshBuffers(DeformableRuntimeMeshInstance& instance);
    [[nodiscard]] RuntimeMeshHandle allocateHandle();
    void releaseRuntimeMesh(Core::ECS::EntityID entity);
    void releaseSource(const Name& sourceName);
    void eraseUnusedSource(const Name& sourceName);
    [[nodiscard]] Core::ECS::EntityID entityForHandle(RuntimeMeshHandle handle)const;
    [[nodiscard]] DeformableRuntimeMeshInstance* findInstanceByEntity(Core::ECS::EntityID entity);
    [[nodiscard]] const DeformableRuntimeMeshInstance* findInstanceByEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] Name deriveRuntimeBufferName(const DeformableRuntimeMeshInstance& instance, AStringView suffix)const;
    [[nodiscard]] bool computePayloadBytes(
        const DeformableRuntimeMeshInstance& instance,
        usize count,
        usize stride,
        usize& outBytes,
        const char* label
    )const;


private:
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;

    HashMap<Name, DeformableGeometrySource, Hasher<Name>, EqualTo<Name>, SourceMapAllocator> m_sources;
    HashMap<Core::ECS::EntityID, DeformableRuntimeMeshInstance, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>, InstanceMapAllocator> m_instances;
    HashMap<u64, Core::ECS::EntityID, Hasher<u64>, EqualTo<u64>, HandleMapAllocator> m_handleToEntity;
    u64 m_nextHandleValue = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

