// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>
#include <core/geometry/geometry_class.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Half4U MakeGeometryNormalStreamValue(const Float3U& normal)noexcept{
    return MakeHalf4U(normal.x, normal.y, normal.z, 0.0f);
}

[[nodiscard]] inline Half4U MakeGeometryColorStreamValue(const Float4U& color0)noexcept{
    return MakeHalf4U(color0.x, color0.y, color0.z, color0.w);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry final : public Core::Assets::TypedAsset<Geometry>{
public:
    NWB_DEFINE_ASSET_TYPE("geometry")


public:
    explicit Geometry(Core::Assets::AssetArena& arena)
        : m_positions(arena)
        , m_normals(arena)
        , m_colors(arena)
        , m_indices(arena)
    {}
    Geometry(Core::Assets::AssetArena& arena, const Name& virtualPath)
        : Core::Assets::TypedAsset<Geometry>(virtualPath)
        , m_positions(arena)
        , m_normals(arena)
        , m_colors(arena)
        , m_indices(arena)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setStreams(
        Core::Assets::AssetVector<Float3U>&& positions,
        Core::Assets::AssetVector<Half4U>&& normals,
        Core::Assets::AssetVector<Half4U>&& colors
    ){
        m_positions = Move(positions);
        m_normals = Move(normals);
        m_colors = Move(colors);
    }
    void setIndices(Core::Assets::AssetVector<u32>&& indices){ m_indices = Move(indices); }

    [[nodiscard]] const Core::Assets::AssetVector<Float3U>& positions()const{ return m_positions; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& normals()const{ return m_normals; }
    [[nodiscard]] const Core::Assets::AssetVector<Half4U>& colors()const{ return m_colors; }
    [[nodiscard]] const Core::Assets::AssetVector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] usize vertexCount()const{ return m_positions.size(); }
    [[nodiscard]] u32 geometryClass()const{ return Core::Geometry::GeometryClass::Static; }


private:
    Core::Assets::AssetVector<Float3U> m_positions;
    Core::Assets::AssetVector<Half4U> m_normals;
    Core::Assets::AssetVector<Half4U> m_colors;
    Core::Assets::AssetVector<u32> m_indices;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GeometryAssetCodec final : public Core::Assets::AssetCodec<Geometry>{
public:
    GeometryAssetCodec() = default;


#if defined(NWB_COOK)
public:
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

