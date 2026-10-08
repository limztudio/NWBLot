// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/images/image_source.h>

#include <tests/common/capturing_logger.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_source_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

struct TextureDescription{
    u32 width = 9u;
    u32 height = 5u;
    u32 depth = 1u;
    TextureDimension::Enum dimension = TextureDimension::Texture2D;
    TexturePayloadFormat::Enum format = TexturePayloadFormat::UastcLdr4x4;
    TextureColorSpace::Enum colorSpace = TextureColorSpace::Srgb;
    TextureAlphaMode::Enum alphaMode = TextureAlphaMode::EmbeddedLdr;
    bool hasAlpha = true;
    u8 alphaConstant = TextureFormat::s_OpaqueAlphaUnorm8;
    u8 seed = 37u;
};

class UiImageSourceTests : public testing::Test{
protected:
    static void Install(
        Texture& texture, const TextureDescription& description,
        Texture::MipLevelVector&& mips, Core::Assets::AssetBytes&& bytes
    ){
        texture.setPayload(
            description.colorSpace, description.hasAlpha, description.width, description.height,
            Move(mips), Move(bytes), description.dimension, description.depth, description.format,
            description.alphaMode, description.alphaConstant
        );
    }

    [[nodiscard]] static bool Prepare(
        Core::Alloc::GlobalArena& arena, Texture& texture, const TextureDescription& description = {}
    ){
        const auto count = TextureFormat::ComputeCompleteMipCount(
            description.dimension, description.width, description.height, description.depth
        );
        if(!count)
            return false;
        Texture::MipLevelVector mips(arena);
        mips.reserve(*count);
        u32 width = description.width;
        u32 height = description.height;
        u32 depth = description.depth;
        u64 offset = 0u;
        for(u32 index = 0u; index < *count; ++index){
            const auto plane = TextureFormat::ComputeMipPlaneBlockLayout(description.format, width, height);
            if(!plane)
                return false;
            const auto slices = TextureFormat::ComputeMipSliceCount(description.dimension, depth);
            if(!slices)
                return false;
            const u64 size = plane->planeByteCount * *slices;
            mips.push_back({ width, height, plane->blocksX, plane->blocksY, offset, size, *slices });
            offset += size;
            width = width > 1u ? width >> 1u : 1u;
            height = height > 1u ? height >> 1u : 1u;
            if(description.dimension == TextureDimension::Texture3D)
                depth = depth > 1u ? depth >> 1u : 1u;
        }
        Core::Assets::AssetBytes bytes(arena);
        const u64 byteCount = description.alphaMode == TextureAlphaMode::SeparateUastcLdr4x4 ? offset * 2u : offset;
        if(byteCount > Limit<usize>::s_Max)
            return false;
        bytes.resize(static_cast<usize>(byteCount));
        for(usize index = 0u; index < bytes.size(); ++index)
            bytes[index] = static_cast<u8>(static_cast<u32>(description.seed) + index * 17u);
        Install(texture, description, Move(mips), Move(bytes));
        return true;
    }

    static void ExpectCopy(const Texture& original, const Texture& copied){
        ASSERT_EQ(copied.mipLevels().size(), original.mipLevels().size());
        ASSERT_EQ(copied.payloadBytes().size(), original.payloadBytes().size());
        EXPECT_NE(copied.mipLevels().data(), original.mipLevels().data());
        EXPECT_NE(copied.payloadBytes().data(), original.payloadBytes().data());
        EXPECT_EQ(copied.payloadBytes(), original.payloadBytes());
        EXPECT_TRUE(copied.validatePayload());
    }


protected:
    UiImageSourceTests()
        : m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::BreakOnFatal)
        , m_inputArena(Name("tests/ui/image_source/input"))
        , m_sourceArena(Name("tests/ui/image_source/owned"))
    {}


protected:
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
    Core::Alloc::GlobalArena m_inputArena;
    Core::Alloc::GlobalArena m_sourceArena;
};

