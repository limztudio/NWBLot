// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"

#include <core/alloc/scratch.h>

#include <logger/client/logger.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_assets{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static AString DescribeAvailableCookers(const HashMap<Name, UniquePtr<IAssetCooker>, Hasher<Name>, EqualTo<Name>>& cookers){
    if(cookers.empty())
        return "(none)";

    Alloc::ScratchArena<> scratchArena;
    Vector<CompactString, Alloc::ScratchAllocator<CompactString>> types{Alloc::ScratchAllocator<CompactString>(scratchArena)};
    types.reserve(cookers.size());
    for(const auto& [_, cooker] : cookers)
        types.push_back(cooker->assetTypeText());

    Sort(types.begin(), types.end());

    AString output;
    for(usize i = 0; i < types.size(); ++i){
        if(i > 0)
            output += ", ";
        output += types[i].c_str();
    }

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool AssetCookerRegistry::registerCooker(UniquePtr<IAssetCooker>&& cooker){
    if(!cooker)
        return false;

    const Name typeName = cooker->assetType();
    if(!typeName)
        return false;

    if(m_assetCookers.find(typeName) != m_assetCookers.end())
        return false;

    m_assetCookers[typeName] = Move(cooker);
    return true;
}


bool AssetCookerRegistry::cook(const AssetCookOptions& options)const{
    if(m_assetCookers.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("No asset cookers are registered"));
        return false;
    }

    const AString availableCookers = __hidden_assets::DescribeAvailableCookers(m_assetCookers);
    const Name requestedType = options.assetType.empty()
        ? NAME_NONE
        : Name(options.assetType.view())
    ;
    if(!requestedType){
        if(m_assetCookers.size() == 1){
            const auto& onlyCookerEntry = *m_assetCookers.begin();
            IAssetCooker& onlyCooker = *onlyCookerEntry.second;
            AssetCookOptions resolvedOptions = options;
            resolvedOptions.assetType = onlyCooker.assetTypeText();
            return onlyCooker.cook(resolvedOptions);
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
            StringConvert(options.assetType.c_str()),
            StringConvert(availableCookers)
        );
        return false;
    }

    return found.value()->cook(options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
