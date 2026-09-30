// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/images/image_loader.h>
#include <impl/assets_texture/binary_payload.h>
#include <impl/assets_ui_skin/asset.h>

#include <tests/common/capturing_logger.h>

#include <global/binary.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_loader_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core::Assets;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static AssetBytes MakeTextureBinary(AssetArena& arena, const u8 initialValue){
    TextureBinaryPayload::HeaderBinary header;
    header.colorSpace = static_cast<u32>(TextureColorSpace::Srgb);
    header.dimension = static_cast<u32>(TextureDimension::Texture2D);
    header.width = 2u;
    header.height = 1u;
    header.depth = 1u;
    header.mipCount = 2u;
    header.alphaInfo = static_cast<u32>(TextureAlphaMode::EmbeddedLdr)
        | (255u << TextureBinaryPayload::s_AlphaInfoConstantShift)
    ;
    header.payloadFormat = static_cast<u32>(TexturePayloadFormat::UastcLdr4x4);
    header.payloadByteCount = 32u;

    TextureBinaryPayload::MipLevelBinary mip;
    mip.width = 2u;
    mip.height = 1u;
    mip.sliceCount = 1u;
    mip.blockCountX = 1u;
    mip.blockCountY = 1u;
    mip.sizeBytes = 16u;

    AssetBytes binary(arena);
    binary.reserve(sizeof(header) + 2u * sizeof(mip) + 32u);
    AppendPOD(binary, header);
    AppendPOD(binary, mip);
    mip.width = 1u;
    mip.offsetBytes = 16u;
    AppendPOD(binary, mip);
    for(usize index = 0u; index < 32u; ++index)
        binary.push_back(static_cast<u8>(initialValue + index));
    return binary;
}