static_assert(!IsDefaultConstructible_V<ImageSource>);
static_assert(!IsConstructible_V<ImageSource, Core::Alloc::GlobalArena&, const Texture&>);
static_assert(!IsConstructible_V<ImageSource, Core::Alloc::GlobalArena&, const Texture&, u64>);
static_assert(!IsConstructible_V<ImageSource, const ImageSource&>);
static_assert(!IsConstructible_V<ImageSource, ImageSource&&>);
static_assert(!IsAssignable_V<ImageSource&, ImageSource&&>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiImageSourceTests, NonPowerOfTwoMipsKeepPaddedBlockAndPayloadBounds){
    Texture texture(m_inputArena, Name("tests/ui/image/paint"));
    ASSERT_TRUE(Prepare(m_inputArena, texture));
    ASSERT_TRUE(texture.validatePayload());
    const auto source = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(source);
    const auto& mips = source->texture().mipLevels();
    ASSERT_EQ(mips.size(), 4u);
    EXPECT_EQ(mips[0u].width, 9u);
    EXPECT_EQ(mips[0u].height, 5u);
    EXPECT_EQ(mips[0u].blockCountX, 3u);
    EXPECT_EQ(mips[0u].blockCountY, 2u);
    EXPECT_EQ(mips[0u].sizeBytes, 96u);
    EXPECT_EQ(mips[1u].width, 4u);
    EXPECT_EQ(mips[1u].height, 2u);
    EXPECT_EQ(mips[1u].offsetBytes, 96u);
    EXPECT_EQ(mips[2u].offsetBytes, 112u);
    EXPECT_EQ(mips[3u].offsetBytes, 128u);
    EXPECT_EQ(source->texture().payloadBytes().size(), 144u);
}

TEST_F(UiImageSourceTests, RetainedSourceOutlivesCallerReplacementTextureAndItsArena){
    SharedImageSource source;
    {
        Core::Alloc::GlobalArena callerArena(Name("tests/ui/image_source/temporary"));
        Texture texture(callerArena, Name("tests/ui/image/temporary"));
        ASSERT_TRUE(Prepare(callerArena, texture));
        source = MakeImageSource(m_sourceArena, texture);
        ASSERT_TRUE(source);
        ExpectCopy(texture, source->texture());
        TextureDescription replacement;
        replacement.width = 1u;
        replacement.height = 1u;
        replacement.colorSpace = TextureColorSpace::Linear;
        replacement.alphaMode = TextureAlphaMode::Opaque;
        replacement.hasAlpha = false;
        replacement.seed = 91u;
        ASSERT_TRUE(Prepare(callerArena, texture, replacement));
        EXPECT_EQ(texture.payloadBytes().size(), 16u);
        EXPECT_EQ(source->texture().payloadBytes().size(), 144u);
        EXPECT_EQ(source->texture().width(), 9u);
    }
    EXPECT_EQ(source->identity().name(), Name("tests/ui/image/temporary"));
    EXPECT_EQ(source->texture().height(), 5u);
    EXPECT_EQ(source->texture().alphaMode(), TextureAlphaMode::EmbeddedLdr);
    ASSERT_EQ(source->texture().payloadBytes().size(), 144u);
    for(usize index = 0u; index < source->texture().payloadBytes().size(); ++index)
        EXPECT_EQ(source->texture().payloadBytes()[index], static_cast<u8>(37u + index * 17u));
    EXPECT_TRUE(source->texture().validatePayload());
}

TEST_F(UiImageSourceTests, IdenticalLoadsAndSamePathReplacementReceiveFreshGenerations){
    Texture texture(m_inputArena, Name("tests/ui/image/shared_path"));
    ASSERT_TRUE(Prepare(m_inputArena, texture));
    const auto original = MakeImageSource(m_sourceArena, texture);
    const auto identical = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(original);
    ASSERT_TRUE(identical);
    EXPECT_EQ(original->identity(), identical->identity());
    EXPECT_NE(original->generation(), identical->generation());
    EXPECT_NE(&original->texture(), &identical->texture());
    TextureDescription changed;
    changed.seed = 149u;
    ASSERT_TRUE(Prepare(m_inputArena, texture, changed));
    const auto replacement = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(replacement);
    EXPECT_EQ(replacement->identity(), original->identity());
    EXPECT_NE(replacement->generation(), original->generation());
    EXPECT_NE(replacement->generation(), identical->generation());
    EXPECT_EQ(replacement->texture().payloadBytes()[0u], 149u);
    EXPECT_EQ(original->texture().payloadBytes()[0u], 37u);
    EXPECT_EQ(identical->texture().payloadBytes(), original->texture().payloadBytes());
}

TEST_F(UiImageSourceTests, CopiedSharedHandleRetainsTheSameImmutableVersionAfterOriginalRelease){
    Texture texture(m_inputArena, Name("tests/ui/image/retained"));
    ASSERT_TRUE(Prepare(m_inputArena, texture));
    auto original = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(original);
    const u64 generation = original->generation();
    SharedImageSource retained = original;
    EXPECT_EQ(retained.get(), original.get());
    original.reset();
    ASSERT_TRUE(retained);
    EXPECT_EQ(retained->generation(), generation);
    EXPECT_EQ(retained->texture().payloadBytes()[143u], static_cast<u8>(37u + 143u * 17u));
    EXPECT_TRUE(retained->texture().validatePayload());
}

