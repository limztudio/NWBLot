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


bool CookDependencyRegistry::registerType(const Name& assetType, const CookDependencyExtractFunction extractDependencies){
    if(!assetType){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: tried to register an unnamed cook dependency type"));
        return false;
    }
    if(!extractDependencies){
        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: cook dependency type '{}' has no extract function"), ConvertString(assetType.resolvedText()));
        return false;
    }

    if(m_lookup.emplace(assetType, MakeNotNull(extractDependencies)).second)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: duplicate cook dependency type registration '{}'"), ConvertString(assetType.resolvedText()));
    return false;
}

CookDependencyExtractFunction CookDependencyRegistry::find(const Name& assetType)const noexcept{
    const auto found = m_lookup.find(assetType);
    return found == m_lookup.end() ? nullptr : found.value().get();
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

