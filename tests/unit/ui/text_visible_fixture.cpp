// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTextVisibleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Rect TextVisibleFixture::OutlineRectangle(const PlacedGlyph& glyph, const Point origin)noexcept{
    return Rect{ origin.x + glyph.position.x + glyph.ink.x, origin.y + glyph.position.y + glyph.ink.y, glyph.ink.width, glyph.ink.height };
}

TextVisibleFixture::TextVisibleFixture()
    : m_arena(Name("tests/ui/text/visible"))
    , m_latin(m_arena, Name(s_TestLatinFontPath))
    , m_korean(m_arena, Name(s_TestKoreanFontPath))
    , m_text(m_arena)
    , m_layout(m_arena)
    , m_skin(m_arena, Name(s_TestSkinPath))
    , m_paint(m_arena)
{}

void TextVisibleFixture::SetUp(){
    ASSERT_TRUE(loadFont("latin.font", m_latin));
    ASSERT_TRUE(loadFont("korean.font", m_korean));
    ASSERT_TRUE(installCoverageFonts());
    UiSkin::RegionVector regions(m_arena);
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.0f, Move(regions));
}

b8 TextVisibleFixture::loadFont(const StringView filename, Font& font){
    return UiWidgetTests::LoadBundledFixtureFont(m_arena, filename, font);
}

b8 TextVisibleFixture::installCoverageFonts(){
    Core::Alloc::ScratchArena fontScratchArena(UiWidgetTests::s_FontScratchArenaName);
    const Array<FontSource, 2u> sources = {
        FontSource{ Core::Assets::AssetRef<Font>(s_TestLatinFontPath), m_latin, 1u },
        FontSource{ Core::Assets::AssetRef<Font>(s_TestKoreanFontPath), m_korean, 1u },
    };
    return m_text.setFonts(fontScratchArena, sources);
}

void TextVisibleFixture::beginPaint(const u64 generation, const DisplayMetrics& display){
    m_paint.begin(display, generation, 1u, Core::Assets::AssetRef<UiSkin>(s_TestSkinPath), m_skin);
}

Expected<SharedGlyphPage> TextVisibleFixture::makePage(const u64 atlasIdentity){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(64u, 128u);
    const GlyphPageBinding binding{ .font = Core::Assets::AssetRef<Font>("tests/ui/independent_font"), .fontGeneration = 1u, .atlasIdentity = atlasIdentity, .generation = 1u, .index = 0u, .width = 8u, .height = 8u, };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

