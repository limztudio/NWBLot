// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "geometry_asset.h"

#include "geometry_binary_payload.h"

#include <global/binary.h>
#include <core/alloc/scratch.h>
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

[[nodiscard]] static bool FiniteVector(const SIMDVector value, const u32 activeMask){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & activeMask) == 0u;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool Geometry::validatePayload()const{
    Core::Alloc::ScratchArena<> scratchArena;
    const TString<Core::Alloc::ScratchArena<>> geometryPathText = Core::Assets::AssetVirtualPathText(scratchArena, *this);

    if(m_positions.empty() || m_normals.empty() || m_colors.empty() || m_indices.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' has incomplete payload")
            , geometryPathText
        );
        return false;
    }

    if(m_positions.size() != m_normals.size() || m_positions.size() != m_colors.size()){
        NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' stream counts do not match")
            , geometryPathText
        );
        return false;
    }

    if(m_positions.size() > static_cast<usize>(Limit<u32>::s_Max) || m_indices.size() > static_cast<usize>(Limit<u32>::s_Max)){
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

    for(usize i = 0; i < m_positions.size(); ++i){
        const SIMDVector position = VectorSetW(LoadFloat(m_positions[i]), 0.0f);
        const SIMDVector normal = VectorSetW(LoadFloat(LoadHalf4U(m_normals[i])), 0.0f);
        const SIMDVector color0 = LoadFloat(LoadHalf4U(m_colors[i]));
        const bool finite =
            __hidden_geometry_asset::FiniteVector(position, 0x7u)
            && __hidden_geometry_asset::FiniteVector(normal, 0x7u)
            && __hidden_geometry_asset::FiniteVector(color0, 0xFu)
        ;
        if(!finite){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' vertex {} contains non-finite data")
                , geometryPathText
                , i
            );
            return false;
        }

        const f32 normalLengthSquared = VectorGetX(Vector3LengthSq(normal));
        if(!IsFinite(normalLengthSquared) || Abs(normalLengthSquared - 1.0f) > 0.001f){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' vertex {} has an invalid normal")
                , geometryPathText
                , i
            );
            return false;
        }
    }

    for(const u32 indexValue : m_indices){
        if(indexValue >= m_positions.size()){
            NWB_LOGGER_ERROR(NWB_TEXT("Geometry::validatePayload failed: geometry '{}' references vertex index {} but only has {} vertices")
                , geometryPathText
                , indexValue
                , m_positions.size()
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

    m_positions.clear();
    m_normals.clear();
    m_colors.clear();
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
    if(!readVector(vertexCount, m_positions, NWB_TEXT("positions")))
        return false;
    if(!readVector(vertexCount, m_normals, NWB_TEXT("normals")))
        return false;
    if(!readVector(vertexCount, m_colors, NWB_TEXT("colors")))
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

