// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_entry_registry.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_cook_entry_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoRegistrationQueue{
    CookArena arena;
    Futex mutex;
    CookVector<CookEntryRegistrationFunction> functions;

    AutoRegistrationQueue()
        : arena("NWB::Core::Assets::CookEntryAutoRegistrationQueue")
        , functions(arena)
    {}
};

AutoRegistrationQueue& QueryAutoRegistrationQueue(){
    static AutoRegistrationQueue queue;
    return queue;
}

static bool ContainsRegistrationFunction(
    const CookVector<CookEntryRegistrationFunction>& functions,
    const CookEntryRegistrationFunction function
){
    for(const CookEntryRegistrationFunction current : functions){
        if(current == function)
            return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CookEntryAutoRegistrar::CookEntryAutoRegistrar(const CookEntryRegistrationFunction function){
    if(function == nullptr)
        return;

    auto& queue = __hidden_cook_entry_registry::QueryAutoRegistrationQueue();
    ScopedLock lock(queue.mutex);
    if(!__hidden_cook_entry_registry::ContainsRegistrationFunction(queue.functions, function))
        queue.functions.push_back(function);
}

bool RegisterAutoCollectedCookEntryTypes(CookEntryRegistry& registry){
    Core::Alloc::ScratchArena scratchArena;
    Vector<CookEntryRegistrationFunction, Core::Alloc::ScratchArena> functions{scratchArena};
    {
        auto& queue = __hidden_cook_entry_registry::QueryAutoRegistrationQueue();
        ScopedLock lock(queue.mutex);
        AssignTriviallyCopyableVector(functions, queue.functions);
    }

    for(const CookEntryRegistrationFunction function : functions){
        if(function == nullptr)
            continue;
        if(function(registry))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetCook: failed to register auto-collected cook entry type"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

