// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/text/service.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/font_fixture.h>

#include <global/filesystem.h>

#include <hb.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_baked_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;

class TextBakedTests : public testing::Test{
public:
    TextBakedTests()
        : m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::BreakOnFatal)
        , m_arena(Name("tests/ui/text/baked"))
        , m_font(m_arena, Name("tests/ui/fonts/latin"))
        , m_atlas(m_arena, Name("tests/ui/fonts/latin_atlas"))
        , m_text(m_arena)
        , m_layout(m_arena)
        , m_skin(m_arena, Name("tests/ui/skin"))
        , m_paint(m_arena)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(loadFont(m_font, "latin.font"));
        const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u };
        ASSERT_TRUE(m_text.setFonts(&source, 1u));
        ASSERT_EQ(m_text.layout({ .text = "A ", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
        FontAtlasPayload payload(m_arena);
        payload.font = source.identity;
        payload.fontSha256 = ComputeSha256({ m_font.fontBytes().data(), m_font.fontBytes().size() });
        const Core::Assets::AssetBytes& bytes = m_font.fontBytes();
        hb_blob_t* blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()), static_cast<u32>(bytes.size()),
            HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        hb_face_t* face = hb_face_create(blob, 0u);
        payload.unitsPerEm = hb_face_get_upem(face);
        payload.sourceGlyphCount = hb_face_get_glyph_count(face);
        const u32 tags[]{ s_FontAtlasGdefTag, s_FontAtlasGposTag, s_FontAtlasKernTag };
        for(const u32 tag : tags){
            hb_blob_t* tableBlob = hb_face_reference_table(face, tag);
            u32 size = 0u;
            const char* data = hb_blob_get_data(tableBlob, &size);
            if(size != 0u){
                payload.positioningTables.emplace_back(m_arena);
                FontAtlasPositioningTable& table = payload.positioningTables.back();
                table.tag = tag;
                table.bytes.assign(reinterpret_cast<const u8*>(data), reinterpret_cast<const u8*>(data) + size);
                table.sha256 = ComputeSha256({ table.bytes.data(), table.bytes.size() });
            }
            hb_blob_destroy(tableBlob);
        }
        hb_face_destroy(face);
        hb_blob_destroy(blob);
        payload.bakePpem = 32u;
        payload.ascenderUnits = 1000.f;
        payload.descenderUnits = -250.f;
        payload.glyphs.resize(payload.sourceGlyphCount);
        for(u32 id = 0u; id < payload.sourceGlyphCount; ++id)
            payload.glyphs[id].glyphId = id;
        payload.groups.emplace_back(m_arena);
        FontAtlasGroup& group = payload.groups.back();
        group.width = 32u;
        group.height = 32u;
        group.pixels.resize(32u * 32u * 4u, 128u);
        group.sha256 = ComputeSha256({ group.pixels.data(), group.pixels.size() });
        for(const PlacedGlyph& placed : m_layout.glyphs()){
            FontAtlasGlyph& glyph = payload.glyphs[placed.glyphId];
            if(m_layout.utf8()[placed.byteBegin] == ' ' || glyph.drawable != 0u)
                continue;
            glyph.drawable = 1u;
            glyph.x = 1u;
            glyph.y = 1u;
            glyph.width = 2u;
            glyph.height = 2u;
            glyph.planeLeft = -125.f;
            glyph.planeTop = -875.f;
            glyph.planeRight = glyph.planeLeft + static_cast<f32>(glyph.width * payload.unitsPerEm) / payload.bakePpem;
            glyph.planeBottom = glyph.planeTop + static_cast<f32>(glyph.height * payload.unitsPerEm) / payload.bakePpem;
        }
        m_atlas.setPayload(Move(payload));
        ASSERT_TRUE(m_atlas.validatePayload());
        UiSkin::RegionVector regions(m_arena);
        m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.f, Move(regions));
    }

    [[nodiscard]] bool loadFont(Font& font, StringView filename){
        const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / filename;
        Core::Assets::AssetBytes bytes(m_arena);
        if(!Tests::ReadBundledFontBytes(path, bytes))
            return false;
        font.setFontBytes(Move(bytes));
        return font.validatePayload();
    }

    [[nodiscard]] bool installAtlas(){
        const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u, &m_atlas };
        return m_text.setFonts(&source, 1u);
    }

    [[nodiscard]] DrawSnapshot paint(f32 scale = 1.f){
        m_paint.begin({ 800.f, 600.f, scale, scale }, 1u, 1u,
            Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
        EXPECT_TRUE(m_text.paint(m_paint, m_layout, { 20.f, 20.f }));
        return m_paint.freeze();
    }


protected:
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
    Core::Alloc::GlobalArena m_arena;
    Font m_font;
    FontAtlas m_atlas;
    TextService m_text;
    TextLayout m_layout;
    UiSkin m_skin;
    PaintBuilder m_paint;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(TextBakedTests, FontHashMismatchUsesCoverageForTheAlreadySelectedFace){
    FontAtlasPayload payload(m_arena);
    Core::Assets::AssetBytes binary(m_arena);
    ASSERT_TRUE(SerializeFontAtlasPayload(m_atlas.payload(), binary));
    ASSERT_TRUE(DeserializeFontAtlasPayload(binary, payload));
    payload.fontSha256.bytes[0u] ^= 1u;
    m_atlas.setPayload(Move(payload));
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ "A" }, m_layout), TextLayoutStatus::Success);
    EXPECT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    const DrawSnapshot snapshot = paint();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_FALSE(snapshot.glyphPages().empty());
    EXPECT_EQ(m_layout.glyphs()[0u].face->identity().name(), Name("tests/ui/fonts/latin"));
}

