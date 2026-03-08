// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AString DescribeAvailableCookers(const HashMap<AString, UniquePtr<IAssetCooker>>& cookers){
    if(cookers.empty())
        return "(none)";

    Vector<AString> types;
    types.reserve(cookers.size());
    for(const auto& [typeName, _] : cookers)
        types.push_back(typeName);

    Sort(types.begin(), types.end());

    AString output;
    for(usize i = 0; i < types.size(); ++i){
        if(i > 0)
            output += ", ";
        output += types[i];
    }

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    if(m_assetCookers.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("No asset cookers are registered"));
        return false;
    }

    const AString availableCookers = __hidden_assets::DescribeAvailableCookers(m_assetCookers);
    const AString requestedType = ::CanonicalizeText(options.assetType);
    if(requestedType.empty()){
        if(m_assetCookers.size() == 1){
            for(const auto& [typeName, cooker] : m_assetCookers){
                AssetCookOptions resolvedOptions = options;
                resolvedOptions.assetType = typeName;
                return cooker->cook(resolvedOptions);
            }
        }

        NWB_LOGGER_ERROR(
            NWB_TEXT("Missing --asset-type. Available types: {}"),
            StringConvert(availableCookers)
        );
        return false;
    }

    const auto found = m_assetCookers.find(requestedType);
    if(found == m_assetCookers.end()){
        NWB_LOGGER_ERROR(
            NWB_TEXT("Unsupported --asset-type '{}'. Available types: {}"),
            StringConvert(options.assetType),
            StringConvert(availableCookers)
        );
        return false;
    }

    return found.value()->cook(options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