TEST_F(UiImageSourceTests, SeparateHdrAlphaOwnsTheCompleteTrailingPayloadStream){
    TextureDescription description;
    description.colorSpace = TextureColorSpace::Linear;
    description.format = TexturePayloadFormat::UastcHdr4x4;
    description.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
    description.hasAlpha = true;
    Texture texture(m_inputArena, Name("tests/ui/image/hdr"));
    ASSERT_TRUE(Prepare(m_inputArena, texture, description));
    ASSERT_TRUE(texture.validatePayload());
    const auto source = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(source);
    const Texture& copied = source->texture();
    EXPECT_EQ(copied.primaryPayloadByteCount(), 144u);
    ASSERT_EQ(copied.payloadBytes().size(), 288u);
    ASSERT_NE(copied.alphaUastcBlocks(), nullptr);
    EXPECT_EQ(copied.alphaUastcBlocks(), copied.payloadBytes().data() + 144u);
    EXPECT_NE(copied.alphaUastcBlocks(), texture.alphaUastcBlocks());
    EXPECT_EQ(copied.alphaUastcBlocks()[143u], static_cast<u8>(37u + 287u * 17u));
}

TEST_F(UiImageSourceTests, RejectsValidCubeAndVolumeAssetsRatherThanFlatteningTheirSlices){
    for(const auto dimension : { TextureDimension::TextureCube, TextureDimension::Texture3D }){
        TextureDescription description;
        description.dimension = dimension;
        description.height = dimension == TextureDimension::TextureCube ? 9u : 5u;
        description.depth = dimension == TextureDimension::Texture3D ? 4u : 1u;
        Texture texture(m_inputArena, Name("tests/ui/image/spatial"));
        ASSERT_TRUE(Prepare(m_inputArena, texture, description));
        ASSERT_TRUE(texture.validatePayload());
        EXPECT_FALSE(MakeImageSource(m_sourceArena, texture));
    }
}

TEST_F(UiImageSourceTests, RejectsMissingAssetIdentityAndEmptyTexturePayload){
    Texture unnamed(m_inputArena);
    ASSERT_TRUE(Prepare(m_inputArena, unnamed));
    EXPECT_FALSE(MakeImageSource(m_sourceArena, unnamed));
    Texture empty(m_inputArena, Name("tests/ui/image/empty"));
    EXPECT_FALSE(MakeImageSource(m_sourceArena, empty));
    Texture texture(m_inputArena, Name("tests/ui/image/no_bytes"));
    ASSERT_TRUE(Prepare(m_inputArena, texture));
    Texture::MipLevelVector mips(m_inputArena);
    mips.assign(texture.mipLevels().begin(), texture.mipLevels().end());
    Core::Assets::AssetBytes bytes(m_inputArena);
    Install(texture, {}, Move(mips), Move(bytes));
    EXPECT_FALSE(MakeImageSource(m_sourceArena, texture));
}

TEST_F(UiImageSourceTests, RejectsInvalidShapeAndUnknownMetadataWithoutMintingUsableVersions){
    for(u32 variant = 0u; variant < 8u; ++variant){
        Texture texture(m_inputArena, Name("tests/ui/image/invalid_metadata"));
        ASSERT_TRUE(Prepare(m_inputArena, texture));
        TextureDescription description;
        switch(variant){
        case 0u: description.width = 0u; break;
        case 1u: description.height = 0u; break;
        case 2u: description.depth = 0u; break;
        case 3u: description.depth = 2u; break;
        case 4u: description.dimension = static_cast<TextureDimension::Enum>(255u); break;
        case 5u: description.colorSpace = static_cast<TextureColorSpace::Enum>(255u); break;
        case 6u: description.format = static_cast<TexturePayloadFormat::Enum>(255u); break;
        case 7u: description.alphaMode = static_cast<TextureAlphaMode::Enum>(255u); break;
        }
        Texture::MipLevelVector mips(m_inputArena);
        mips.assign(texture.mipLevels().begin(), texture.mipLevels().end());
        Core::Assets::AssetBytes bytes(m_inputArena);
        bytes.assign(texture.payloadBytes().begin(), texture.payloadBytes().end());
        Install(texture, description, Move(mips), Move(bytes));
        EXPECT_FALSE(MakeImageSource(m_sourceArena, texture)) << variant;
    }
}

