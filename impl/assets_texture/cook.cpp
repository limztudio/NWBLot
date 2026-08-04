// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_COOK)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "cook.h"

#include "binary_payload.h"

#include <core/assets/binary_payload_io.h>
#include <core/assets/paths.h>
#include <core/common/log.h>
#include <global/binary.h>
#include <global/filesystem.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_texture_cook{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core::Metascript;

static constexpr AStringView s_DiagnosticPrefix = "Texture meta";
static constexpr AStringView s_VersionField = "version";
static constexpr AStringView s_FormatField = "format";
static constexpr AStringView s_UastcSpecificationRevisionField = "uastc_spec_revision";
static constexpr AStringView s_UastcHdrSpecificationRevisionField = "uastc_hdr_spec_revision";
static constexpr AStringView s_ColorSpaceField = "color_space";
static constexpr AStringView s_DimensionField = "dimension";
static constexpr AStringView s_WidthField = "width";
static constexpr AStringView s_HeightField = "height";
static constexpr AStringView s_DepthField = "depth";
static constexpr AStringView s_BlockWidthField = "block_width";
static constexpr AStringView s_BlockHeightField = "block_height";
static constexpr AStringView s_BytesPerBlockField = "bytes_per_block";
static constexpr AStringView s_PayloadLayoutField = "payload_layout";
static constexpr AStringView s_MipAddressModeField = "mip_address_mode";
static constexpr AStringView s_HasAlphaField = "has_alpha";
static constexpr AStringView s_AlphaModeField = "alpha_mode";
static constexpr AStringView s_AlphaConstantUnorm8Field = "alpha_constant_unorm8";
static constexpr AStringView s_AlphaPayloadOffsetBytesField = "alpha_payload_offset_bytes";
static constexpr AStringView s_AlphaPayloadByteCountField = "alpha_payload_byte_count";
static constexpr AStringView s_AlphaUastcSpecificationRevisionField = "alpha_uastc_spec_revision";
static constexpr AStringView s_MipCountField = "mip_count";
static constexpr AStringView s_DataField = "data";
static constexpr AStringView s_MipsField = "mips";

static constexpr AStringView s_LevelField = "level";
static constexpr AStringView s_BlocksXField = "blocks_x";
static constexpr AStringView s_BlocksYField = "blocks_y";
static constexpr AStringView s_SlicesField = "slices";
static constexpr AStringView s_OffsetBytesField = "offset_bytes";
static constexpr AStringView s_SizeBytesField = "size_bytes";

using TextureFormat::ComputeCompleteMipCount;
using TextureFormat::ComputeMipPlaneBlockLayout;
using TextureFormat::ComputeMipSliceCount;
using TextureFormat::GetTexturePayloadBlockLayout;
using TextureFormat::s_ClampMipAddressMode;
using TextureFormat::s_AlphaConstantUnorm8Mode;
using TextureFormat::s_AlphaOpaqueMode;
using TextureFormat::s_AlphaUastcLdr4x4Mode;
using TextureFormat::s_LinearColorSpace;
using TextureFormat::s_MipMajorSliceMajorBlocksPayloadLayout;
using TextureFormat::s_SrgbColorSpace;
using TextureFormat::s_Texture2DDimension;
using TextureFormat::s_Texture3DDimension;
using TextureFormat::s_TextureCubeDimension;
using TextureFormat::s_TextureDataExtension;
using TextureFormat::s_UastcLdrTextureMetadataVersion;
using TextureFormat::s_UastcLdr4x4Format;
using TextureFormat::s_UastcHdr4x4Format;
using TextureFormat::s_UastcHdrTextureMetadataVersion;
using TextureFormat::s_UastcSpecificationRevision;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ReadRequiredStringField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    AStringView& outValue
){
    outValue = {};

    const Value* const field = FindField(asset, fieldName);
    if(!field){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is required")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    if(!field->isString()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a string")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }

    const MStringView text = field->asString();
    outValue = AStringView(text.data(), text.size());
    if(outValue.empty()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must not be empty")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(fieldName)
        );
        return false;
    }
    return true;
}

