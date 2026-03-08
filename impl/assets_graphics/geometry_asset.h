// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <core/assets/asset.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Geometry final : public Core::Assets::IAsset{
public:
    static constexpr AStringView s_AssetTypeText = "geometry";


public:
    Geometry()
        : IAsset(AssetTypeName())
    {}
    explicit Geometry(const Name& virtualPath)
        : IAsset(AssetTypeName(), virtualPath)
    {}


public:
    [[nodiscard]] static const Name& AssetTypeName(){
        static const Name s_AssetType(s_AssetTypeText.data());
        return s_AssetType;
    }

public:
    bool loadBinary(const Core::Assets::AssetBytes& binary);

public:
    void setVertexLayout(u32 vertexStride){ m_vertexStride = vertexStride; }
    void setVertexData(const void* data, usize bytes);
    void setIndexData(const void* data, usize bytes, bool use32BitIndices);

    [[nodiscard]] u32 vertexStride()const{ return m_vertexStride; }
    [[nodiscard]] bool use32BitIndices()const{ return m_use32BitIndices; }
    [[nodiscard]] const Vector<u8>& vertexData()const{ return m_vertexData; }
    [[nodiscard]] const Vector<u8>& indexData()const{ return m_indexData; }


private:
    u32 m_vertexStride = 0;
    bool m_use32BitIndices = false;
    Vector<u8> m_vertexData;
    Vector<u8> m_indexData;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class GeometryAssetCodec final : public Core::Assets::IAssetCodec{
public:
    GeometryAssetCodec()
        : IAssetCodec(Geometry::AssetTypeName())
    {}

public:
    virtual bool deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const override;
#if defined(NWB_COOK)
    virtual bool serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const override;
#endif
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