TEST_F(TextBakedTests, LogicalFontIdentityMustMatchEvenWhenTheBytesMatch){
    FontAtlasPayload payload(m_arena);
    Core::Assets::AssetBytes binary(m_arena);
    ASSERT_TRUE(SerializeFontAtlasPayload(m_atlas.payload(), binary));
    ASSERT_TRUE(DeserializeFontAtlasPayload(binary, payload));
    payload.font = Core::Assets::AssetRef<Font>("tests/ui/fonts/other");
    m_atlas.setPayload(Move(payload));
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
    EXPECT_FALSE(m_layout.glyphs()[0u].face->bakedAtlas());
    EXPECT_TRUE(paint().sdfPages().empty());
}

TEST_F(TextBakedTests, OldLayoutAndSnapshotOwnTheirAtlasAfterFontReplacementAndAssetRelease){
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
    const SharedBakedFontAtlas retained = m_layout.glyphs()[0u].face->bakedAtlas();
    const DrawSnapshot first = paint();
    ASSERT_EQ(first.sdfPages().size(), 1u);
    const FontSource source{ Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 2u };
    ASSERT_TRUE(m_text.setFonts(&source, 1u));
    m_atlas.setPayload(FontAtlasPayload(m_arena));
    const DrawSnapshot second = paint();
    ASSERT_EQ(second.sdfPages().size(), 1u);
    EXPECT_EQ(first.sdfPages()[0u].get(), second.sdfPages()[0u].get());
    EXPECT_EQ(second.sdfPages()[0u]->binding().fontGeneration, 1u);
    EXPECT_EQ(retained.get(), m_layout.glyphs()[0u].face->bakedAtlas().get());
    EXPECT_EQ(first.sdfPages()[0u]->pixels()[0u], 128u);
}

TEST_F(TextBakedTests, OutsideBakedSizeRangeUsesCoverageWithLowerThresholdControl){
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
    const DrawSnapshot first = paint();
    ASSERT_EQ(first.sdfPages().size(), 1u);
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 8.f }, m_layout), TextLayoutStatus::Success);
    const DrawSnapshot small = paint();
    EXPECT_TRUE(small.sdfPages().empty());
    EXPECT_FALSE(small.glyphPages().empty());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 129.f }, m_layout), TextLayoutStatus::Success);
    EXPECT_TRUE(paint().sdfPages().empty());
}

TEST_F(TextBakedTests, TextBelowThreeQuarterBakeSizeUsesCoverageAtNormalDpi){
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 22.f }, m_layout), TextLayoutStatus::Success);
    ASSERT_TRUE(m_layout.glyphs()[0u].face->bakedAtlas());
    const DrawSnapshot small = paint();
    EXPECT_TRUE(small.sdfPages().empty());
    ASSERT_EQ(small.glyphPages().size(), 1u);
    ASSERT_EQ(small.commands().size(), 1u);
    EXPECT_EQ(small.commands()[0u].material, PaintMaterial::Glyph);

    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
    const DrawSnapshot threshold = paint();
    EXPECT_TRUE(threshold.glyphPages().empty());
    ASSERT_EQ(threshold.sdfPages().size(), 1u);
    ASSERT_EQ(threshold.commands().size(), 1u);
    EXPECT_EQ(threshold.commands()[0u].material, PaintMaterial::SdfGlyph);
}

TEST_F(TextBakedTests, FractionalPhysicalSizeBelowSdfThresholdUsesCoverage){
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 23.5f }, m_layout), TextLayoutStatus::Success);
    const DrawSnapshot fractionalFont = paint();
    EXPECT_TRUE(fractionalFont.sdfPages().empty());
    ASSERT_EQ(fractionalFont.glyphPages().size(), 1u);
    ASSERT_EQ(fractionalFont.commands().size(), 1u);
    EXPECT_EQ(fractionalFont.commands()[0u].material, PaintMaterial::Glyph);

    ASSERT_EQ(m_text.layout({ .text = "A", .fontSize = 16.0f }, m_layout), TextLayoutStatus::Success);
    const DrawSnapshot fractionalDpi = paint(1.49f);
    EXPECT_TRUE(fractionalDpi.sdfPages().empty());
    ASSERT_EQ(fractionalDpi.glyphPages().size(), 1u);
    ASSERT_EQ(fractionalDpi.commands().size(), 1u);
    EXPECT_EQ(fractionalDpi.commands()[0u].material, PaintMaterial::Glyph);

    const DrawSnapshot thresholdDpi = paint(1.5f);
    EXPECT_TRUE(thresholdDpi.glyphPages().empty());
    ASSERT_EQ(thresholdDpi.sdfPages().size(), 1u);
    ASSERT_EQ(thresholdDpi.commands().size(), 1u);
    EXPECT_EQ(thresholdDpi.commands()[0u].material, PaintMaterial::SdfGlyph);
}

TEST_F(TextBakedTests, WhitespaceKeepsItsAdvanceWithoutAdmittingAnImage){
    ASSERT_TRUE(installAtlas());
    ASSERT_EQ(m_text.layout({ .text = "   ", .fontSize = 24.f }, m_layout), TextLayoutStatus::Success);
    EXPECT_GT(m_layout.measure().x, 0.f);
    const DrawSnapshot snapshot = paint();
    EXPECT_TRUE(snapshot.sdfPages().empty());
    EXPECT_TRUE(snapshot.glyphPages().empty());
    EXPECT_TRUE(snapshot.vertices().empty());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

