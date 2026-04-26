// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <core/assets/asset_manager.h>
#include <core/graphics/graphics.h>
#include <impl/assets_graphics/deformable_geometry_asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class World;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ECS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeformableRuntimeMeshInstance{
    Core::ECS::EntityID entity = Core::ECS::ENTITY_ID_INVALID;
    RuntimeMeshHandle handle;
    Core::Assets::AssetRef<DeformableGeometry> source;
    Vector<DeformableVertexRest> restVertices;
    Vector<u32> indices;
    u32 sourceTriangleCount = 0;
    Vector<SkinInfluence4> skin;
    Vector<SourceSample> sourceSamples;
    Vector<DeformableEditMaskFlags> editMaskPerTriangle;
    DeformableDisplacement displacement;
    Vector<DeformableMorph> morphs;
    Core::BufferHandle restVertexBuffer;
    Core::BufferHandle indexBuffer;
    Core::BufferHandle deformedVertexBuffer;
    u32 editRevision = 0;
    RuntimeMeshDirtyFlags dirtyFlags = RuntimeMeshDirtyFlag::All;

    [[nodiscard]] bool valid()const{
        return entity.valid()
            && handle.valid()
            && source.valid()
            && !restVertices.empty()
            && !indices.empty()
            && (sourceSamples.empty() || sourceTriangleCount != 0u)
            && (dirtyFlags & RuntimeMeshDirtyFlag::GpuUploadDirty) == 0u
            && restVertexBuffer != nullptr
            && indexBuffer != nullptr
            && deformedVertexBuffer != nullptr
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class DeformableRuntimeMeshCache final : NoCopy{
private:
    struct DeformableGeometrySource{
        Name sourceName = NAME_NONE;
        UniquePtr<Core::Assets::IAsset> asset;
        u32 referenceCount = 0;

        [[nodiscard]] const DeformableGeometry* geometry()const;
    };


public:
    DeformableRuntimeMeshCache(Core::Graphics& graphics, Core::Assets::AssetManager& assetManager);


public:
    void update(Core::ECS::World& world);

    [[nodiscard]] RuntimeMeshHandle handleForEntity(Core::ECS::EntityID entity)const;
    [[nodiscard]] DeformableRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle);
    [[nodiscard]] const DeformableRuntimeMeshInstance* findInstance(RuntimeMeshHandle handle)const;
    [[nodiscard]] u32 editRevision(RuntimeMeshHandle handle)const;
    [[nodiscard]] bool bumpEditRevision(RuntimeMeshHandle handle, RuntimeMeshDirtyFlags dirtyFlags);


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
    HashMap<Name, DeformableGeometrySource, Hasher<Name>, EqualTo<Name>> m_sources;
    HashMap<Core::ECS::EntityID, DeformableRuntimeMeshInstance, Hasher<Core::ECS::EntityID>, EqualTo<Core::ECS::EntityID>> m_instances;
    HashMap<u64, Core::ECS::EntityID, Hasher<u64>, EqualTo<u64>> m_handleToEntity;
    u64 m_nextHandleValue = 1u;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

