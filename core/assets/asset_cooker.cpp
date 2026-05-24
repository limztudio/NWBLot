// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "asset_cooker.h"

#include <core/alloc/scratch.h>

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_cooker{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CookerMap>
static AssetString DescribeAvailableCookers(AssetArena& arena, const CookerMap& cookers){
    if(cookers.empty())
        return AssetString("(none)", arena);

    Alloc::ScratchArena<> scratchArena;
    Vector<ACompactString, Alloc::ScratchArena<>> types{scratchArena};
    types.reserve(cookers.size());
    for(const auto& [_, cooker] : cookers)
        types.push_back(cooker->assetTypeText());

    Sort(types.begin(), types.end());

    usize outputSize = 0;
    for(const ACompactString& type : types){
        const usize typeSize = type.view().size();
        if(typeSize > Limit<usize>::s_Max - outputSize){
            outputSize = 0;
            break;
        }
        outputSize += typeSize;
        if(outputSize > Limit<usize>::s_Max - 2u){
            outputSize = 0;
            break;
        }
        outputSize += 2u;
    }
    if(outputSize >= 2u)
        outputSize -= 2u;

    AssetString output{arena};
    output.reserve(outputSize);
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


AssetCookerRegistry::AssetCookerRegistry(AssetArena& arena)
    : m_arena(arena)
    , m_assetCookers(0, Hasher<Name>(), EqualTo<Name>(), arena)
{}


bool AssetCookerRegistry::registerCooker(UniquePtr<IAssetCooker>&& cooker){
    if(!cooker){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCookerRegistry: rejected null cooker registration"));
        return false;
    }

    const Name typeName = cooker->assetType();
    if(!typeName){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCookerRegistry: rejected cooker registration with empty asset type"));
        return false;
    }

    auto registered = m_assetCookers.emplace(typeName, Move(cooker));
    if(!registered.second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCookerRegistry: cooker for type '{}' is already registered")
            , StringConvert(typeName.c_str())
        );
        return false;
    }

    return true;
}


bool AssetCookerRegistry::cook(const AssetCookOptions& options)const{
    if(m_assetCookers.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("No asset cookers are registered"));
        return false;
    }

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
            NWB_LOGGER_INFO(NWB_TEXT("AssetCookerRegistry: selected only registered asset cooker '{}'")
                , StringConvert(resolvedOptions.assetType.c_str())
            );
            return onlyCooker.cook(resolvedOptions);
        }

        NWB_LOGGER_ERROR(NWB_TEXT("Missing --asset-type. Available types: {}")
            , StringConvert(__hidden_asset_cooker::DescribeAvailableCookers(m_arena, m_assetCookers))
        );
        return false;
    }

    const auto found = m_assetCookers.find(requestedType);
    if(found == m_assetCookers.end()){
        NWB_LOGGER_ERROR(NWB_TEXT("Unsupported --asset-type '{}'. Available types: {}")
            , StringConvert(options.assetType.c_str())
            , StringConvert(__hidden_asset_cooker::DescribeAvailableCookers(m_arena, m_assetCookers))
        );
        return false;
    }

    return found.value()->cook(options);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

