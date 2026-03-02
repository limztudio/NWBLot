// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCookerRegistry::registerCooker(IAssetCooker& cooker){
    const AString incomingType = ::CanonicalizeText(cooker.assetType());
    if(incomingType.empty())
        return false;

    for(IAssetCooker* registeredCooker : m_assetCookers){
        if(registeredCooker == &cooker)
            return true;

        const AString registeredType = ::CanonicalizeText(registeredCooker->assetType());
        if(registeredType == incomingType)
            return false;
    }

    m_assetCookers.push_back(&cooker);
    return true;
}


bool AssetCookerRegistry::cook(const AssetCookOptions& options, AString& outError)const{
    const AString requestedType = ::CanonicalizeText(options.assetType);
    if(requestedType.empty()){
        if(m_assetCookers.size() == 1)
            return m_assetCookers[0]->cook(options, outError);

        outError = "Missing --asset-type";
        return false;
    }

    for(IAssetCooker* cooker : m_assetCookers){
        if(cooker == nullptr)
            continue;

        if(::CanonicalizeText(cooker->assetType()) == requestedType)
            return cooker->cook(options, outError);
    }

    outError = StringFormat("Unsupported --asset-type '{}'", options.assetType);
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

