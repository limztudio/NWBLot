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
#if defined(NWB_COOK)
static constexpr usize s_GeometryHeaderBytes =
    sizeof(u32) + // magic
    sizeof(u32) + // version
    sizeof(u32) + // vertex stride
    sizeof(u8) +  // index-format flag
    sizeof(u64) + // vertex byte count
    sizeof(u64)   // index byte count
;
#endif


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

bool Geometry::validatePayload()const{
    const TString geometryPathText = virtualPath()
        ? StringConvert(virtualPath().c_str())
        : TString(NWB_TEXT("<unnamed>"))
    ;

    if(m_vertexStride == 0 || m_vertexData.empty() || m_indexData.empty()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' has incomplete payload"),
            geometryPathText
        );
        return false;
    }

    if((m_vertexData.size() % m_vertexStride) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' vertex payload size {} is not aligned to stride {}"),
            geometryPathText,
            m_vertexData.size(),
            m_vertexStride
        );
        return false;
    }

    const usize indexStride = m_use32BitIndices ? sizeof(u32) : sizeof(u16);
    if((m_indexData.size() % indexStride) != 0){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' index payload size {} is not aligned to {}-byte indices"),
            geometryPathText,
            m_indexData.size(),
            indexStride
        );
        return false;
    }

    const usize vertexCount = m_vertexData.size() / m_vertexStride;
    const usize indexCount = m_indexData.size() / indexStride;
    if(vertexCount > static_cast<usize>(Limit<u32>::s_Max) || indexCount > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits"),
            geometryPathText
        );
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3 for triangle-list rendering"),
            geometryPathText,
            indexCount
        );
        return false;
    }

    u64 maxIndexValue = 0;
    const u8* indexBytes = m_indexData.data();
    if(m_use32BitIndices){
        for(usize i = 0; i < indexCount; ++i){
            u32 indexValue = 0;
            NWB_MEMCPY(&indexValue, sizeof(indexValue), indexBytes + i * sizeof(indexValue), sizeof(indexValue));
            maxIndexValue = Max(maxIndexValue, static_cast<u64>(indexValue));
        }
    }
    else{
        for(usize i = 0; i < indexCount; ++i){
            u16 indexValue = 0;
            NWB_MEMCPY(&indexValue, sizeof(indexValue), indexBytes + i * sizeof(indexValue), sizeof(indexValue));
            maxIndexValue = Max(maxIndexValue, static_cast<u64>(indexValue));
        }
    }

    if(maxIndexValue >= static_cast<u64>(vertexCount)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Geometry::validatePayload failed: geometry '{}' references vertex index {} but only has {} vertices"),
            geometryPathText,
            maxIndexValue,
            vertexCount
        );
        return false;
    }

    return true;
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
        || !ReadPOD(binary, cursor, indexBytes)
    ){
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
    if(use32BitIndices > 1){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: invalid index-format flag"));
        return false;
    }
    if(vertexBytes > static_cast<u64>(Limit<usize>::s_Max) || indexBytes > static_cast<u64>(Limit<usize>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: geometry payload exceeds addressable size"));
        return false;
    }
    const usize vertexByteCount = static_cast<usize>(vertexBytes);
    const usize indexByteCount = static_cast<usize>(indexBytes);
    if(cursor > binary.size() || vertexByteCount > binary.size() - cursor){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed vertex payload"));
        return false;
    }
    const usize indexBegin = cursor + vertexByteCount;
    if(indexByteCount > binary.size() - indexBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed index payload"));
        return false;
    }

    m_vertexStride = vertexStride;
    m_use32BitIndices = (use32BitIndices != 0);

    m_vertexData.resize(vertexByteCount);
    NWB_MEMCPY(m_vertexData.data(), m_vertexData.size(), binary.data() + cursor, m_vertexData.size());
    cursor += m_vertexData.size();

    m_indexData.resize(indexByteCount);
    NWB_MEMCPY(m_indexData.data(), m_indexData.size(), binary.data() + cursor, m_indexData.size());
    cursor += m_indexData.size();

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return validatePayload();
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
    if(!geometry.validatePayload())
        return false;

    const usize vertexBytes = geometry.vertexData().size();
    const usize indexBytes = geometry.indexData().size();

    outBinary.clear();
    if(vertexBytes <= Limit<usize>::s_Max - __hidden_assets::s_GeometryHeaderBytes){
        const usize headerAndVertexBytes = __hidden_assets::s_GeometryHeaderBytes + vertexBytes;
        if(indexBytes <= Limit<usize>::s_Max - headerAndVertexBytes)
            outBinary.reserve(headerAndVertexBytes + indexBytes);
    }

    AppendPOD(outBinary, __hidden_assets::s_GeometryMagic);
    AppendPOD(outBinary, __hidden_assets::s_GeometryVersion);
    AppendPOD(outBinary, geometry.vertexStride());
    AppendPOD(outBinary, static_cast<u8>(geometry.use32BitIndices() ? 1u : 0u));
    AppendPOD(outBinary, static_cast<u64>(vertexBytes));
    AppendPOD(outBinary, static_cast<u64>(indexBytes));
    const usize vertexBegin = outBinary.size();
    if(vertexBytes > Limit<usize>::s_Max - vertexBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: vertex payload size overflows output binary"));
        return false;
    }
    outBinary.resize(vertexBegin + vertexBytes);
    NWB_MEMCPY(outBinary.data() + vertexBegin, vertexBytes, geometry.vertexData().data(), vertexBytes);

    const usize indexBegin = outBinary.size();
    if(indexBytes > Limit<usize>::s_Max - indexBegin){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: index payload size overflows output binary"));
        return false;
    }
    outBinary.resize(indexBegin + indexBytes);
    NWB_MEMCPY(outBinary.data() + indexBegin, indexBytes, geometry.indexData().data(), indexBytes);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

