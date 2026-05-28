// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "payload_types.h"
#include "skinned_types.h"

#include <core/assets/module.h>
#include <core/mesh/classification.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SkinnedMesh final : public Core::Assets::TypedAsset<SkinnedMesh>{
public:
    NWB_DEFINE_ASSET_TYPE("skinned_mesh")


public:
    explicit SkinnedMesh(Core::Assets::AssetArena& arena)
        : m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefs(arena)
        , m_meshletAttributeRefs(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
    {}
    SkinnedMesh(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<SkinnedMesh>(virtualPath)
        , m_positionStream(arena)
        , m_normalStream(arena)
        , m_tangentStream(arena)
        , m_uv0Stream(arena)
        , m_colorStream(arena)
        , m_skin(arena)
        , m_inverseBindMatrices(arena)
        , m_meshlets(arena)
        , m_meshletBounds(arena)
        , m_meshletPositionRefs(arena)
        , m_meshletAttributeRefs(arena)
        , m_meshletLocalVertexRefs(arena)
        , m_meshletPrimitiveIndices(arena)
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
        Core::Assets::AssetVector<MeshletDeformedPositionRef>&& meshletPositionRefs,
        Core::Assets::AssetVector<MeshletShadingAttributeRef>&& meshletAttributeRefs,
        Core::Assets::AssetVector<MeshletLocalVertexRef>&& meshletLocalVertexRefs,
        Core::Assets::AssetVector<u8>&& meshletPrimitiveIndices
    ){
        m_positionStream = Move(positions);
        m_normalStream = Move(normals);
        m_tangentStream = Move(tangents);
        m_uv0Stream = Move(uv0);
        m_colorStream = Move(colors);
        m_skin = Move(skin);
        m_inverseBindMatrices = Move(inverseBindMatrices);
        m_meshlets = Move(meshlets);
        m_meshletBounds = Move(meshletBounds);
        m_meshletPositionRefs = Move(meshletPositionRefs);
        m_meshletAttributeRefs = Move(meshletAttributeRefs);
        m_meshletLocalVertexRefs = Move(meshletLocalVertexRefs);
        m_meshletPrimitiveIndices = Move(meshletPrimitiveIndices);
    }

    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positionStream()const{ return m_positionStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normalStream()const{ return m_normalStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& tangentStream()const{ return m_tangentStream; }
    [[nodiscard]] const Core::Assets::AssetVector<Float2U>& uv0Stream()const{ return m_uv0Stream; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colorStream()const{ return m_colorStream; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletDesc>& meshlets()const{ return m_meshlets; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletBounds>& meshletBounds()const{ return m_meshletBounds; }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletDeformedPositionRef>& meshletPositionRefs()const{
        return m_meshletPositionRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletShadingAttributeRef>& meshletAttributeRefs()const{
        return m_meshletAttributeRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<MeshletLocalVertexRef>& meshletLocalVertexRefs()const{
        return m_meshletLocalVertexRefs;
    }
    [[nodiscard]] const Core::Assets::AssetVector<u8>& meshletPrimitiveIndices()const{ return m_meshletPrimitiveIndices; }
    [[nodiscard]] u32 meshClass()const{ return m_meshClass; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinInfluence4>& skinStream()const{ return m_skin; }
    [[nodiscard]] u32 skeletonJointCount()const{ return m_skeletonJointCount; }
    [[nodiscard]] const Core::Assets::AssetVector<SkinnedMeshJointMatrix>& inverseBindMatrices()const{
        return m_inverseBindMatrices;
    }
private:
    u32 m_meshClass = Core::Mesh::MeshClass::Invalid;
    Core::Assets::AssetVector<Float3U> m_positionStream;
    Core::Assets::AssetVector<Half4U> m_normalStream;
    Core::Assets::AssetVector<Half4U> m_tangentStream;
    Core::Assets::AssetVector<Float2U> m_uv0Stream;
    Core::Assets::AssetVector<Half4U> m_colorStream;
    Core::Assets::AssetVector<SkinInfluence4> m_skin;
    u32 m_skeletonJointCount = 0;
    Core::Assets::AssetVector<SkinnedMeshJointMatrix> m_inverseBindMatrices;
    Core::Assets::AssetVector<MeshletDesc> m_meshlets;
    Core::Assets::AssetVector<MeshletBounds> m_meshletBounds;
    Core::Assets::AssetVector<MeshletDeformedPositionRef> m_meshletPositionRefs;
    Core::Assets::AssetVector<MeshletShadingAttributeRef> m_meshletAttributeRefs;
    Core::Assets::AssetVector<MeshletLocalVertexRef> m_meshletLocalVertexRefs;
    Core::Assets::AssetVector<u8> m_meshletPrimitiveIndices;
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

NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

