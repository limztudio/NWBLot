// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_loader.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<SharedImageSource> LoadImageSource(Core::Alloc::GlobalArena& arena, const Core::Assets::AssetManager& assets, const Core::Assets::AssetRef<Texture>& identity){
    if(!identity.valid()){
        NWB_LOGGER_ERROR(NWB_TEXT("LoadImageSource: texture asset reference is empty"));
        return MakeUnexpected(Failure{});
    }
    auto loadedAsset = assets.loadTypedSync<Texture>(identity.name(), NWB_TEXT("LoadImageSource"), Texture::s_AssetTypeText);
    if(!loadedAsset)
        return MakeUnexpected(loadedAsset.error());
    const Texture* texture = loadedAsset->get();
    if(!texture)
        return MakeUnexpected(Failure{});
    if(texture->virtualPath() != identity.name()){
        NWB_LOGGER_ERROR(NWB_TEXT("LoadImageSource: loaded texture identity does not match the requested asset"));
        return MakeUnexpected(Failure{});
    }
    return MakeImageSource(arena, *texture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

