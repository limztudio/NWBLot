// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"
#include "binary_payload.h"

#include <core/assets/paths.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_skin_cook_metadata{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_DiagnosticPrefix = "UI skin meta";

static constexpr Core::Assets::NamedEnumCase<UiSkinDrawMode::Enum> s_DrawModeCases[] = {
    { "sprite", UiSkinDrawMode::Sprite },
    { "nine_slice", UiSkinDrawMode::NineSlice },
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadU32Value(const Path& path, const Value& value, const AStringView fieldName, u32& outValue){
    if(!value.isInteger() || !FitsU32(value.asInteger())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must contain integers in the u32 range")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
        );
        return false;
    }
    outValue = static_cast<u32>(value.asInteger());
    return true;
}

template<usize Count>
[[nodiscard]] static bool ReadU32List(
    const Path& path,
    const Value& object,
    const AStringView fieldName,
    const bool required,
    u32 (&outValues)[Count]){
    const Value* field = FindField(object, fieldName);
    if(!field && !required)
        return true;
    if(!field || !field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a {}-component integer list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return false;
    }
    for(usize index = 0u; index < Count; ++index){
        if(!ReadU32Value(path, field->asList()[index], fieldName, outValues[index]))
            return false;
    }
    return true;
}

template<usize Count>
[[nodiscard]] static bool ReadLogicalList(const Path& path, const Value& object, const AStringView fieldName, f32 (&outValues)[Count]){
    const Value* field = FindField(object, fieldName);
    if(!field)
        return true;
    if(!field->isList() || field->asList().size() != Count){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a {}-component numeric list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(path)
            , StringConvert(fieldName)
            , Count
        );
        return false;
    }
    for(usize index = 0u; index < Count; ++index){
        if(!Core::Assets::ReadMetadataFiniteF32Value(path, field->asList()[index], s_DiagnosticPrefix, fieldName, outValues[index]))
            return false;
    }
    return true;
}

[[nodiscard]] static bool ParseRegion(const Path& path, const Value& object, UiSkinRegion& outRegion){
    if(!Core::Assets::CheckMetadataAssetMap(path, object, s_DiagnosticPrefix))
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(
        path,
        object,
        s_DiagnosticPrefix,
        { "name", "rect", "draw_mode", "slice", "padding", "minimum_size" }
    ))
        return false;
    if(!Core::Assets::ReadMetadataNameField(path, object, s_DiagnosticPrefix, "name", true, outRegion.name))
        return false;

    if(FindField(object, "draw_mode")){
        if(!Core::Assets::ParseNamedMetadataEnumField(
            path,
            object,
            s_DiagnosticPrefix,
            "draw_mode",
            s_DrawModeCases,
            LengthOf(s_DrawModeCases),
            outRegion.drawMode,
            "must be 'sprite' or 'nine_slice'"
        ))
            return false;
    }

    u32 rectangle[4u] = {};
    u32 slice[4u] = {};
    f32 padding[4u] = {};
    f32 minimumSize[2u] = {};
    if(
        !ReadU32List(path, object, "rect", true, rectangle)
        || !ReadU32List(path, object, "slice", outRegion.drawMode == UiSkinDrawMode::NineSlice, slice)
        || !ReadLogicalList(path, object, "padding", padding)
        || !ReadLogicalList(path, object, "minimum_size", minimumSize)
    )
        return false;

    outRegion.rectangle = { rectangle[0u], rectangle[1u], rectangle[2u], rectangle[3u] };
    outRegion.sliceInsets = { slice[0u], slice[1u], slice[2u], slice[3u] };
    outRegion.padding = { padding[0u], padding[1u], padding[2u], padding[3u] };
    outRegion.minimumWidth = minimumSize[0u];
    outRegion.minimumHeight = minimumSize[1u];
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseUiSkinCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    UiSkinCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena){
    using namespace __hidden_ui_skin_cook_metadata;

    UiSkinCookEntry parsed(outEntry.arena);
    const Value& asset = doc.asset();
    if(!Core::Assets::CheckMetadataAssetMap(nwbFilePath, asset, s_DiagnosticPrefix))
        return false;
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        { "schema_version", "texture", "atlas_extent", "reference_density", "toolkit_contract", "regions" }
    ))
        return false;

    const Value* schema = FindField(asset, "schema_version");
    u32 schemaVersion = 0u;
    if(!schema || !ReadU32Value(nwbFilePath, *schema, "schema_version", schemaVersion) || schemaVersion != UiSkinBinaryPayload::s_UiSkinVersion){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': schema_version must be {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , UiSkinBinaryPayload::s_UiSkinVersion
        );
        return false;
    }
    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, parsed.virtualPath, scratchArena))
        return false;
    if(!Core::Assets::ReadMetadataAssetRefField(nwbFilePath, asset, s_DiagnosticPrefix, "texture", true, parsed.texture))
        return false;

    u32 extent[2u] = {};
    if(!ReadU32List(nwbFilePath, asset, "atlas_extent", true, extent))
        return false;
    parsed.atlasWidth = extent[0u];
    parsed.atlasHeight = extent[1u];
    if(!Core::Assets::ReadMetadataFiniteF32Field(nwbFilePath, asset, s_DiagnosticPrefix, "reference_density", true, parsed.referenceDensity))
        return false;

    AStringView toolkitContract;
    bool hasToolkitContract = false;
    if(!Core::Assets::ReadMetadataStringField(
        nwbFilePath, asset, s_DiagnosticPrefix, "toolkit_contract", false, toolkitContract, &hasToolkitContract
    ))
        return false;
    if(hasToolkitContract && toolkitContract != "widgets_v1"){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': toolkit_contract must be 'widgets_v1'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    parsed.completeToolkitSkin = hasToolkitContract;

    const Value* regions = Core::Assets::FindMetadataListField(nwbFilePath, asset, s_DiagnosticPrefix, "regions");
    if(!regions)
        return false;
    if(regions->asList().size() > s_UiSkinMaxRegionCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': regions exceed schema limit {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , s_UiSkinMaxRegionCount
        );
        return false;
    }
    if(regions->asList().empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': regions must be nonempty and fit the supported count")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    parsed.regions.reserve(regions->asList().size());
    for(const Value& value : regions->asList()){
        UiSkinRegion region;
        if(!ParseRegion(nwbFilePath, value, region))
            return false;
        parsed.regions.push_back(region);
    }

    UiSkin validated(outEntry.arena);
    if(!BuildUiSkinAsset(parsed, validated))
        return false;

    outEntry.regions = Move(parsed.regions);
    outEntry.texture = parsed.texture;
    outEntry.virtualPath = parsed.virtualPath;
    outEntry.atlasWidth = parsed.atlasWidth;
    outEntry.atlasHeight = parsed.atlasHeight;
    outEntry.referenceDensity = parsed.referenceDensity;
    outEntry.completeToolkitSkin = parsed.completeToolkitSkin;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

