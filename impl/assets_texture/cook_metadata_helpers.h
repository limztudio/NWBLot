// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "cook.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TextureCookDetail{
// Shared metadata readers cross the fields/mips/meta translation units.
using Core::Metascript::Value;
using Core::Metascript::FindField;
using TextureFormat::ComputeCompleteMipCount;
using TextureFormat::GetTexturePayloadBlockLayout;
using TextureFormat::s_AlphaConstantUnorm8Mode;
using TextureFormat::s_AlphaOpaqueMode;
using TextureFormat::s_AlphaUastcLdr4x4Mode;
using TextureFormat::s_ClampMipAddressMode;
using TextureFormat::s_LinearColorSpace;
using TextureFormat::s_MaxConstantAlphaUnorm8;
using TextureFormat::s_MipMajorSliceMajorBlocksPayloadLayout;
using TextureFormat::s_OpaqueAlphaUnorm8;
using TextureFormat::s_SrgbColorSpace;
using TextureFormat::s_UastcHdr4x4Format;
using TextureFormat::s_UastcHdrTextureMetadataVersion;
using TextureFormat::s_UastcLdr4x4Format;
using TextureFormat::s_UastcLdrTextureMetadataVersion;
using TextureFormat::s_UastcSpecificationRevision;
inline constexpr AStringView s_DiagnosticPrefix = "Texture meta";
inline constexpr AStringView s_VersionField = "version";
inline constexpr AStringView s_FormatField = "format";
inline constexpr AStringView s_UastcSpecificationRevisionField = "uastc_spec_revision";
inline constexpr AStringView s_UastcHdrSpecificationRevisionField = "uastc_hdr_spec_revision";
inline constexpr AStringView s_ColorSpaceField = "color_space";
inline constexpr AStringView s_DimensionField = "dimension";
inline constexpr AStringView s_WidthField = "width";
inline constexpr AStringView s_HeightField = "height";
inline constexpr AStringView s_DepthField = "depth";
inline constexpr AStringView s_BlockWidthField = "block_width";
inline constexpr AStringView s_BlockHeightField = "block_height";
inline constexpr AStringView s_BytesPerBlockField = "bytes_per_block";
inline constexpr AStringView s_PayloadLayoutField = "payload_layout";
inline constexpr AStringView s_MipAddressModeField = "mip_address_mode";
inline constexpr AStringView s_HasAlphaField = "has_alpha";
inline constexpr AStringView s_AlphaModeField = "alpha_mode";
inline constexpr AStringView s_AlphaConstantUnorm8Field = "alpha_constant_unorm8";
inline constexpr AStringView s_AlphaPayloadOffsetBytesField = "alpha_payload_offset_bytes";
inline constexpr AStringView s_AlphaPayloadByteCountField = "alpha_payload_byte_count";
inline constexpr AStringView s_AlphaUastcSpecificationRevisionField = "alpha_uastc_spec_revision";
inline constexpr AStringView s_MipCountField = "mip_count";
inline constexpr AStringView s_DataField = "data";
inline constexpr AStringView s_MipsField = "mips";

inline constexpr AStringView s_LevelField = "level";
inline constexpr AStringView s_BlocksXField = "blocks_x";
inline constexpr AStringView s_BlocksYField = "blocks_y";
inline constexpr AStringView s_SlicesField = "slices";
inline constexpr AStringView s_OffsetBytesField = "offset_bytes";
inline constexpr AStringView s_SizeBytesField = "size_bytes";

template<typename IntegerT>
[[nodiscard]] inline bool ReadRequiredUnsignedField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const IntegerT minimum,
    const IntegerT maximum,
    IntegerT& outValue
){
    outValue = 0u;

    const Value* const field = FindField(asset, fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!field->isInteger() || field->asInteger() < 0){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a non-negative integer")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const u64 value = static_cast<u64>(field->asInteger());
    if(value < static_cast<u64>(minimum) || value > static_cast<u64>(maximum)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is outside the supported range")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    outValue = static_cast<IntegerT>(value);
    return true;
}

template<typename IntegerT>
[[nodiscard]] inline bool ReadExactUnsignedField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const IntegerT expectedValue
){
    IntegerT value = 0u;
    return ReadRequiredUnsignedField(nwbFilePath, asset, fieldName, expectedValue, expectedValue, value);
}
[[nodiscard]] bool ReadExactStringField(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    AStringView fieldName,
    AStringView expectedValue
);
[[nodiscard]] bool ReadTextureDimension(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    TextureDimension::Enum& outDimension
);
[[nodiscard]] bool ValidateTextureDataFileName(
    const Path& nwbFilePath,
    AStringView dataFileName,
    Core::Alloc::ScratchArena& scratchArena
);
[[nodiscard]] bool ParseMipLevels(
    const Path& nwbFilePath,
    const Core::Metascript::Value& asset,
    TexturePayloadFormat::Enum payloadFormat,
    TextureDimension::Enum dimension,
    u32 width,
    u32 height,
    u32 depth,
    u32 expectedMipCount,
    Texture::MipLevelVector& outMipLevels
);
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