static void ExpectPayload(const Texture& texture, const u8 initialValue){
    ASSERT_EQ(texture.payloadBytes().size(), 32u);
    for(usize index = 0u; index < texture.payloadBytes().size(); ++index)
        EXPECT_EQ(texture.payloadBytes()[index], static_cast<u8>(initialValue + index));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class MemoryImageBinarySource final : public IAssetBinarySource{
public:
    explicit MemoryImageBinarySource(AssetArena& arena)
        : m_binary(arena)
    {}


public:
    virtual bool readAssetBinary(const Name& virtualPath, AssetBytes& outBinary)const override{
        ++m_readCount;
        m_lastReadPath = virtualPath;
        outBinary.clear();
        if(!m_available || virtualPath != m_path)
            return false;
        outBinary.assign(m_binary.begin(), m_binary.end());
        return true;
    }


public:
    AssetBytes m_binary;
    Name m_path = Name("tests/ui/image_loader/texture");
    mutable Name m_lastReadPath = NAME_NONE;
    mutable usize m_readCount = 0u;
    bool m_available = true;
};

// An application codec can violate its declared type or return a different path; the loader must fence both.
class MisdirectedImageCodec final : public IAssetCodec{
public:
    explicit MisdirectedImageCodec(const bool wrongType)
        : IAssetCodec(Texture::AssetTypeName())
        , m_wrongType(wrongType)
    {}


public:
    virtual bool deserialize(
        AssetArena& arena,
        const Name& virtualPath,
        const AssetBytes& binary,
        UniquePtr<IAsset>& outAsset)const override{
        if(m_wrongType){
            outAsset = MakeUnique<UiSkin>(arena, virtualPath);
            return true;
        }
        TextureAssetCodec codec;
        return codec.deserialize(arena, Name("tests/ui/image_loader/other_texture"), binary, outAsset);
    }


private:
    bool m_wrongType = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ImageLoaderTests : public testing::Test{
public:
    ImageLoaderTests()
        : m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::BreakOnFatal)
        , m_arena(Name("tests/ui/image_loader"))
        , m_registry(m_arena)
        , m_source(m_arena)
        , m_assets(m_arena, m_registry, m_source)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(m_registry.registerCodec(MakeUnique<TextureAssetCodec>()));
        m_source.m_binary = MakeTextureBinary(m_arena, 3u);
    }


protected:
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
    AssetArena m_arena;
    AssetRegistry m_registry;
    MemoryImageBinarySource m_source;
    AssetManager m_assets;
    const AssetRef<Texture> m_identity{ "tests/ui/image_loader/texture" };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ImageLoaderTests, RealCodecLoadRetainsExactTextureAfterTemporaryAssetAndBinaryAreGone){
    const SharedImageSource image = LoadImageSource(m_arena, m_assets, m_identity);
    ASSERT_TRUE(image);
    EXPECT_EQ(m_source.m_readCount, 1u);
    EXPECT_EQ(m_source.m_lastReadPath, m_identity.name());
    EXPECT_EQ(image->identity(), m_identity);
    EXPECT_NE(image->generation(), 0u);

    const Texture& texture = image->texture();
    EXPECT_EQ(texture.virtualPath(), m_identity.name());
    EXPECT_EQ(texture.width(), 2u);
    EXPECT_EQ(texture.height(), 1u);
    EXPECT_EQ(texture.dimension(), TextureDimension::Texture2D);
    EXPECT_EQ(texture.depth(), 1u);
    EXPECT_EQ(texture.colorSpace(), TextureColorSpace::Srgb);
    EXPECT_EQ(texture.payloadFormat(), TexturePayloadFormat::UastcLdr4x4);
    EXPECT_EQ(texture.alphaMode(), TextureAlphaMode::EmbeddedLdr);
    EXPECT_EQ(texture.alphaConstantUnorm8(), 255u);
    EXPECT_TRUE(texture.hasAlpha());
    ASSERT_EQ(texture.mipLevels().size(), 2u);
    const TextureMipLevel& first = texture.mipLevels()[0u];
    EXPECT_EQ(first.width, 2u);
    EXPECT_EQ(first.height, 1u);
    EXPECT_EQ(first.sliceCount, 1u);
    EXPECT_EQ(first.blockCountX, 1u);
    EXPECT_EQ(first.blockCountY, 1u);
    EXPECT_EQ(first.offsetBytes, 0u);
    EXPECT_EQ(first.sizeBytes, 16u);
    const TextureMipLevel& last = texture.mipLevels()[1u];
    EXPECT_EQ(last.width, 1u);
    EXPECT_EQ(last.height, 1u);
    EXPECT_EQ(last.sliceCount, 1u);
    EXPECT_EQ(last.blockCountX, 1u);
    EXPECT_EQ(last.blockCountY, 1u);
    EXPECT_EQ(last.offsetBytes, 16u);
    EXPECT_EQ(last.sizeBytes, 16u);
    EXPECT_EQ(texture.primaryPayloadByteCount(), 32u);
    EXPECT_EQ(texture.alphaUastcBlocks(), nullptr);

    const usize payloadOffset = sizeof(TextureBinaryPayload::HeaderBinary) + 2u * sizeof(TextureBinaryPayload::MipLevelBinary);
    EXPECT_NE(texture.payloadBytes().data(), m_source.m_binary.data() + payloadOffset);
    m_source.m_binary.assign(m_source.m_binary.size(), 0u);
    m_source.m_binary.clear();
    ExpectPayload(texture, 3u);
    EXPECT_TRUE(texture.validatePayload());
    EXPECT_EQ(m_logger.errorCount(), 0u);
}

TEST_F(ImageLoaderTests, EmptyReferenceFailsBeforeReadingTheBinarySource){
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, AssetRef<Texture>{}));
    EXPECT_EQ(m_source.m_readCount, 0u);
    EXPECT_EQ(m_source.m_lastReadPath, NAME_NONE);
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("texture asset reference is empty")));
}

TEST_F(ImageLoaderTests, MissingPathAndFailedBinaryReadReturnNoSource){
    const AssetRef<Texture> missing{ "tests/ui/image_loader/missing" };
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, missing));
    EXPECT_EQ(m_source.m_readCount, 1u);
    EXPECT_EQ(m_source.m_lastReadPath, missing.name());
    m_source.m_available = false;
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    EXPECT_EQ(m_source.m_readCount, 2u);
    EXPECT_EQ(m_source.m_lastReadPath, m_identity.name());
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("failed to read binary")));
}

