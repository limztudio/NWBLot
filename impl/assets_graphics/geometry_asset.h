// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_class.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GeometryVertex{
    Float3U position;
    Float3U normal;
    Float4U color0;
};
static_assert(IsStandardLayout_V<GeometryVertex>, "GeometryVertex must stay binary-serializable");
static_assert(IsTriviallyCopyable_V<GeometryVertex>, "GeometryVertex must stay binary-serializable");
static_assert(sizeof(GeometryVertex) == sizeof(f32) * 10u, "GeometryVertex GPU/source layout drifted");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry final : public Core::Assets::TypedAsset<Geometry>{
public:
    NWB_DEFINE_ASSET_TYPE("geometry")


public:
    Geometry() = default;
    explicit Geometry(const Name& virtualPath)
        : Core::Assets::TypedAsset<Geometry>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setVertices(Vector<GeometryVertex>&& vertices){ m_vertices = Move(vertices); }
    void setIndices(Vector<u32>&& indices){ m_indices = Move(indices); }

    [[nodiscard]] const Vector<GeometryVertex>& vertices()const{ return m_vertices; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] u32 geometryClass()const{ return GeometryClass::Static; }


private:
    Vector<GeometryVertex> m_vertices;
    Vector<u32> m_indices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GeometryAssetCodec final : public Core::Assets::TypedAssetCodec<Geometry>{
public:
    GeometryAssetCodec() = default;

public:
    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

