// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "auto_registration.h"
#include "arena_names.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>

#include <global/auto_registration.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AssetCodecAutoRegistrar::initialize(){
    ::RegisterAutoFactory(QueryAssetAutoRegistrationQueue<NotNull<AssetCodecFactory>, AssetsArenaScope::s_AutoCodecFactoryQueueArena>(), m_factory);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RegisterAutoCollectedAssetCodecs(AssetRegistry& outRegistry){
    Alloc::ScratchArena scratchArena(AssetsArenaScope::s_RegisterCodecsScratch);
    Vector<NotNull<AssetCodecFactory>, Alloc::ScratchArena> codecFactories{scratchArena};
    QueryAssetAutoRegistrationQueue<NotNull<AssetCodecFactory>, AssetsArenaScope::s_AutoCodecFactoryQueueArena>().copyTo(codecFactories);

    ::RegisterAutoFactoryProducts(
        codecFactories, [](const NotNull<AssetCodecFactory> factory){ return (*factory)(); },
        [&](UniquePtr<IAssetCodec> codec){ return outRegistry.registerCodec(Move(codec)); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: codec factory returned null codec")); },
        [](){ NWB_LOGGER_ERROR(NWB_TEXT("RegisterAutoCollectedAssetCodecs: failed to register codec")); }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

