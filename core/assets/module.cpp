// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "module.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_COOK)
template<typename ContextT>
[[nodiscard]] b8 CheckVirtualPathImpl(const Name virtualPath, ContextT&& failureContext){
    if(virtualPath)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} failed: virtual path is empty"), Forward<ContextT>(failureContext));
    return false;
}


#if defined(FOUNDATION_UNICODE)
b8 IAsset::checkVirtualPath(const AStringView failureContext)const{
    if(virtualPath())
        return true;

    constexpr Name s_DiagnosticScratch("core/assets/virtual_path_diagnostic");
    Alloc::ScratchArena scratchArena(s_DiagnosticScratch);
    const auto converted = ConvertString(scratchArena, failureContext);
    return checkVirtualPath(TStringView(converted));
}
#endif
#else
template<typename ContextT>
[[nodiscard]] b8 CheckSerializeAssetTypeImpl(const IAsset& asset, const Name expectedAssetType, ContextT&& failureContext){
    if(asset.assetType() == expectedAssetType)
        return true;

    NWB_LOGGER_ERROR(
        FOUNDATION_TEXT("{} failed: invalid asset type '{}', expected '{}'"),
        Forward<ContextT>(failureContext),
        ConvertString(asset.assetType().resolvedText()),
        ConvertString(expectedAssetType.resolvedText())
    );
    return false;
}
#if defined(FOUNDATION_UNICODE)
b8 IAssetCodec::checkSerializeAssetType(const IAsset& asset, const AStringView failureContext)const{
    if(asset.assetType() == assetType())
        return true;

    constexpr Name s_DiagnosticScratch("core/assets/serialize_type_diagnostic");
    Alloc::ScratchArena scratchArena(s_DiagnosticScratch);
    const auto converted = ConvertString(scratchArena, failureContext);
    return checkSerializeAssetType(asset, TStringView(converted));
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if !defined(NWB_COOK)
b8 IAsset::checkVirtualPath(const TStringView failureContext)const{
    return CheckVirtualPathImpl(virtualPath(), failureContext);
}
#if defined(FOUNDATION_UNICODE)
b8 IAsset::checkVirtualPath(const AStringView failureContext)const{
    return CheckVirtualPathImpl(virtualPath(), ConvertString(failureContext));
}
#endif
#else
b8 IAssetCodec::checkSerializeAssetType(const IAsset& asset, const TStringView failureContext)const{
    return CheckSerializeAssetTypeImpl(asset, assetType(), failureContext);
}
#if defined(FOUNDATION_UNICODE)
b8 IAssetCodec::checkSerializeAssetType(const IAsset& asset, const AStringView failureContext)const{
    return CheckSerializeAssetTypeImpl(asset, assetType(), ConvertString(failureContext));
}
#endif
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

