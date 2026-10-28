// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_dependency_registry.h"
#include "arena_names.h"

#include <core/common/log.h>

#include <global/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_dependency_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


::AutoRegistrationQueue<NotNull<CookDependencyRegistrationFunction>, AssetArena>& QueryAutoRegistrationQueue(){
    static ::AutoRegistrationQueue<NotNull<CookDependencyRegistrationFunction>, AssetArena> s_Queue(AssetsArenaScope::s_CookDependencyQueueArena);
    return s_Queue;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool CookDependencyRegistry::registerType(const Name& assetType, const CookDependencyExtractFunction extractDependencies, const Name& categoryAssetType){
    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: tried to register an unnamed cook dependency type"));
        return false;
    }
    if(!extractDependencies){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: cook dependency type '{}' has no extract function"), ConvertString(assetType.resolvedText()));
        return false;
    }

    if(!m_lookup.emplace(assetType, MakeNotNull(extractDependencies)).second){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: duplicate cook dependency type registration '{}'"), ConvertString(assetType.resolvedText()));
        return false;
    }

    if(categoryAssetType)
        m_categories.emplace(assetType, categoryAssetType);
    return true;
}

bool CookDependencyRegistry::registerDefaultEntryAsset(const Name& virtualPath, const Name& assetType, const AStringView virtualPathText){
    if(!virtualPath || !assetType || virtualPathText.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: invalid default cook entry asset registration"));
        return false;
    }
    if(m_defaultEntryAsset.virtualPath && (m_defaultEntryAsset.virtualPath != virtualPath || m_defaultEntryAsset.assetType != assetType)){
        NWB_LOGGER_ERROR(
            NWB_TEXT("AssetCook: duplicate default cook entry asset registration '{}' (already registered '{}')"),
            ConvertString(virtualPathText),
            ConvertString(m_defaultEntryAsset.virtualPathText)
        );
        return false;
    }

    m_defaultEntryAsset.virtualPath = virtualPath;
    m_defaultEntryAsset.assetType = assetType;
    m_defaultEntryAsset.virtualPathText = virtualPathText;
    return true;
}

CookDependencyExtractFunction CookDependencyRegistry::find(const Name& assetType)const noexcept{
    const auto found = m_lookup.find(assetType);
    return found == m_lookup.end() ? nullptr : found.value().get();
}

bool CookDependencyRegistry::matchesAssetType(const Name& actualType, const Name& expectedType)const noexcept{
    if(!expectedType || actualType == expectedType)
        return true;
    if(!actualType)
        return false;

    const auto found = m_categories.find(actualType);
    return found != m_categories.end() && found.value() == expectedType;
}

bool CookDependencyRegistry::extractDependencies(const CookDependencyItemContext& context, ICookDependencyCollector& collector)const{
    const CookDependencyExtractFunction extractor = find(context.assetType);
    if(!extractor)
        return true;
    return extractor(context, collector);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CookDependencyAutoRegistrar::CookDependencyAutoRegistrar(const CookDependencyRegistrationFunction function){
    ::RegisterAutoFactory(__hidden_cook_dependency_registry::QueryAutoRegistrationQueue(), function);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool RegisterAutoCollectedCookDependencyTypes(CookDependencyRegistry& registry, ScratchArena& scratchArena){
    Vector<NotNull<CookDependencyRegistrationFunction>, Core::Alloc::ScratchArena> functions{ scratchArena };
    return RunAutoCollectedFunctions(
        __hidden_cook_dependency_registry::QueryAutoRegistrationQueue(),
        registry,
        functions,
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: collected null cook dependency registration function")); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to register auto-collected cook dependency type")); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

