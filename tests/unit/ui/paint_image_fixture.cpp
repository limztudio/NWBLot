// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "paint_image_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiPaintImageTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiWidgetTests;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectSameImagePaint(const DrawSnapshot& actual, const DrawSnapshot& expected){
    EXPECT_FLOAT_EQ(actual.displayMetrics().logicalWidth, expected.displayMetrics().logicalWidth);
    EXPECT_FLOAT_EQ(actual.displayMetrics().logicalHeight, expected.displayMetrics().logicalHeight);
    EXPECT_FLOAT_EQ(actual.displayMetrics().pixelScaleX, expected.displayMetrics().pixelScaleX);
    EXPECT_FLOAT_EQ(actual.displayMetrics().pixelScaleY, expected.displayMetrics().pixelScaleY);
    EXPECT_EQ(actual.skinBinding().skin, expected.skinBinding().skin);
    EXPECT_EQ(actual.skinBinding().texture, expected.skinBinding().texture);
    EXPECT_EQ(actual.skinBinding().generation, expected.skinBinding().generation);
    EXPECT_EQ(actual.skinBinding().atlasWidth, expected.skinBinding().atlasWidth);
    EXPECT_EQ(actual.skinBinding().atlasHeight, expected.skinBinding().atlasHeight);
    EXPECT_FLOAT_EQ(actual.skinBinding().referenceDensity, expected.skinBinding().referenceDensity);
    ASSERT_EQ(actual.glyphPages().size(), expected.glyphPages().size());
    ASSERT_EQ(actual.sdfPages().size(), expected.sdfPages().size());
    ASSERT_EQ(actual.textureImages().size(), expected.textureImages().size());
    ASSERT_EQ(actual.vertices().size(), expected.vertices().size());
    ASSERT_EQ(actual.commands().size(), expected.commands().size());
    EXPECT_EQ(actual.indices(), expected.indices());
    for(usize index = 0u; index < actual.glyphPages().size(); ++index){
        ASSERT_TRUE(actual.glyphPages()[index]);
        ASSERT_TRUE(expected.glyphPages()[index]);
        EXPECT_EQ(actual.glyphPages()[index].get(), expected.glyphPages()[index].get());
        EXPECT_EQ(actual.glyphPages()[index]->binding(), expected.glyphPages()[index]->binding());
        EXPECT_EQ(actual.glyphPages()[index]->pixels(), expected.glyphPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.sdfPages().size(); ++index){
        ASSERT_TRUE(actual.sdfPages()[index]);
        ASSERT_TRUE(expected.sdfPages()[index]);
        EXPECT_EQ(actual.sdfPages()[index].get(), expected.sdfPages()[index].get());
        EXPECT_EQ(actual.sdfPages()[index]->binding(), expected.sdfPages()[index]->binding());
        EXPECT_EQ(actual.sdfPages()[index]->pixels(), expected.sdfPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.textureImages().size(); ++index){
        ASSERT_TRUE(actual.textureImages()[index]);
        ASSERT_TRUE(expected.textureImages()[index]);
        const ImageSource& a = *actual.textureImages()[index];
        const ImageSource& e = *expected.textureImages()[index];
        EXPECT_EQ(actual.textureImages()[index].get(), expected.textureImages()[index].get());
        EXPECT_EQ(a.identity(), e.identity());
        EXPECT_EQ(a.generation(), e.generation());
        EXPECT_EQ(a.texture().virtualPath(), e.texture().virtualPath());
        EXPECT_EQ(a.texture().width(), e.texture().width());
        EXPECT_EQ(a.texture().height(), e.texture().height());
        EXPECT_EQ(a.texture().dimension(), e.texture().dimension());
        EXPECT_EQ(a.texture().depth(), e.texture().depth());
        EXPECT_EQ(a.texture().colorSpace(), e.texture().colorSpace());
        EXPECT_EQ(a.texture().payloadFormat(), e.texture().payloadFormat());
        EXPECT_EQ(a.texture().hasAlpha(), e.texture().hasAlpha());
        EXPECT_EQ(a.texture().alphaMode(), e.texture().alphaMode());
        EXPECT_EQ(a.texture().alphaConstantUnorm8(), e.texture().alphaConstantUnorm8());
        EXPECT_EQ(a.texture().payloadBytes(), e.texture().payloadBytes());
        ASSERT_EQ(a.texture().mipLevels().size(), e.texture().mipLevels().size());
        for(usize mipIndex = 0u; mipIndex < a.texture().mipLevels().size(); ++mipIndex){
            const TextureMipLevel& am = a.texture().mipLevels()[mipIndex];
            const TextureMipLevel& em = e.texture().mipLevels()[mipIndex];
            EXPECT_EQ(am.width, em.width);
            EXPECT_EQ(am.height, em.height);
            EXPECT_EQ(am.blockCountX, em.blockCountX);
            EXPECT_EQ(am.blockCountY, em.blockCountY);
            EXPECT_EQ(am.offsetBytes, em.offsetBytes);
            EXPECT_EQ(am.sizeBytes, em.sizeBytes);
            EXPECT_EQ(am.sliceCount, em.sliceCount);
        }
    }
    for(usize index = 0u; index < actual.vertices().size(); ++index){
        const Vertex& a = actual.vertices()[index];
        const Vertex& e = expected.vertices()[index];
        EXPECT_FLOAT_EQ(a.position.x, e.position.x);
        EXPECT_FLOAT_EQ(a.position.y, e.position.y);
        EXPECT_FLOAT_EQ(a.texCoord.x, e.texCoord.x);
        EXPECT_FLOAT_EQ(a.texCoord.y, e.texCoord.y);
        EXPECT_FLOAT_EQ(a.color.r, e.color.r);
        EXPECT_FLOAT_EQ(a.color.g, e.color.g);
        EXPECT_FLOAT_EQ(a.color.b, e.color.b);
        EXPECT_FLOAT_EQ(a.color.a, e.color.a);
    }
    for(usize index = 0u; index < actual.commands().size(); ++index){
        const DrawCommand& a = actual.commands()[index];
        const DrawCommand& e = expected.commands()[index];
        EXPECT_EQ(a.firstIndex, e.firstIndex);
        EXPECT_EQ(a.indexCount, e.indexCount);
        EXPECT_EQ(a.material, e.material);
        EXPECT_EQ(a.glyphPageIndex, e.glyphPageIndex);
        EXPECT_EQ(a.sdfPageIndex, e.sdfPageIndex);
        EXPECT_EQ(a.sdfChannel, e.sdfChannel);
        EXPECT_EQ(a.layer, e.layer);
        EXPECT_EQ(a.textureImageIndex, e.textureImageIndex);
        ExpectRect(a.clip, e.clip);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


PaintImageFixture::PaintImageFixture()
    : m_arena(Name("tests/ui/paint_image"))
    , m_skin(m_arena, Name("tests/ui/skin"))
    , m_builder(m_arena)
{
    UiSkin::RegionVector regions(m_arena);
    regions.push_back({ Name("sprite"), { 0u, 0u, 8u, 8u }, {}, {}, 0.0f, 0.0f, UiSkinDrawMode::Sprite });
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/skin_texture"), 16u, 16u, 1.0f, Move(regions));
    beginPaint(m_builder);
}

void PaintImageFixture::beginPaint(PaintBuilder& builder, const u64 generation, const DisplayMetrics& display){
    builder.begin(display, generation, 3u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
    builder.reserve(16u);
}

SharedGlyphPage PaintImageFixture::makeGlyph(const u64 atlasIdentity, const u64 generation, const u32 index){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(64u, 0u);
    pixels[0u] = 64u;
    if(generation > 1u)
        pixels[32u] = 192u;
    const GlyphPageBinding binding{
        .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
        .fontGeneration = 9u,
        .atlasIdentity = atlasIdentity,
        .generation = generation,
        .index = index,
        .width = 8u,
        .height = 8u,
    };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}

SharedSdfAtlasPage PaintImageFixture::makeSdf(const u64 atlasIdentity, const u64 generation, const u32 index, const u8 pixel){
    SdfAtlasPage::Pixels pixels(m_arena);
    pixels.resize(8u * 8u * 4u, 0u);
    pixels[0u] = 32u;
    pixels[1u] = 128u;
    pixels[2u] = pixel;
    pixels[3u] = 192u;
    const SdfAtlasPageBinding binding{
        .font = Core::Assets::AssetRef<Font>("tests/ui/font"),
        .fontSha256 = {},
        .pixelsSha256 = {},
        .fontGeneration = 9u,
        .atlasIdentity = atlasIdentity,
        .generation = generation,
        .index = index,
        .width = 8u,
        .height = 8u,
        .spreadPixels = 8u,
    };
    return CreateSdfAtlasPage(m_arena, binding, Move(pixels));
}

SharedImageSource PaintImageFixture::makeImage(const StringView path, const u8 seed, const u32 width, const u32 height){
    if(path.empty() || width == 0u || height == 0u)
        return {};
    u32 mipCount = 0u;
    if(!TextureFormat::ComputeCompleteMipCount(TextureDimension::Texture2D, width, height, 1u, mipCount))
        return {};
    Texture::MipLevelVector mips(m_arena);
    mips.reserve(mipCount);
    u32 mipWidth = width;
    u32 mipHeight = height;
    u64 offset = 0u;
    for(u32 index = 0u; index < mipCount; ++index){
        u32 blocksX = 0u;
        u32 blocksY = 0u;
        u64 byteCount = 0u;
        if(!TextureFormat::ComputeMipPlaneBlockLayout(
            TexturePayloadFormat::UastcLdr4x4, mipWidth, mipHeight, blocksX, blocksY, byteCount
        ))
            return {};
        mips.push_back({ mipWidth, mipHeight, blocksX, blocksY, offset, byteCount, 1u });
        offset += byteCount;
        mipWidth = mipWidth > 1u ? mipWidth >> 1u : 1u;
        mipHeight = mipHeight > 1u ? mipHeight >> 1u : 1u;
    }
    if(offset > Limit<usize>::s_Max)
        return {};
    Core::Assets::AssetBytes bytes(m_arena);
    bytes.resize(static_cast<usize>(offset));
    for(usize index = 0u; index < bytes.size(); ++index)
        bytes[index] = static_cast<u8>(static_cast<u32>(seed) + index * 17u);
    Texture texture(m_arena, Name(path));
    texture.setPayload(
        TextureColorSpace::Srgb, true, width, height, Move(mips), Move(bytes),
        TextureDimension::Texture2D, 1u, TexturePayloadFormat::UastcLdr4x4,
        TextureAlphaMode::EmbeddedLdr, TextureFormat::s_OpaqueAlphaUnorm8
    );
    return MakeImageSource(m_arena, texture);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

