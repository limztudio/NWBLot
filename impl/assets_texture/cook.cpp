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
static constexpr AStringView s_ColorSpaceField = "color_space";
static constexpr AStringView s_WidthField = "width";
static constexpr AStringView s_HeightField = "height";
static constexpr AStringView s_BlockWidthField = "block_width";
static constexpr AStringView s_BlockHeightField = "block_height";
static constexpr AStringView s_BytesPerBlockField = "bytes_per_block";
static constexpr AStringView s_PayloadLayoutField = "payload_layout";
static constexpr AStringView s_MipAddressModeField = "mip_address_mode";
static constexpr AStringView s_HasAlphaField = "has_alpha";
static constexpr AStringView s_MipCountField = "mip_count";
static constexpr AStringView s_DataField = "data";
static constexpr AStringView s_MipsField = "mips";

static constexpr AStringView s_LevelField = "level";
static constexpr AStringView s_BlocksXField = "blocks_x";
static constexpr AStringView s_BlocksYField = "blocks_y";
static constexpr AStringView s_OffsetBytesField = "offset_bytes";
static constexpr AStringView s_SizeBytesField = "size_bytes";

static constexpr AStringView s_UastcLdr4x4Format = "uastc_ldr_4x4";
static constexpr AStringView s_UastcSpecificationRevision = "b624c07ad3c659e7b0f0badcb36e9a6b8820a99d";
static constexpr AStringView s_MipMajorBlocksPayloadLayout = "mip_major_blocks";
static constexpr AStringView s_ClampMipAddressMode = "clamp";
static constexpr AStringView s_LinearColorSpace = "linear";
static constexpr AStringView s_SrgbColorSpace = "srgb";
static constexpr AStringView s_TextureDataExtension = ".tex";
static constexpr u32 s_UastcBlockWidth = 4u;
static constexpr u32 s_UastcBlockHeight = 4u;
static constexpr u32 s_UastcBytesPerBlock = 16u;


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