template<typename IntegerT>
[[nodiscard]] static bool ReadRequiredUnsignedField(
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
[[nodiscard]] static bool ReadExactUnsignedField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const IntegerT expectedValue
){
    IntegerT value = 0u;
    return ReadRequiredUnsignedField(nwbFilePath, asset, fieldName, expectedValue, expectedValue, value);
}

[[nodiscard]] static bool ReadExactStringField(
    const Path& nwbFilePath,
    const Value& asset,
    const AStringView fieldName,
    const AStringView expectedValue
){
    AStringView value;
    if(!ReadRequiredStringField(nwbFilePath, asset, fieldName, value))
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

[[nodiscard]] static bool ReadTextureDimension(
    const Path& nwbFilePath,
    const Value& asset,
    TextureDimension::Enum& outDimension
){
    AStringView text;
    if(!ReadRequiredStringField(nwbFilePath, asset, s_DimensionField, text))
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

[[nodiscard]] static bool ValidateTextureDataFileName(
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

[[nodiscard]] static bool ParseMipLevels(
    const Path& nwbFilePath,
    const Value& asset,
    const TexturePayloadFormat::Enum payloadFormat,
    const TextureDimension::Enum dimension,
    const u32 width,
    const u32 height,
    const u32 depth,
    const u32 expectedMipCount,
    Texture::MipLevelVector& outMipLevels
){
    outMipLevels.clear();

    const Value* const mipsField = FindField(asset, s_MipsField);
    if(!mipsField || !mipsField->isList()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be a list")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipsField)
        );
        return false;
    }

    const Value::ListType& mips = mipsField->asList();
    if(mips.size() != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must contain a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipsField)
        );
        return false;
    }

    outMipLevels.reserve(mips.size());
    u32 expectedWidth = width;
    u32 expectedHeight = height;
    u32 expectedDepth = depth;
    u64 expectedOffsetBytes = 0u;
    for(usize mipIndex = 0u; mipIndex < mips.size(); ++mipIndex){
        const Value& mipValue = mips[mipIndex];
        if(!mipValue.isMap()){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] must be a map")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(!Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            mipValue,
            "Texture mip",
            { s_LevelField, s_WidthField, s_HeightField, s_SlicesField, s_BlocksXField, s_BlocksYField, s_OffsetBytesField, s_SizeBytesField }
        ))
            return false;

        u32 level = 0u;
        u32 mipWidth = 0u;
        u32 mipHeight = 0u;
        u32 sliceCount = 0u;
        u32 blockCountX = 0u;
        u32 blockCountY = 0u;
        u64 offsetBytes = 0u;
        u64 sizeBytes = 0u;
        if(
            !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_LevelField, 0u, Limit<u32>::s_Max, level)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_WidthField, 1u, Limit<u32>::s_Max, mipWidth)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_HeightField, 1u, Limit<u32>::s_Max, mipHeight)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_SlicesField, 1u, Limit<u32>::s_Max, sliceCount)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksXField, 1u, Limit<u32>::s_Max, blockCountX)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksYField, 1u, Limit<u32>::s_Max, blockCountY)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_OffsetBytesField, static_cast<u64>(0u), Limit<u64>::s_Max, offsetBytes)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_SizeBytesField, static_cast<u64>(1u), Limit<u64>::s_Max, sizeBytes)
        )
            return false;
        u32 expectedBlockCountX = 0u;
        u32 expectedBlockCountY = 0u;
        u64 expectedSliceSizeBytes = 0u;
        if(!ComputeMipPlaneBlockLayout(
            payloadFormat,
            expectedWidth,
            expectedHeight,
            expectedBlockCountX,
            expectedBlockCountY,
            expectedSliceSizeBytes
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] block grid exceeds runtime limits")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        u32 expectedSliceCount = 0u;
        if(!ComputeMipSliceCount(dimension, expectedDepth, expectedSliceCount)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] has an invalid slice count")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(expectedSliceSizeBytes > Limit<u64>::s_Max / expectedSliceCount){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] byte size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        const u64 expectedSizeBytes = expectedSliceSizeBytes * expectedSliceCount;

        if(
            level != mipIndex
            || mipWidth != expectedWidth
            || mipHeight != expectedHeight
            || sliceCount != expectedSliceCount
            || blockCountX != expectedBlockCountX
            || blockCountY != expectedBlockCountY
            || offsetBytes != expectedOffsetBytes
            || sizeBytes != expectedSizeBytes
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] does not match the required contiguous texture mip chain")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(expectedSizeBytes > Limit<u64>::s_Max - expectedOffsetBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mip payload offsets overflow")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }

        TextureMipLevel mip;
        mip.width = mipWidth;
        mip.height = mipHeight;
        mip.blockCountX = blockCountX;
        mip.blockCountY = blockCountY;
        mip.offsetBytes = offsetBytes;
        mip.sizeBytes = sizeBytes;
        mip.sliceCount = sliceCount;
        outMipLevels.push_back(mip);

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = expectedWidth > 1u ? expectedWidth >> 1u : 1u;
        expectedHeight = expectedHeight > 1u ? expectedHeight >> 1u : 1u;
        if(dimension == TextureDimension::Texture3D)
            expectedDepth = expectedDepth > 1u ? expectedDepth >> 1u : 1u;
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool TextureAssetCodec::serialize(const Core::Assets::IAsset& asset, Core::Assets::AssetBytes& outBinary)const{
    if(asset.assetType() != assetType()){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: invalid asset type '{}', expected '{}'")
            , StringConvert(asset.assetType().c_str())
            , StringConvert(Texture::s_AssetTypeText)
        );
        return false;
    }

    const Texture& texture = static_cast<const Texture&>(asset);
    if(!texture.validatePayload())
        return false;
    if(texture.mipLevels().size() > Limit<u32>::s_Max){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: mip count exceeds cooked payload limits"));
        return false;
    }

    Core::Assets::AssetVector<TextureBinaryPayload::MipLevelBinary> mipBinaries(outBinary.get_allocator().arena());
    mipBinaries.reserve(texture.mipLevels().size());
    for(const TextureMipLevel& mip : texture.mipLevels()){
        TextureBinaryPayload::MipLevelBinary binaryMip;
        binaryMip.width = mip.width;
        binaryMip.height = mip.height;
        binaryMip.sliceCount = mip.sliceCount;
        binaryMip.blockCountX = mip.blockCountX;
        binaryMip.blockCountY = mip.blockCountY;
        binaryMip.offsetBytes = mip.offsetBytes;
        binaryMip.sizeBytes = mip.sizeBytes;
        mipBinaries.push_back(binaryMip);
    }

    const bool writeLegacyLdrHeader = texture.payloadFormat() == TexturePayloadFormat::UastcLdr4x4;
    const usize headerByteCount = writeLegacyLdrHeader
        ? sizeof(TextureBinaryPayload::HeaderBinaryV2)
        : sizeof(TextureBinaryPayload::HeaderBinary)
    ;
    usize reserveBytes = headerByteCount;
    if(
        !AddBinaryRepeatedReserveBytes(reserveBytes, mipBinaries.size(), sizeof(TextureBinaryPayload::MipLevelBinary))
        || !AddBinaryReserveBytes(reserveBytes, texture.payloadBytes().size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    if(writeLegacyLdrHeader){
        TextureBinaryPayload::HeaderBinaryV2 header;
        header.colorSpace = static_cast<u32>(texture.colorSpace());
        header.dimension = static_cast<u32>(texture.dimension());
        header.width = texture.width();
        header.height = texture.height();
        header.depth = texture.depth();
        header.mipCount = static_cast<u32>(mipBinaries.size());
        header.hasAlpha = texture.hasAlpha() ? 1u : 0u;
        header.uastcByteCount = static_cast<u64>(texture.payloadBytes().size());
        AppendPOD(outBinary, header);
    }
    else{
        TextureBinaryPayload::HeaderBinary header;
        header.colorSpace = static_cast<u32>(texture.colorSpace());
        header.dimension = static_cast<u32>(texture.dimension());
        header.width = texture.width();
        header.height = texture.height();
        header.depth = texture.depth();
        header.mipCount = static_cast<u32>(mipBinaries.size());
        header.alphaInfo = static_cast<u32>(texture.alphaMode())
            | (static_cast<u32>(texture.alphaConstantUnorm8()) << TextureBinaryPayload::s_AlphaInfoConstantShift)
        ;
        header.payloadFormat = static_cast<u32>(texture.payloadFormat());
        header.payloadByteCount = static_cast<u64>(texture.payloadBytes().size());
        AppendPOD(outBinary, header);
    }
    if(!Core::Assets::AppendVectorPayload(
        outBinary,
        mipBinaries,
        NWB_TEXT("TextureAssetCodec::serialize"),
        NWB_TEXT("mip levels")
    ))
        return false;

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, texture.payloadBytes().data(), texture.payloadBytes().size());
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool ParseTextureCookMetadata(
    const Path& assetRoot,
    const AStringView virtualRoot,
    const Path& nwbFilePath,
    const Core::Metascript::Document& doc,
    TextureCookEntry& outEntry,
    Core::Alloc::ScratchArena& scratchArena
){
    using namespace __hidden_texture_cook;

    outEntry = TextureCookEntry(outEntry.mipLevels.get_allocator().arena());

    const Value& asset = doc.asset();
    if(!asset.isMap()){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': asset is not a map")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(
        !Core::Assets::ValidateMetadataAssetFields(
            nwbFilePath,
            asset,
            s_DiagnosticPrefix,
            {
                s_VersionField,
                s_FormatField,
                s_UastcSpecificationRevisionField,
                s_UastcHdrSpecificationRevisionField,
                s_ColorSpaceField,
                s_DimensionField,
                s_WidthField,
                s_HeightField,
                s_DepthField,
                s_BlockWidthField,
                s_BlockHeightField,
                s_BytesPerBlockField,
                s_PayloadLayoutField,
                s_MipAddressModeField,
                s_HasAlphaField,
                s_AlphaModeField,
                s_AlphaConstantUnorm8Field,
                s_AlphaPayloadOffsetBytesField,
                s_AlphaPayloadByteCountField,
                s_AlphaUastcSpecificationRevisionField,
                s_MipCountField,
                s_DataField,
                s_MipsField,
            }
        )
        || !ReadTextureDimension(nwbFilePath, asset, outEntry.dimension)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_DepthField, 1u, Limit<u32>::s_Max, outEntry.depth)
    )
        return false;

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outEntry.virtualPath, scratchArena))
        return false;

    u32 metadataVersion = 0u;
    u32 hasAlpha = 0u;
    u32 mipCount = 0u;
    u64 alphaPayloadOffsetBytes = 0u;
    u64 alphaPayloadByteCount = 0u;
    AStringView format;
    if(!ReadRequiredStringField(nwbFilePath, asset, s_FormatField, format))
        return false;

    u32 expectedBlockWidth = 0u;
    u32 expectedBlockHeight = 0u;
    u32 expectedBytesPerBlock = 0u;
    if(format == s_UastcLdr4x4Format){
        outEntry.payloadFormat = TexturePayloadFormat::UastcLdr4x4;
        if(
            !ReadRequiredUnsignedField(
                nwbFilePath,
                asset,
                s_VersionField,
                s_UastcLdrTextureMetadataVersion,
                s_UastcLdrTextureMetadataVersion,
                metadataVersion
            )
            || !ReadExactStringField(nwbFilePath, asset, s_UastcSpecificationRevisionField, s_UastcSpecificationRevision)
        )
            return false;
        if(
            FindField(asset, s_UastcHdrSpecificationRevisionField)
            || FindField(asset, s_AlphaModeField)
            || FindField(asset, s_AlphaConstantUnorm8Field)
            || FindField(asset, s_AlphaPayloadOffsetBytesField)
            || FindField(asset, s_AlphaPayloadByteCountField)
            || FindField(asset, s_AlphaUastcSpecificationRevisionField)
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': HDR alpha fields are not valid for '{}' textures")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_UastcLdr4x4Format)
            );
            return false;
        }
    }
    else if(format == s_UastcHdr4x4Format){
        outEntry.payloadFormat = TexturePayloadFormat::UastcHdr4x4;
        if(
            !ReadRequiredUnsignedField(
                nwbFilePath,
                asset,
                s_VersionField,
                s_UastcHdrTextureMetadataVersion,
                s_UastcHdrTextureMetadataVersion,
                metadataVersion
            )
            || !ReadExactStringField(nwbFilePath, asset, s_UastcHdrSpecificationRevisionField, s_UastcSpecificationRevision)
        )
            return false;
        if(FindField(asset, s_UastcSpecificationRevisionField)){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' is only valid for '{}' textures")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_UastcSpecificationRevisionField)
                , StringConvert(s_UastcLdr4x4Format)
            );
            return false;
        }
    }
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_FormatField)
            , StringConvert(s_UastcLdr4x4Format)
            , StringConvert(s_UastcHdr4x4Format)
        );
        return false;
    }
    if(!GetTexturePayloadBlockLayout(
        outEntry.payloadFormat,
        expectedBlockWidth,
        expectedBlockHeight,
        expectedBytesPerBlock
    ))
        return false;

    if(
        !ReadRequiredUnsignedField(nwbFilePath, asset, s_WidthField, 1u, Limit<u32>::s_Max, outEntry.width)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HeightField, 1u, Limit<u32>::s_Max, outEntry.height)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockWidthField, expectedBlockWidth)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockHeightField, expectedBlockHeight)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BytesPerBlockField, expectedBytesPerBlock)
        || !ReadExactStringField(
            nwbFilePath,
            asset,
            s_PayloadLayoutField,
            s_MipMajorSliceMajorBlocksPayloadLayout
        )
        || !ReadExactStringField(nwbFilePath, asset, s_MipAddressModeField, s_ClampMipAddressMode)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HasAlphaField, 0u, 1u, hasAlpha)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_MipCountField, 1u, Limit<u32>::s_Max, mipCount)
    )
        return false;

    AStringView colorSpace;
    if(!ReadRequiredStringField(nwbFilePath, asset, s_ColorSpaceField, colorSpace))
        return false;
    if(colorSpace == s_LinearColorSpace)
        outEntry.colorSpace = TextureColorSpace::Linear;
    else if(colorSpace == s_SrgbColorSpace)
        outEntry.colorSpace = TextureColorSpace::Srgb;
    else{
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must be '{}' or '{}'")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_ColorSpaceField)
            , StringConvert(s_LinearColorSpace)
            , StringConvert(s_SrgbColorSpace)
        );
        return false;
    }
    if(IsHdrTexturePayloadFormat(outEntry.payloadFormat) && outEntry.colorSpace != TextureColorSpace::Linear){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': '{}' textures must use '{}' color space")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_UastcHdr4x4Format)
            , StringConvert(s_LinearColorSpace)
        );
        return false;
    }

    if(outEntry.dimension != TextureDimension::Texture3D && outEntry.depth != 1u){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': non-volume textures must have depth 1")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }
    if(outEntry.dimension == TextureDimension::TextureCube && outEntry.width != outEntry.height){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': cubemap faces must be square")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    u32 expectedMipCount = 0u;
    if(!ComputeCompleteMipCount(outEntry.dimension, outEntry.width, outEntry.height, outEntry.depth, expectedMipCount) || mipCount != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must describe a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipCountField)
        );
        return false;
    }
    if(!ParseMipLevels(
        nwbFilePath,
        asset,
        outEntry.payloadFormat,
        outEntry.dimension,
        outEntry.width,
        outEntry.height,
        outEntry.depth,
        expectedMipCount,
        outEntry.mipLevels
    ))
        return false;

    AStringView dataFileName;
    if(!ReadRequiredStringField(nwbFilePath, asset, s_DataField, dataFileName))
        return false;
    if(!ValidateTextureDataFileName(nwbFilePath, dataFileName, scratchArena))
        return false;

    Path dataPath(nwbFilePath.parent_path());
    dataPath /= dataFileName;
    ErrorCode errorCode;
    if(!ReadBinaryFile(dataPath, outEntry.payloadBytes, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read texture sidecar '{}': {}")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , PathToString<tchar>(dataPath)
            , StringConvert(errorCode.message())
        );
        return false;
    }

    u64 expectedPayloadBytes = 0u;
    for(const TextureMipLevel& mip : outEntry.mipLevels){
        if(mip.sizeBytes > Limit<u64>::s_Max - expectedPayloadBytes){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture payload size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        expectedPayloadBytes += mip.sizeBytes;
    }
    u64 expectedTotalPayloadBytes = expectedPayloadBytes;
    if(outEntry.payloadFormat == TexturePayloadFormat::UastcLdr4x4){
        outEntry.alphaMode = hasAlpha != 0u ? TextureAlphaMode::EmbeddedLdr : TextureAlphaMode::Opaque;
        outEntry.alphaConstantUnorm8 = 255u;
    }
    else{
        AStringView alphaModeText;
        if(!ReadRequiredStringField(nwbFilePath, asset, s_AlphaModeField, alphaModeText))
            return false;
        if(alphaModeText == s_AlphaOpaqueMode){
            outEntry.alphaMode = TextureAlphaMode::Opaque;
            outEntry.alphaConstantUnorm8 = 255u;
            if(
                hasAlpha != 0u
                || FindField(asset, s_AlphaConstantUnorm8Field)
                || FindField(asset, s_AlphaPayloadOffsetBytesField)
                || FindField(asset, s_AlphaPayloadByteCountField)
                || FindField(asset, s_AlphaUastcSpecificationRevisionField)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': opaque HDR alpha metadata must not carry an alpha payload")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
        }
        else if(alphaModeText == s_AlphaConstantUnorm8Mode){
            u32 alphaConstant = 0u;
            if(
                hasAlpha != 1u
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaConstantUnorm8Field,
                    0u,
                    254u,
                    alphaConstant
                )
                || FindField(asset, s_AlphaPayloadOffsetBytesField)
                || FindField(asset, s_AlphaPayloadByteCountField)
                || FindField(asset, s_AlphaUastcSpecificationRevisionField)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': constant HDR alpha metadata is invalid")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            outEntry.alphaMode = TextureAlphaMode::ConstantUnorm8;
            outEntry.alphaConstantUnorm8 = static_cast<u8>(alphaConstant);
        }
        else if(alphaModeText == s_AlphaUastcLdr4x4Mode){
            if(
                hasAlpha != 1u
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaPayloadOffsetBytesField,
                    static_cast<u64>(0u),
                    Limit<u64>::s_Max,
                    alphaPayloadOffsetBytes
                )
                || !ReadRequiredUnsignedField(
                    nwbFilePath,
                    asset,
                    s_AlphaPayloadByteCountField,
                    static_cast<u64>(0u),
                    Limit<u64>::s_Max,
                    alphaPayloadByteCount
                )
                || !ReadExactStringField(
                    nwbFilePath,
                    asset,
                    s_AlphaUastcSpecificationRevisionField,
                    s_UastcSpecificationRevision
                )
                || FindField(asset, s_AlphaConstantUnorm8Field)
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': separate HDR alpha metadata is invalid")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            if(
                alphaPayloadOffsetBytes != expectedPayloadBytes
                || alphaPayloadByteCount != expectedPayloadBytes
                || expectedPayloadBytes > Limit<u64>::s_Max - expectedTotalPayloadBytes
            ){
                NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': separate HDR alpha payload must mirror the RGB UASTC mip layout")
                    , StringConvert(s_DiagnosticPrefix)
                    , PathToString<tchar>(nwbFilePath)
                );
                return false;
            }
            expectedTotalPayloadBytes += expectedPayloadBytes;
            outEntry.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
            outEntry.alphaConstantUnorm8 = 255u;
        }
        else{
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' has an unsupported HDR alpha mode")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , StringConvert(s_AlphaModeField)
            );
            return false;
        }
    }

    if(expectedTotalPayloadBytes != static_cast<u64>(outEntry.payloadBytes.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': texture sidecar size does not match the mip and alpha metadata")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
        );
        return false;
    }

    outEntry.hasAlpha = hasAlpha != 0u;
    return true;
}

bool BuildTextureAsset(TextureCookEntry& textureEntry, Texture& outTexture){
    Core::Assets::AssetArena& arena = textureEntry.mipLevels.get_allocator().arena();
    outTexture = Texture(arena, textureEntry.virtualPath);
    outTexture.setPayload(
        textureEntry.colorSpace,
        textureEntry.hasAlpha,
        textureEntry.width,
        textureEntry.height,
        Move(textureEntry.mipLevels),
        Move(textureEntry.payloadBytes),
        textureEntry.dimension,
        textureEntry.depth,
        textureEntry.payloadFormat,
        textureEntry.alphaMode,
        textureEntry.alphaConstantUnorm8
    );
    return outTexture.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

