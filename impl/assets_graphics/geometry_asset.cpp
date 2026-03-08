// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include <logger/client/logger.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_GeometryMagic = 0x47454F31u; // GEO1
static constexpr u32 s_GeometryVersion = 1u;


UniquePtr<Core::Assets::IAssetCodec> CreateGeometryAssetCodec(){
    return MakeUnique<GeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_GeometryAssetCodecAutoRegistrar(&CreateGeometryAssetCodec);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void Geometry::setVertexData(const void* data, const usize bytes){
    m_vertexData.resize(bytes);
    if(data && bytes > 0)
        NWB_MEMCPY(m_vertexData.data(), bytes, data, bytes);
}

void Geometry::setIndexData(const void* data, const usize bytes, const bool use32BitIndices){
    m_use32BitIndices = use32BitIndices;
    m_indexData.resize(bytes);
    if(data && bytes > 0)
        NWB_MEMCPY(m_indexData.data(), bytes, data, bytes);
}


bool Geometry::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_vertexStride = 0;
    m_use32BitIndices = false;
    m_vertexData.clear();
    m_indexData.clear();

    usize cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u32 vertexStride = 0;
    u8 use32BitIndices = 0;
    u64 vertexBytes = 0;
    u64 indexBytes = 0;
    if(!ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || !ReadPOD(binary, cursor, vertexStride)
        || !ReadPOD(binary, cursor, use32BitIndices)
        || !ReadPOD(binary, cursor, vertexBytes)
        || !ReadPOD(binary, cursor, indexBytes))
    {
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed header"));
        return false;
    }

    if(magic != __hidden_assets::s_GeometryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: invalid magic"));
        return false;
    }
    if(version != __hidden_assets::s_GeometryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: unsupported version {}"), version);
        return false;
    }
    if(vertexStride == 0 || vertexBytes == 0 || indexBytes == 0){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: geometry payload is empty"));
        return false;
    }
    if(vertexBytes > static_cast<u64>(Limit<usize>::s_Max) || indexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: geometry payload exceeds addressable size"));
        return false;
    }

    m_vertexStride = vertexStride;
    m_use32BitIndices = (use32BitIndices != 0);

    m_vertexData.resize(static_cast<usize>(vertexBytes));
    if(cursor > binary.size() || binary.size() - cursor < m_vertexData.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed vertex payload"));
        return false;
    }
    NWB_MEMCPY(m_vertexData.data(), m_vertexData.size(), binary.data() + cursor, m_vertexData.size());
    cursor += m_vertexData.size();

    m_indexData.resize(static_cast<usize>(indexBytes));
    if(cursor > binary.size() || binary.size() - cursor < m_indexData.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed index payload"));
        return false;
    }
    NWB_MEMCPY(m_indexData.data(), m_indexData.size(), binary.data() + cursor, m_indexData.size());
    cursor += m_indexData.size();

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return true;
}


bool GeometryAssetCodec::deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const{
    auto asset = MakeUnique<Geometry>(virtualPath);
    if(!asset->loadBinary(binary))
        return false;

    outAsset = Move(asset);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("GeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'"),
            StringConvert(asset.assetType().c_str()),
            StringConvert(Geometry::s_AssetTypeText)
        );
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(asset);
    if(geometry.vertexStride() == 0 || geometry.vertexData().empty() || geometry.indexData().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: geometry payload is incomplete"));
        return false;
    }

    outBinary.clear();
    AppendPOD(outBinary, __hidden_assets::s_GeometryMagic);
    AppendPOD(outBinary, __hidden_assets::s_GeometryVersion);
    AppendPOD(outBinary, geometry.vertexStride());
    AppendPOD(outBinary, static_cast<u8>(geometry.use32BitIndices() ? 1u : 0u));
    AppendPOD(outBinary, static_cast<u64>(geometry.vertexData().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.indexData().size()));
    const usize vertexBegin = outBinary.size();
    outBinary.resize(vertexBegin + geometry.vertexData().size());
    NWB_MEMCPY(outBinary.data() + vertexBegin, geometry.vertexData().size(), geometry.vertexData().data(), geometry.vertexData().size());

    const usize indexBegin = outBinary.size();
    outBinary.resize(indexBegin + geometry.indexData().size());
    NWB_MEMCPY(outBinary.data() + indexBegin, geometry.indexData().size(), geometry.indexData().data(), geometry.indexData().size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

