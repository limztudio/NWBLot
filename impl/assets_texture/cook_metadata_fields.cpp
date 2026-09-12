// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook_metadata_helpers.h"

#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/filesystem.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureCookDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TextureFormat::ComputeMipPlaneBlockLayout;
using TextureFormat::ComputeMipSliceCount;
using TextureFormat::s_Texture2DDimension;
using TextureFormat::s_Texture3DDimension;
using TextureFormat::s_TextureCubeDimension;
using TextureFormat::s_TextureDataExtension;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename IntegerT>

[[nodiscard]] bool ReadExactStringField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const AStringView expectedValue
){
    AStringView value;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, fieldName, true, value))
        return false;
    if(value == expectedValue)
        return true;

    NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}'")
        , StringConvert(s_DiagnosticPrefix)
        , PathToString<tchar>(nwbFilePath)
        , StringConvert(fieldName)
        , StringConvert(expectedValue)
    );
    return false;
}

[[nodiscard]] bool ReadTextureDimension(
    const Path& nwbFilePath,
    const Value& asset,
    TextureDimension::Enum& outDimension
){
    AStringView text;
    if(!::NWB::Core::Assets::ReadMetadataStringField(nwbFilePath, asset, s_DiagnosticPrefix, s_DimensionField, true, text))
        return false;
    if(text == s_Texture2DDimension)
        outDimension = TextureDimension::Texture2D;
    else if(text == s_TextureCubeDimension)
        outDimension = TextureDimension::TextureCube;
    else if(text == s_Texture3DDimension)
        outDimension = TextureDimension::Texture3D;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}', '{}', or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_DimensionField)
            , StringConvert(s_Texture2DDimension)
            , StringConvert(s_TextureCubeDimension)
            , StringConvert(s_Texture3DDimension)
        );
        return false;
    }
    return true;
}

[[nodiscard]] bool ValidateTextureDataFileName(
    const Path& nwbFilePath,
    const AStringView dataFileName,
    Core::Alloc::ScratchArena& scratchArena
){
    if(
        dataFileName.empty()
        || dataFileName == "."
        || dataFileName == ".."
        || dataFileName.find('/') != AStringView::npos
        || dataFileName.find('\\') != AStringView::npos
        || dataFileName.find(':') != AStringView::npos
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a sidecar filename without path components")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_DataField)
        );
        return false;
    }

    const Path dataPath(nwbFilePath.arena(), dataFileName);
    if(dataPath.is_absolute() || dataPath.filename().native() != dataPath.native()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a relative sidecar filename")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_DataField)
        );
        return false;
    }

    AString<Core::Alloc::ScratchArena> extension = PathToString(scratchArena, dataPath.extension());
    CanonicalizeTextInPlace(extension);
    if(extension != s_TextureDataExtension){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must reference a .tex sidecar")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_DataField)
        );
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

