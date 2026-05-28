// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "geometry_payload.h"
#include "skinned_types.h"

#include <core/assets/module.h>
#include <core/mesh/classification.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedMesh final : public Core::Assets::TypedAsset<SkinnedMesh>, public MeshGeometryPayload{
public:
    NWB_DEFINE_ASSET_TYPE("skinned_mesh")


public:
    explicit SkinnedMesh(Core::Assets::AssetArena& arena)
        : MeshGeometryPayload(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
    {}
    SkinnedMesh(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<SkinnedMesh>(virtualPath)
        , MeshGeometryPayload(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setMeshClass(u32 meshClass){ m_meshClass = meshClass; }
    void setSkeletonJointCount(u32 jointCount){ m_skeletonJointCount = jointCount; }
    void setPayload(
        Core::Assets::AssetVector<Float3U>&& positions,
        Core::Assets::AssetVector<Half4U>&& normals,
        Core::Assets::AssetVector<Half4U>&& tangents,
        Core::Assets::AssetVector<Float2U>&& uv0,
        Core::Assets::AssetVector<Half4U>&& colors,
        Core::Assets::AssetVector<SkinInfluence4>&& skin,
        Core::Assets::AssetVector<SkinnedMeshJointMatrix>&& inverseBindMatrices,
        Core::Assets::AssetVector<MeshletDesc>&& meshlets,
        Core::Assets::AssetVector<MeshletBounds>&& meshletBounds,
        Core::Assets::AssetVector<u8>&& meshletPositionRefDeltas,
        Core::Assets::AssetVector<u8>&& meshletAttributeRefDeltas,
        Core::Assets::AssetVector<MeshletLocalVertexRef>&& meshletLocalVertexRefs,
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices
    ){
        m_skin = Move(skin);
        m_inverseBindMatrices = Move(inverseBindMatrices);
        setGeometryPayload(
            Move(positions),
            Move(normals),
            Move(tangents),
            Move(uv0),
            Move(colors),
            Move(meshlets),
            Move(meshletBounds),
            Move(meshletPositionRefDeltas),
            Move(meshletAttributeRefDeltas),
            Move(meshletLocalVertexRefs),
            Move(meshletPrimitiveIndices)
        );
    }

    [[nodiscard]] u32 meshClass()const{ return m_meshClass; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinInfluence4>& skinStream()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinnedMeshJointMatrix>& inverseBindMatrices()const{
        return m_inverseBindMatrices;
    }
private:
    u32 m_meshClass = Core::Mesh::MeshClass::Invalid;
    Core::Assets::AssetVector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Core::Assets::AssetVector<SkinnedMeshJointMatrix> m_inverseBindMatrices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedMeshAssetCodec final : public Core::Assets::AssetCodec<SkinnedMesh>{
public:
    SkinnedMeshAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

