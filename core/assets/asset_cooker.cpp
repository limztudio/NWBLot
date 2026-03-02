// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCookerRegistry::registerCooker(UniquePtr<IAssetCooker>&& cooker){
    if(!cooker)
        return false;

    const AString incomingType = ::CanonicalizeText(cooker->assetType());
    if(incomingType.empty())
        return false;

    for(const UniquePtr<IAssetCooker>& registeredCooker : m_assetCookers){
        if(!registeredCooker)
            continue;

        const AString registeredType = ::CanonicalizeText(registeredCooker->assetType());
        if(registeredType == incomingType)
            return false;
    }

    m_assetCookers.push_back(Move(cooker));
    return true;
}


bool AssetCookerRegistry::cook(const AssetCookOptions& options, AString& outError)const{
    const AString requestedType = ::CanonicalizeText(options.assetType);
    if(requestedType.empty()){
        u32 validCookerCount = 0;
        IAssetCooker* selectedCooker = nullptr;
        for(const UniquePtr<IAssetCooker>& cooker : m_assetCookers){
            if(!cooker)
                continue;

            ++validCookerCount;
            selectedCooker = cooker.get();
        }

        if(validCookerCount == 1 && selectedCooker != nullptr)
            return selectedCooker->cook(options, outError);

        outError = "Missing --asset-type";
        return false;
    }

    for(const UniquePtr<IAssetCooker>& cooker : m_assetCookers){
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

