// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include <global/binary.h>
#include <logger/client/logger.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr u32 s_GeometryMagic = 0x47454F31u; // GEO1
static constexpr u32 s_GeometryVersion = 2u;
#if defined(NWB_COOK)
static constexpr usize s_GeometryHeaderBytes =
    sizeof(u32) + // magic
    sizeof(u32) + // version
    sizeof(u64) + // vertex count
    sizeof(u64)   // index count
;
#endif


UniquePtr<Core::Assets::IAssetCodec> CreateGeometryAssetCodec(){
    return MakeUnique<GeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_GeometryAssetCodecAutoRegistrar(&CreateGeometryAssetCodec);


template<typename ValueT>
bool ReadVectorPayload(
    const Core::Assets::AssetBytes& binary,
    usize& inOutCursor,
    const u64 count,
    Vector<ValueT>& outValues,
    const tchar* label
){
    outValues.clear();
    if(count > static_cast<u64>(Limit<usize>::s_Max / sizeof(ValueT))){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: '{}' payload byte size overflows"), label);
        return false;
    }

    const usize typedCount = static_cast<usize>(count);
    const usize byteCount = typedCount * sizeof(ValueT);
    if(inOutCursor > binary.size() || byteCount > binary.size() - inOutCursor){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed '{}' payload"), label);
        return false;
    }

    outValues.resize(typedCount);
    if(byteCount > 0u)
        NWB_MEMCPY(outValues.data(), byteCount, binary.data() + inOutCursor, byteCount);
    inOutCursor += byteCount;
    return true;
}

#if defined(NWB_COOK)
template<typename ValueT>
bool AppendVectorPayload(Core::Assets::AssetBytes& outBinary, const Vector<ValueT>& values, const tchar* label){
    if(values.size() > Limit<usize>::s_Max / sizeof(ValueT)){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: '{}' payload byte size overflows"), label);
        return false;
    }

    const usize byteCount = values.size() * sizeof(ValueT);
    if(byteCount > Limit<usize>::s_Max - outBinary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: '{}' payload overflows output binary"), label);
        return false;
    }

    ::BinaryDetail::AppendBytesUnchecked(outBinary, values.data(), byteCount);
    return true;
}

#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Geometry::validatePayload()const{
    const auto geometryPathText = [this]() -> TString{
        return
            virtualPath()
                ? StringConvert(virtualPath().c_str())
                : TString(NWB_TEXT("<unnamed>"))
        ;
    };

    if(m_vertices.empty() || m_indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' has incomplete payload")
            , geometryPathText()
        );
        return false;
    }

    if(m_vertices.size() > static_cast<usize>(Limit<u32>::s_Max) || m_indices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits")
            , geometryPathText()
        );
        return false;
    }
    if((m_indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3 for triangle-list rendering")
            , geometryPathText()
            , m_indices.size()
        );
        return false;
    }

    for(usize i = 0; i < m_vertices.size(); ++i){
        const GeometryVertex& vertex = m_vertices[i];
        const bool finite =
            IsFinite(vertex.position.x)
            && IsFinite(vertex.position.y)
            && IsFinite(vertex.position.z)
            && IsFinite(vertex.normal.x)
            && IsFinite(vertex.normal.y)
            && IsFinite(vertex.normal.z)
            && IsFinite(vertex.color0.x)
            && IsFinite(vertex.color0.y)
            && IsFinite(vertex.color0.z)
            && IsFinite(vertex.color0.w)
        ;
        if(!finite){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' vertex {} contains non-finite data")
                , geometryPathText()
                , i
            );
            return false;
        }

        const f32 normalLengthSquared =
            (vertex.normal.x * vertex.normal.x)
            + (vertex.normal.y * vertex.normal.y)
            + (vertex.normal.z * vertex.normal.z)
        ;
        if(!IsFinite(normalLengthSquared) || Abs(normalLengthSquared - 1.0f) > 0.001f){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' vertex {} has an invalid normal")
                , geometryPathText()
                , i
            );
            return false;
        }
    }

    for(const u32 indexValue : m_indices){
        if(indexValue >= m_vertices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' references vertex index {} but only has {} vertices")
                , geometryPathText()
                , indexValue
                , m_vertices.size()
            );
            return false;
        }
    }

    return true;
}


bool Geometry::loadBinary(const Core::Assets::AssetBytes& binary){
    if(!virtualPath()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: virtual path is empty"));
        return false;
    }

    m_vertices.clear();
    m_indices.clear();

    usize cursor = 0;
    u32 magic = 0;
    u32 version = 0;
    u64 vertexCount = 0;
    u64 indexCount = 0;
    if(
        !ReadPOD(binary, cursor, magic)
        || !ReadPOD(binary, cursor, version)
        || !ReadPOD(binary, cursor, vertexCount)
        || !ReadPOD(binary, cursor, indexCount)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: malformed header"));
        return false;
    }

    if(magic != __hidden_geometry_asset::s_GeometryMagic){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: invalid magic"));
        return false;
    }
    if(version != __hidden_geometry_asset::s_GeometryVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: unsupported version {}"), version);
        return false;
    }
    if(vertexCount == 0u || indexCount == 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: geometry payload is empty"));
        return false;
    }
    if(vertexCount > static_cast<u64>(Limit<u32>::s_Max) || indexCount > static_cast<u64>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: payload counts exceed u32 limits"));
        return false;
    }
    if((indexCount % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: index count is not a multiple of 3"));
        return false;
    }

    if(!__hidden_geometry_asset::ReadVectorPayload(binary, cursor, vertexCount, m_vertices, NWB_TEXT("vertices")))
        return false;
    if(!__hidden_geometry_asset::ReadVectorPayload(binary, cursor, indexCount, m_indices, NWB_TEXT("indices")))
        return false;

    if(cursor != binary.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::loadBinary failed: trailing bytes detected"));
        return false;
    }

    return validatePayload();
}


bool GeometryAssetCodec::deserialize(const Name& virtualPath, const Core::Assets::AssetBytes& binary, UniquePtr<Core::Assets::IAsset>& outAsset)const{
    return Core::Assets::DeserializeTypedAsset<Geometry>(virtualPath, binary, outAsset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GeometryAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("GeometryAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Geometry::s_AssetTypeText)
        );
        return false;
    }

    const Geometry& geometry = static_cast<const Geometry&>(asset);
    if(!geometry.validatePayload())
        return false;

    usize reserveBytes = __hidden_geometry_asset::s_GeometryHeaderBytes;
    const bool canReserve = AddBinaryVectorReserveBytes(reserveBytes, geometry.vertices())
        && AddBinaryVectorReserveBytes(reserveBytes, geometry.indices())
    ;

    outBinary.clear();
    if(canReserve)
        outBinary.reserve(reserveBytes);

    AppendPOD(outBinary, __hidden_geometry_asset::s_GeometryMagic);
    AppendPOD(outBinary, __hidden_geometry_asset::s_GeometryVersion);
    AppendPOD(outBinary, static_cast<u64>(geometry.vertices().size()));
    AppendPOD(outBinary, static_cast<u64>(geometry.indices().size()));
    if(!__hidden_geometry_asset::AppendVectorPayload(outBinary, geometry.vertices(), NWB_TEXT("vertices")))
        return false;

    return __hidden_geometry_asset::AppendVectorPayload(outBinary, geometry.indices(), NWB_TEXT("indices"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