TEST_F(ImageLoaderTests, MissingTextureCodecFailsAfterReadingAValidBinary){
    ASSERT_TRUE(m_registry.unregisterCodec(Texture::AssetTypeName()));
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    EXPECT_EQ(m_source.m_readCount, 1u);
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("failed to deserialize")));
}

TEST_F(ImageLoaderTests, RealTextureCodecRejectsWrongMagicTruncationAndTrailingBytes){
    m_source.m_binary[0u] ^= 0xFFu;
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    m_source.m_binary = MakeTextureBinary(m_arena, 3u);
    m_source.m_binary.pop_back();
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    m_source.m_binary = MakeTextureBinary(m_arena, 3u);
    m_source.m_binary.push_back(0u);
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    EXPECT_EQ(m_source.m_readCount, 3u);
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("invalid texture asset format")));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("texture payload is truncated")));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("trailing bytes detected")));
}

TEST_F(ImageLoaderTests, CodecReturningARealDifferentAssetTypeIsRejectedByTheTypedManager){
    ASSERT_TRUE(m_registry.registerCodec(MakeUnique<MisdirectedImageCodec>(true), true));
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    EXPECT_EQ(m_source.m_readCount, 1u);
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("is not a texture")));
}

TEST_F(ImageLoaderTests, CodecReturningAValidTextureAtAnotherIdentityIsRejected){
    ASSERT_TRUE(m_registry.registerCodec(MakeUnique<MisdirectedImageCodec>(false), true));
    EXPECT_FALSE(LoadImageSource(m_arena, m_assets, m_identity));
    EXPECT_EQ(m_source.m_readCount, 1u);
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("loaded texture identity does not match")));
}

TEST_F(ImageLoaderTests, RepeatedLoadsOfIdenticalBytesOwnDistinctFreshVersions){
    const SharedImageSource first = LoadImageSource(m_arena, m_assets, m_identity);
    const SharedImageSource second = LoadImageSource(m_arena, m_assets, m_identity);
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first->identity(), second->identity());
    EXPECT_NE(first.get(), second.get());
    EXPECT_NE(first->generation(), second->generation());
    EXPECT_NE(first->texture().payloadBytes().data(), second->texture().payloadBytes().data());
    EXPECT_NE(first->texture().mipLevels().data(), second->texture().mipLevels().data());
    ExpectPayload(first->texture(), 3u);
    ExpectPayload(second->texture(), 3u);
    EXPECT_EQ(m_source.m_readCount, 2u);
    EXPECT_EQ(m_logger.errorCount(), 0u);
}

TEST_F(ImageLoaderTests, FailedReplacementPreservesRetainedSourceAndLaterSuccessPublishesAFreshVersion){
    SharedImageSource current = LoadImageSource(m_arena, m_assets, m_identity);
    ASSERT_TRUE(current);
    const SharedImageSource retained = current;
    const u64 oldGeneration = retained->generation();

    m_source.m_binary.pop_back();
    SharedImageSource replacement = LoadImageSource(m_arena, m_assets, m_identity);
    EXPECT_FALSE(replacement);
    if(replacement)
        current = Move(replacement);
    EXPECT_EQ(current.get(), retained.get());
    EXPECT_EQ(current->generation(), oldGeneration);
    ExpectPayload(current->texture(), 3u);

    m_source.m_binary = MakeTextureBinary(m_arena, 67u);
    replacement = LoadImageSource(m_arena, m_assets, m_identity);
    ASSERT_TRUE(replacement);
    current = Move(replacement);
    EXPECT_EQ(current->identity(), retained->identity());
    EXPECT_NE(current.get(), retained.get());
    EXPECT_NE(current->generation(), oldGeneration);
    EXPECT_EQ(retained->generation(), oldGeneration);
    m_source.m_binary.clear();
    ExpectPayload(current->texture(), 67u);
    ExpectPayload(retained->texture(), 3u);
    EXPECT_EQ(m_source.m_readCount, 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

