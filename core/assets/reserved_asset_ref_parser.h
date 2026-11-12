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
    template<typename AssetT>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseText(
        const Path& nwbFilePath,
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

    template<typename AssetT, typename MetadataValue>
    [[nodiscard]] static Expected<AssetRef<AssetT>> ParseValue(
        const Path& nwbFilePath,
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

    template<typename AssetT, typename MetadataValue>
    [[nodiscard]] static Expected<AssetVector<AssetRef<AssetT>>> ParseList(
        const Path& nwbFilePath,
        const AStringView diagnosticPrefix,
        const MetadataValue& listValue,
        const AStringView fieldLabel,
        AssetArena& arena,
        Alloc::ScratchArena& scratchArena
    ){
        if(!listValue.isList()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be a list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldLabel));
            return MakeUnexpected(Failure{});
        }
        const auto& items = listValue.asList();
        AssetVector<AssetRef<AssetT>> refs(arena);
        refs.reserve(items.size());
        for(const MetadataValue& item : items){
            const auto parsedRef = ParseValue<AssetT>(nwbFilePath, diagnosticPrefix, fieldLabel, item, "entries must be strings", scratchArena);
            if(!parsedRef)
                return MakeUnexpected(Failure{});
            refs.push_back(*parsedRef);
        }
        return refs;
    }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

