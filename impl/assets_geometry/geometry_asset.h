// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"
#include "geometry_class.h"

#include <core/assets/asset.h>


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
    Geometry() = default;
    explicit Geometry(const Name& virtualPath)
        : Core::Assets::TypedAsset<Geometry>(virtualPath)
    {}


public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);
    [[nodiscard]] bool validatePayload()const;

public:
    void setStreams(Vector<Float3U>&& positions, Vector<Half4U>&& normals, Vector<Half4U>&& colors){
        m_positions = Move(positions);
        m_normals = Move(normals);
        m_colors = Move(colors);
    }
    void setIndices(Vector<u32>&& indices){ m_indices = Move(indices); }

    [[nodiscard]] const Vector<Float3U>& positions()const{ return m_positions; }
    [[nodiscard]] const Vector<Half4U>& normals()const{ return m_normals; }
    [[nodiscard]] const Vector<Half4U>& colors()const{ return m_colors; }
    [[nodiscard]] const Vector<u32>& indices()const{ return m_indices; }
    [[nodiscard]] usize vertexCount()const{ return m_positions.size(); }
    [[nodiscard]] u32 geometryClass()const{ return GeometryClass::Static; }


private:
    Vector<Float3U> m_positions;
    Vector<Half4U> m_normals;
    Vector<Half4U> m_colors;
    Vector<u32> m_indices;
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

