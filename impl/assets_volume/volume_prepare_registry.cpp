// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "volume_prepare_registry.h"

#include "arena_names.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_volume_prepare_registry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct AutoPrepareQueue{
    AssetsVolumeCookDetail::CookArena arena;
    Futex mutex;
    AssetsVolumeCookDetail::CookVector<AssetsVolumeCookDetail::AssetVolumePrepareFunction> functions;

    AutoPrepareQueue()
        : arena(AssetsVolumeArenaScope::s_PrepareQueueArena)
        , functions(arena)
    {}
};

AutoPrepareQueue& QueryAutoPrepareQueue(){
    static AutoPrepareQueue queue;
    return queue;
}

static bool ContainsPrepareFunction(
    const AssetsVolumeCookDetail::CookVector<AssetsVolumeCookDetail::AssetVolumePrepareFunction>& functions,
    const AssetsVolumeCookDetail::AssetVolumePrepareFunction function
){
    for(const AssetsVolumeCookDetail::AssetVolumePrepareFunction current : functions){
        if(current == function)
            return true;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace AssetsVolumeCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


AssetVolumePrepareAutoRegistrar::AssetVolumePrepareAutoRegistrar(const AssetVolumePrepareFunction function){
    if(function == nullptr)
        return;

    auto& queue = __hidden_asset_volume_prepare_registry::QueryAutoPrepareQueue();
    ScopedLock lock(queue.mutex);
    if(!__hidden_asset_volume_prepare_registry::ContainsPrepareFunction(queue.functions, function))
        queue.functions.push_back(function);
}

bool RegisterAutoCollectedAssetVolumePreparers(AssetVolumePrepareContext& context){
    Core::Alloc::ScratchArena scratchArena(AssetsVolumeArenaScope::s_RegisterPreparersArena);
    Vector<AssetVolumePrepareFunction, Core::Alloc::ScratchArena> functions{scratchArena};
    {
        auto& queue = __hidden_asset_volume_prepare_registry::QueryAutoPrepareQueue();
        ScopedLock lock(queue.mutex);
        AssignTriviallyCopyableVector(functions, queue.functions);
    }

    for(const AssetVolumePrepareFunction function : functions){
        if(function == nullptr)
            continue;
        if(function(context))
            continue;

        NWB_LOGGER_ERROR(NWB_TEXT("AssetVolumeCooker: failed to run auto-collected volume prepare step"));
        return false;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

