// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "paths.h"

#include <global/numeric_text.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PathArenaT, typename MetadataValue, typename IsAllowedField>
requires requires(IsAllowedField& fn, const AStringView field){ fn(field); }
[[nodiscard]] inline b8 ValidateMetadataAssetFields(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix, IsAllowedField&& isAllowedField){
    for(const auto& field : asset.asMap()){
        const auto& fieldName = field.first;
        const AStringView fieldNameText(fieldName.data(), fieldName.size());
        if(isAllowedField(fieldNameText))
            continue;

        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': unsupported asset field '{}'"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldNameText));
        return false;
    }
    return true;
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 ValidateMetadataAssetFields(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix, const Span<const AStringView> allowedFields = {}){
    return ValidateMetadataAssetFields(nwbFilePath, asset, diagnosticPrefix, [allowedFields](const AStringView fieldName)noexcept{
        for(const AStringView allowedField : allowedFields){
            if(fieldName == allowedField)
                return true;
        }
        return false;
    });
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 ValidateMetadataAssetFields(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix, const InitializerList<AStringView> allowedFields){
    return ValidateMetadataAssetFields(nwbFilePath, asset, diagnosticPrefix, Span<const AStringView>(allowedFields.begin(), allowedFields.size()));
}

struct MetadataStringField{
    AStringView text;
    b8 present = false;
};


template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<MetadataStringField> ReadMetadataStringField(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const b8 required){
    MetadataStringField result;

    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return result;

        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' is required"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    result.present = true;
    if(!fieldValue->isString()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be a string"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    const auto text = fieldValue->asString();
    result.text = AStringView(text.data(), text.size());
    if(required && result.text.empty()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must not be empty"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    return result;
}

template<typename MetadataValue>
[[nodiscard]] inline Expected<f32> TryDecodeMetadataFiniteF32(const MetadataValue& value)noexcept(noexcept(!value.isNumeric() ? true : false) && noexcept(DeclVal<void(*)(f64)noexcept>()(value.toDouble()))){
    if(!value.isNumeric())
        return MakeUnexpected(Failure{});

    return TryFiniteF32FromF64(value.toDouble());
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Value(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& value, const AStringView diagnosticPrefix, const AStringView fieldName){
    const auto decoded = TryDecodeMetadataFiniteF32(value);
    if(decoded)
        return decoded;

    if(!value.isNumeric()){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be numeric"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' is outside the supported float range"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
    return MakeUnexpected(Failure{});
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<f32> ReadMetadataFiniteF32Field(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const b8 required){
    const auto* fieldValue = object.findField(fieldName);
    if(!fieldValue){
        if(!required)
            return 0.0f;

        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' is required"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    return ReadMetadataFiniteF32Value(nwbFilePath, *fieldValue, diagnosticPrefix, fieldName);
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<Name> ReadMetadataNameField(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const b8 required){
    const auto text = ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!text)
        return MakeUnexpected(text.error());
    // An absent optional field must remain s_NameNone. Name("") is a valid, non-null hash.
    if(!text->present)
        return s_NameNone;

    const auto name = Name::FromText(text->text);
    if(!name){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' contains an embedded null"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
        return MakeUnexpected(Failure{});
    }
    return *name;
}

template<typename AssetT, typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<AssetRef<AssetT>> ReadMetadataAssetRefField(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName, const b8 required){
    const auto assetName = ReadMetadataNameField(nwbFilePath, object, diagnosticPrefix, fieldName, required);
    if(!assetName)
        return MakeUnexpected(assetName.error());

    AssetRef<AssetT> ref;
    ref.virtualPath = *assetName;
    if(required && !ref.valid())
        return MakeUnexpected(Failure{});
    return ref;
}

template<typename SourceStringT, typename MetadataDocument, typename ParseDocument, typename PathArenaT>
[[nodiscard]] inline b8 ParseMetadataDocumentText(const ::Path<PathArenaT>& filePath, const AStringView diagnosticPrefix, ParseDocument&& parseDoc, SourceStringT& ioText, MetadataDocument& outDoc){
    if(!ReadTextFile(filePath, ioText)){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': failed to read source text"), ConvertString(diagnosticPrefix), PathToString<tchar>(filePath));
        return false;
    }
    StripUtf8Bom(ioText);

    if(!parseDoc(AStringView(ioText))){
        for(const auto& err : outDoc.errors()){
            NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}' parse error at {}:{}: {}"), ConvertString(diagnosticPrefix), PathToString<tchar>(filePath), err.line, err.column, ConvertString(err.message));
        }
        return false;
    }
    return true;
}

namespace Detail{

[[nodiscard]] b8 CheckPairedSourceExtensionNative(TStringView nwbFilePathText, AStringView sourcePath, AStringView expectedExtension, AStringView diagnosticPrefix, Alloc::ScratchArena& scratchArena);

}

template<typename PathArenaT>
[[nodiscard]] inline b8 CheckPairedSourceExtension(const ::Path<PathArenaT>& nwbFilePath, const AStringView sourcePath, const AStringView expectedExtension, const AStringView diagnosticPrefix, Alloc::ScratchArena& scratchArena){
    return Detail::CheckPairedSourceExtensionNative(nwbFilePath.native(), sourcePath, expectedExtension, diagnosticPrefix, scratchArena);
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataListField(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName){
    const auto* fieldValue = object.findField(fieldName);
    if(fieldValue && fieldValue->isList())
        return MakeNotNull(fieldValue);

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' must be a list"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName));
    return MakeUnexpected(Failure{});
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<Span<const MetadataValue>> ReadMetadataListField(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& object, const AStringView diagnosticPrefix, const AStringView fieldName){
    const auto listValue = FindMetadataListField(nwbFilePath, object, diagnosticPrefix, fieldName);
    if(!listValue)
        return MakeUnexpected(listValue.error());

    const auto& list = listValue.value().get()->asList();
    return Span<const MetadataValue>(list.data(), list.size());
}

template<typename CookEntryT, typename PathArenaT>
[[nodiscard]] inline b8 AssignCookEntryVirtualPath(const Name& virtualPath, const ::Path<PathArenaT>& nwbFilePath, const AStringView diagnosticPrefix, CookEntryT& outEntry){
    outEntry.virtualPath = virtualPath;
    if(outEntry.virtualPath)
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': virtual path must not be empty"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

template<typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline b8 CheckMetadataAssetMap(const ::Path<PathArenaT>& nwbFilePath, const MetadataValue& asset, const AStringView diagnosticPrefix){
    if(asset.isMap())
        return true;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': asset is not a map"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath));
    return false;
}

template<typename MetadataDocument, typename MetadataValue, typename PathArenaT>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataAssetValue(const ::Path<PathArenaT>& nwbFilePath, const MetadataDocument& doc, const AStringView diagnosticPrefix){
    const auto* asset = doc.findVariable(s_MetadataAssetVariable);
    if(!asset){
        NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': missing asset declaration '{}'"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(s_MetadataAssetVariable));
        return MakeUnexpected(Failure{});
    }
    return MakeNotNull(asset);
}

template<typename MetadataDocument, typename MetadataValue, typename PathArenaT>
[[nodiscard]] inline Expected<NotNull<const MetadataValue*>> FindMetadataAssetMapValue(const ::Path<PathArenaT>& nwbFilePath, const MetadataDocument& doc, const AStringView diagnosticPrefix){
    const auto asset = FindMetadataAssetValue<MetadataDocument, MetadataValue>(nwbFilePath, doc, diagnosticPrefix);
    if(!asset)
        return MakeUnexpected(asset.error());
    if(!CheckMetadataAssetMap(nwbFilePath, **asset, diagnosticPrefix))
        return MakeUnexpected(Failure{});
    return *asset;
}

template<typename PathArenaT, typename MetadataDocument, typename Visitor>
inline b8 VisitMetadataAssetMap(const ::Path<PathArenaT>& nwbFilePath, const MetadataDocument& doc, const AStringView diagnosticPrefix, Visitor&& visitor){
    using MetadataValue = RemoveCVRef_T<decltype(*DeclVal<const MetadataDocument&>().findVariable(s_MetadataAssetVariable))>;
    const auto assetMap = FindMetadataAssetMapValue<MetadataDocument, MetadataValue>(nwbFilePath, doc, diagnosticPrefix);
    if(!assetMap)
        return false;
    const auto& value = *assetMap.value().get();
    if constexpr(IsSame_V<decltype(visitor(value)), b8>)
        return visitor(value);
    else{
        visitor(value);
        return true;
    }
}

template<typename NamedEnumT, typename PathArenaT, typename MetadataValue>
[[nodiscard]] inline Expected<NamedEnumT> ParseNamedMetadataEnumField(
    const ::Path<PathArenaT>& nwbFilePath,
    const MetadataValue& object,
    const AStringView diagnosticPrefix,
    const AStringView fieldName,
    const Span<const ::NamedEnumCase<NamedEnumT>> cases,
    const AStringView errorDetailText
){
    const auto text = ReadMetadataStringField(nwbFilePath, object, diagnosticPrefix, fieldName, true);
    if(!text)
        return MakeUnexpected(text.error());
    const auto value = ParseNamedEnumText<NamedEnumT>(text->text, cases);
    if(value)
        return value;

    NWB_LOGGER_ERROR(FOUNDATION_TEXT("{} '{}': field '{}' {}"), ConvertString(diagnosticPrefix), PathToString<tchar>(nwbFilePath), ConvertString(fieldName), ConvertString(errorDetailText));
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

