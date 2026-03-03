// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"


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


bool AssetCookerRegistry::cook(const AssetCookOptions& options, AString& outError)const{
    const AString requestedType = ::CanonicalizeText(options.assetType);
    if(requestedType.empty()){
        if(m_assetCookers.size() == 1)
            return m_assetCookers.begin().value()->cook(options, outError);

        outError = "Missing --asset-type";
        return false;
    }

    const auto found = m_assetCookers.find(requestedType);
    if(found == m_assetCookers.end()){
        outError = StringFormat("Unsupported --asset-type '{}'", options.assetType);
        return false;
    }

    return found.value()->cook(options, outError);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

