// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCookerRegistry::registerCooker(UniquePtr<IAssetCooker>&& cooker){
    if(!cooker)
        return false;

    const AString canonicalType = ::CanonicalizeText(cooker->assetType());
    if(canonicalType.empty())
        return false;

    if(m_assetCookers.find(canonicalType) != m_assetCookers.end())
        return false;

    m_assetCookers[canonicalType] = Move(cooker);
    return true;
}


bool AssetCookerRegistry::cook(const AssetCookOptions& options)const{
    const AString requestedType = ::CanonicalizeText(options.assetType);
    if(requestedType.empty()){
        if(m_assetCookers.size() == 1)
            return m_assetCookers.begin().value()->cook(options);

        NWB_LOGGER_ERROR(NWB_TEXT("Missing --asset-type"));
        return false;
    }

    const auto found = m_assetCookers.find(requestedType);
    if(found == m_assetCookers.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Unsupported --asset-type '{}'"), StringConvert(options.assetType));
        return false;
    }

    return found.value()->cook(options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

