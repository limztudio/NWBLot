// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "paths.h"

#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ReservedAssetRefParser{
public:
    template<typename AssetT, typename PathArenaT>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseText(
        const ::Path<PathArenaT>& nwbFilePath,
        const AStringView diagnosticPrefix,
        const AStringView fieldLabel,
        const AStringView text,
        Alloc::ScratchArena& scratchArena
    ){
        if(TrimView(text).empty() || text != TrimView(text)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': '{}' entry '{}' must be a non-empty trimmed virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
            return MakeUnexpected(Failure{});
        }
        if(text.find('\\') != AStringView::npos || !HasReservedAssetVirtualRoot(text, scratchArena)){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': '{}' entry '{}' must be an engine/- or project/-rooted virtual path"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
            return MakeUnexpected(Failure{});
        }
        const auto ref = AssetRef<AssetT>::FromText(text);
        if(!ref || !ref->valid()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': '{}' entry '{}' is invalid"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(text));
            return MakeUnexpected(Failure{});
        }
        return *ref;
    }

    template<typename AssetT, typename PathArenaT>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseText(const ::Path<PathArenaT>& file, const AStringView prefix, const AStringView field, const AStringView text){
        Alloc::ScratchArena scratch;
        return ParseText<AssetT>(file, prefix, field, text, scratch);
    }

    template<typename AssetT, typename MetadataValue, typename PathArenaT>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseValue(
        const ::Path<PathArenaT>& nwbFilePath,
        const AStringView diagnosticPrefix,
        const AStringView fieldLabel,
        const MetadataValue& value,
        const AStringView nonStringErrorText,
        Alloc::ScratchArena& scratchArena
    ){
        if(!value.isString()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': '{}' {}"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel), ConvertString(nonStringErrorText));
            return MakeUnexpected(Failure{});
        }
        const AStringView text(value.asString().data(), value.asString().size());
        return ParseText<AssetT>(nwbFilePath, diagnosticPrefix, fieldLabel, text, scratchArena);
    }

    template<typename AssetT, typename MetadataValue, typename PathArenaT>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseValue(
        const ::Path<PathArenaT>& nwbFilePath, const AStringView diagnosticPrefix,
        const AStringView fieldLabel, const MetadataValue& value, const AStringView nonStringErrorText
    ){
        Alloc::ScratchArena scratchArena;
        return ParseValue<AssetT>(nwbFilePath, diagnosticPrefix, fieldLabel, value, nonStringErrorText, scratchArena);
    }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

