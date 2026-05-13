// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include "geometry_binary_payload.h"

#include <global/binary.h>
#include <core/common/log.h>
#include <core/assets/asset_auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_geometry_asset{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UniquePtr<Core::Assets::IAssetCodec> CreateGeometryAssetCodec(){
    return MakeUnique<GeometryAssetCodec>();
}
Core::Assets::AssetCodecAutoRegistrar s_GeometryAssetCodecAutoRegistrar(&CreateGeometryAssetCodec);


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Geometry::validatePayload()const{
    const TString geometryPathText = Core::Assets::AssetVirtualPathText(*this);

    if(m_vertices.empty() || m_indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' has incomplete payload")
            , geometryPathText
        );
        return false;
    }

    if(m_vertices.size() > static_cast<usize>(Limit<u32>::s_Max) || m_indices.size() > static_cast<usize>(Limit<u32>::s_Max)){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' exceeds u32 vertex/index count limits")
            , geometryPathText
        );
        return false;
    }
    if((m_indices.size() % 3u) != 0u){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' index count {} is not a multiple of 3 for triangle-list rendering")
            , geometryPathText
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
                , geometryPathText
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
                , geometryPathText
                , i
            );
            return false;
        }
    }

    for(const u32 indexValue : m_indices){
        if(indexValue >= m_vertices.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' references vertex index {} but only has {} vertices")
                , geometryPathText
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

    const tchar* const loadFailureContext = NWB_TEXT("Geometry::loadBinary");
    usize cursor = 0;
    GeometryBinaryPayload::GeometryHeaderBinary header;
    if(!GeometryBinaryPayload::ReadHeader(
        binary,
        cursor,
        header,
        GeometryBinaryPayload::s_GeometryMagic,
        GeometryBinaryPayload::s_GeometryVersion,
        loadFailureContext
    ))
        return false;

    const u64 vertexCount = header.vertexCount;
    const u64 indexCount = header.indexCount;
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

    auto readVector = [&](const u64 count, auto& outValues, const tchar* label){
        return GeometryBinaryPayload::ReadVector(binary, cursor, count, outValues, loadFailureContext, label);
    };
    if(!readVector(vertexCount, m_vertices, NWB_TEXT("vertices")))
        return false;
    if(!readVector(indexCount, m_indices, NWB_TEXT("indices")))
        return false;

    if(!GeometryBinaryPayload::ReadComplete(binary, cursor, loadFailureContext))
        return false;

    return validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

