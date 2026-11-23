// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_module{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_COOK)
template<typename ContextT>
[[nodiscard]] static b8 CheckVirtualPathImpl(const Name virtualPath, ContextT&& failureContext){
    if(virtualPath)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: virtual path is empty"), Forward<ContextT>(failureContext));
    return false;
}

#else
template<typename ContextT>
[[nodiscard]] static b8 CheckSerializeAssetTypeImpl(const IAsset& asset, const Name expectedAssetType, ContextT&& failureContext){
    if(asset.assetType() == expectedAssetType)
        return true;

    NWB_LOGGER_ERROR(
        FOUNDATION_TEXT("{} failed: invalid asset type '{}', expected '{}'")
        , Forward<ContextT>(failureContext)
        , ConvertString(asset.assetType().resolvedText())
        , ConvertString(expectedAssetType.resolvedText())
    );
    return false;
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_COOK)
b8 IAsset::checkVirtualPath(const TStringView failureContext)const{
    return __hidden_asset_module::CheckVirtualPathImpl(virtualPath(), failureContext);
}
#if defined(FOUNDATION_UNICODE)
b8 IAsset::checkVirtualPath(const AStringView failureContext)const{
    return __hidden_asset_module::CheckVirtualPathImpl(virtualPath(), ConvertString(failureContext));
}
#endif
#else
b8 IAssetCodec::checkSerializeAssetType(const IAsset& asset, const TStringView failureContext)const{
    return __hidden_asset_module::CheckSerializeAssetTypeImpl(asset, assetType(), failureContext);
}
#if defined(FOUNDATION_UNICODE)
b8 IAssetCodec::checkSerializeAssetType(const IAsset& asset, const AStringView failureContext)const{
    return __hidden_asset_module::CheckSerializeAssetTypeImpl(asset, assetType(), ConvertString(failureContext));
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