TEST_F(UiImageSourceTests, RejectsIncompleteDiscontiguousOrMalformedMipMetadata){
    for(u32 variant = 0u; variant < 7u; ++variant){
        Texture texture(m_inputArena, Name("tests/ui/image/invalid_mips"));
        ASSERT_TRUE(Prepare(m_inputArena, texture));
        Texture::MipLevelVector mips(m_inputArena);
        mips.assign(texture.mipLevels().begin(), texture.mipLevels().end());
        Core::Assets::AssetBytes bytes(m_inputArena);
        bytes.assign(texture.payloadBytes().begin(), texture.payloadBytes().end());
        switch(variant){
        case 0u: mips.pop_back(); break;
        case 1u: mips[1u].width = 3u; break;
        case 2u: mips[1u].offsetBytes += 16u; break;
        case 3u: mips[0u].sizeBytes -= 16u; break;
        case 4u: ++mips[0u].blockCountX; break;
        case 5u: mips[0u].sliceCount = 2u; break;
        case 6u: mips.push_back(mips.back()); break;
        }
        Install(texture, {}, Move(mips), Move(bytes));
        EXPECT_FALSE(MakeImageSource(m_sourceArena, texture)) << variant;
    }
}

TEST_F(UiImageSourceTests, RejectsPayloadLengthAndAlphaTransportMismatches){
    for(u32 variant = 0u; variant < 8u; ++variant){
        Texture texture(m_inputArena, Name("tests/ui/image/invalid_transport"));
        ASSERT_TRUE(Prepare(m_inputArena, texture));
        TextureDescription description;
        Texture::MipLevelVector mips(m_inputArena);
        mips.assign(texture.mipLevels().begin(), texture.mipLevels().end());
        Core::Assets::AssetBytes bytes(m_inputArena);
        bytes.assign(texture.payloadBytes().begin(), texture.payloadBytes().end());
        switch(variant){
        case 0u: bytes.pop_back(); break;
        case 1u: bytes.push_back(0u); break;
        case 2u:
            description.format = TexturePayloadFormat::UastcHdr4x4;
            description.colorSpace = TextureColorSpace::Linear;
            description.alphaMode = TextureAlphaMode::SeparateUastcLdr4x4;
            break;
        case 3u: description.format = TexturePayloadFormat::UastcHdr4x4; break;
        case 4u:
            description.alphaMode = TextureAlphaMode::ConstantUnorm8;
            description.alphaConstant = 77u;
            break;
        case 5u: description.hasAlpha = false; break;
        case 6u:
            description.format = TexturePayloadFormat::UastcHdr4x4;
            description.colorSpace = TextureColorSpace::Linear;
            break;
        case 7u: description.alphaConstant = 77u; break;
        }
        Install(texture, description, Move(mips), Move(bytes));
        EXPECT_FALSE(MakeImageSource(m_sourceArena, texture)) << variant;
    }
}

TEST_F(UiImageSourceTests, RejectedSamePathReplacementPreservesPreviouslyAcceptedOwnedVersion){
    Texture texture(m_inputArena, Name("tests/ui/image/replace"));
    ASSERT_TRUE(Prepare(m_inputArena, texture));
    const auto accepted = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(accepted);
    const u64 generation = accepted->generation();
    Texture::MipLevelVector mips(m_inputArena);
    mips.assign(texture.mipLevels().begin(), texture.mipLevels().end());
    Core::Assets::AssetBytes bytes(m_inputArena);
    bytes.assign(texture.payloadBytes().begin(), texture.payloadBytes().end());
    bytes.pop_back();
    Install(texture, {}, Move(mips), Move(bytes));
    EXPECT_FALSE(MakeImageSource(m_sourceArena, texture));
    EXPECT_EQ(accepted->generation(), generation);
    EXPECT_EQ(accepted->identity().name(), texture.virtualPath());
    ASSERT_EQ(accepted->texture().payloadBytes().size(), 144u);
    EXPECT_EQ(accepted->texture().payloadBytes()[143u], static_cast<u8>(37u + 143u * 17u));
    TextureDescription replacement;
    replacement.seed = 73u;
    ASSERT_TRUE(Prepare(m_inputArena, texture, replacement));
    const auto next = MakeImageSource(m_sourceArena, texture);
    ASSERT_TRUE(next);
    EXPECT_NE(next->generation(), generation);
    EXPECT_EQ(next->texture().payloadBytes()[0u], 73u);
    EXPECT_EQ(accepted->texture().payloadBytes()[0u], 37u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