[[nodiscard]] static bool ComputeCompleteMipCount(const u32 width, const u32 height, u32& outMipCount){
    outMipCount = 0u;
    if(width == 0u || height == 0u)
        return false;

    u32 mipWidth = width;
    u32 mipHeight = height;
    for(;;){
        if(outMipCount == Limit<u32>::s_Max)
            return false;
        ++outMipCount;

        if(mipWidth == 1u && mipHeight == 1u)
            return true;

        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }
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
    const u32 width,
    const u32 height,
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
            { s_LevelField, s_WidthField, s_HeightField, s_BlocksXField, s_BlocksYField, s_OffsetBytesField, s_SizeBytesField }
        ))
            return false;

        u32 level = 0u;
        u32 mipWidth = 0u;
        u32 mipHeight = 0u;
        u32 blockCountX = 0u;
        u32 blockCountY = 0u;
        u64 offsetBytes = 0u;
        u64 sizeBytes = 0u;
        if(
            !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_LevelField, 0u, Limit<u32>::s_Max, level)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_WidthField, 1u, Limit<u32>::s_Max, mipWidth)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_HeightField, 1u, Limit<u32>::s_Max, mipHeight)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksXField, 1u, Limit<u32>::s_Max, blockCountX)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_BlocksYField, 1u, Limit<u32>::s_Max, blockCountY)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_OffsetBytesField, static_cast<u64>(0u), Limit<u64>::s_Max, offsetBytes)
            || !ReadRequiredUnsignedField(nwbFilePath, mipValue, s_SizeBytesField, static_cast<u64>(1u), Limit<u64>::s_Max, sizeBytes)
        )
            return false;

        const u64 expectedBlockCountX = DivideUp(static_cast<u64>(expectedWidth), static_cast<u64>(s_UastcBlockWidth));
        const u64 expectedBlockCountY = DivideUp(static_cast<u64>(expectedHeight), static_cast<u64>(s_UastcBlockHeight));
        if(expectedBlockCountX > Limit<u32>::s_Max || expectedBlockCountY > Limit<u32>::s_Max){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] block grid exceeds runtime limits")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        if(expectedBlockCountX > Limit<u64>::s_Max / expectedBlockCountY){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] block count overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        const u64 expectedBlockCount = expectedBlockCountX * expectedBlockCountY;
        if(expectedBlockCount > Limit<u64>::s_Max / s_UastcBytesPerBlock){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] byte size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
                , mipIndex
            );
            return false;
        }
        const u64 expectedSizeBytes = expectedBlockCount * s_UastcBytesPerBlock;

        if(
            level != mipIndex
            || mipWidth != expectedWidth
            || mipHeight != expectedHeight
            || blockCountX != expectedBlockCountX
            || blockCountY != expectedBlockCountY
            || offsetBytes != expectedOffsetBytes
            || sizeBytes != expectedSizeBytes
        ){
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': mips[{}] does not match the required contiguous UASTC mip chain")
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
        outMipLevels.push_back(mip);

        expectedOffsetBytes += expectedSizeBytes;
        expectedWidth = expectedWidth > 1u ? expectedWidth >> 1u : 1u;
        expectedHeight = expectedHeight > 1u ? expectedHeight >> 1u : 1u;
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
        binaryMip.blockCountX = mip.blockCountX;
        binaryMip.blockCountY = mip.blockCountY;
        binaryMip.offsetBytes = mip.offsetBytes;
        binaryMip.sizeBytes = mip.sizeBytes;
        mipBinaries.push_back(binaryMip);
    }

    usize reserveBytes = sizeof(TextureBinaryPayload::HeaderBinary);
    if(
        !AddBinaryRepeatedReserveBytes(reserveBytes, mipBinaries.size(), sizeof(TextureBinaryPayload::MipLevelBinary))
        || !AddBinaryReserveBytes(reserveBytes, texture.uastcBlocks().size())
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("TextureAssetCodec::serialize failed: cooked payload size overflows"));
        return false;
    }

    outBinary.clear();
    outBinary.reserve(reserveBytes);

    TextureBinaryPayload::HeaderBinary header;
    header.colorSpace = static_cast<u32>(texture.colorSpace());
    header.width = texture.width();
    header.height = texture.height();
    header.mipCount = static_cast<u32>(mipBinaries.size());
    header.hasAlpha = texture.hasAlpha() ? 1u : 0u;
    header.uastcByteCount = static_cast<u64>(texture.uastcBlocks().size());
    AppendPOD(outBinary, header);
    if(!Core::Assets::AppendVectorPayload(
        outBinary,
        mipBinaries,
        NWB_TEXT("TextureAssetCodec::serialize"),
        NWB_TEXT("mip levels")
    ))
        return false;

    BinaryDetail::AppendBytesNoReserveUnchecked(outBinary, texture.uastcBlocks().data(), texture.uastcBlocks().size());
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
    if(!Core::Assets::ValidateMetadataAssetFields(
        nwbFilePath,
        asset,
        s_DiagnosticPrefix,
        {
            s_VersionField,
            s_FormatField,
            s_UastcSpecificationRevisionField,
            s_ColorSpaceField,
            s_WidthField,
            s_HeightField,
            s_BlockWidthField,
            s_BlockHeightField,
            s_BytesPerBlockField,
            s_PayloadLayoutField,
            s_MipAddressModeField,
            s_HasAlphaField,
            s_MipCountField,
            s_DataField,
            s_MipsField,
        }
    ))
        return false;

    if(!Core::Assets::BuildMetadataDerivedAssetVirtualPath(assetRoot, virtualRoot, nwbFilePath, outEntry.virtualPath, scratchArena))
        return false;

    u32 hasAlpha = 0u;
    u32 mipCount = 0u;
    if(
        !ReadExactUnsignedField(nwbFilePath, asset, s_VersionField, 1u)
        || !ReadExactStringField(nwbFilePath, asset, s_FormatField, s_UastcLdr4x4Format)
        || !ReadExactStringField(nwbFilePath, asset, s_UastcSpecificationRevisionField, s_UastcSpecificationRevision)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_WidthField, 1u, Limit<u32>::s_Max, outEntry.width)
        || !ReadRequiredUnsignedField(nwbFilePath, asset, s_HeightField, 1u, Limit<u32>::s_Max, outEntry.height)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockWidthField, s_UastcBlockWidth)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BlockHeightField, s_UastcBlockHeight)
        || !ReadExactUnsignedField(nwbFilePath, asset, s_BytesPerBlockField, s_UastcBytesPerBlock)
        || !ReadExactStringField(nwbFilePath, asset, s_PayloadLayoutField, s_MipMajorBlocksPayloadLayout)
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

    u32 expectedMipCount = 0u;
    if(!ComputeCompleteMipCount(outEntry.width, outEntry.height, expectedMipCount) || mipCount != expectedMipCount){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': field '{}' must describe a complete mip chain")
            , StringConvert(s_DiagnosticPrefix)
            , PathToString<tchar>(nwbFilePath)
            , StringConvert(s_MipCountField)
        );
        return false;
    }
    if(!ParseMipLevels(nwbFilePath, asset, outEntry.width, outEntry.height, expectedMipCount, outEntry.mipLevels))
        return false;

    AStringView dataFileName;
    if(!ReadRequiredStringField(nwbFilePath, asset, s_DataField, dataFileName))
        return false;
    if(!ValidateTextureDataFileName(nwbFilePath, dataFileName, scratchArena))
        return false;

    Path dataPath(nwbFilePath.parent_path());
    dataPath /= dataFileName;
    ErrorCode errorCode;
    if(!ReadBinaryFile(dataPath, outEntry.uastcBlocks, errorCode)){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': failed to read UASTC sidecar '{}': {}")
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
            NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': UASTC payload size overflows")
                , StringConvert(s_DiagnosticPrefix)
                , PathToString<tchar>(nwbFilePath)
            );
            return false;
        }
        expectedPayloadBytes += mip.sizeBytes;
    }
    if(expectedPayloadBytes != static_cast<u64>(outEntry.uastcBlocks.size())){
        NWB_LOGGER_ERROR(NWB_TEXT("{} '{}': UASTC sidecar size does not match the mip metadata")
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
        Move(textureEntry.uastcBlocks)
    );
    return outTexture.validatePayload();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
