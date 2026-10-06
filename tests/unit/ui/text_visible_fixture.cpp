// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_visible_fixture.h"

#include <tests/common/font_fixture.h>

#include <global/filesystem.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTextVisibleTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectRectangle(const Rect& actual, const Rect& expected){
    EXPECT_FLOAT_EQ(actual.x, expected.x);
    EXPECT_FLOAT_EQ(actual.y, expected.y);
    EXPECT_FLOAT_EQ(actual.width, expected.width);
    EXPECT_FLOAT_EQ(actual.height, expected.height);
}

void ExpectSamePaint(const DrawSnapshot& actual, const DrawSnapshot& expected){
    ASSERT_EQ(actual.glyphPages().size(), expected.glyphPages().size());
    ASSERT_EQ(actual.sdfPages().size(), expected.sdfPages().size());
    ASSERT_EQ(actual.vertices().size(), expected.vertices().size());
    ASSERT_EQ(actual.commands().size(), expected.commands().size());
    EXPECT_EQ(actual.indices(), expected.indices());
    for(usize index = 0u; index < actual.glyphPages().size(); ++index){
        EXPECT_EQ(actual.glyphPages()[index].get(), expected.glyphPages()[index].get());
        EXPECT_EQ(actual.glyphPages()[index]->binding(), expected.glyphPages()[index]->binding());
        EXPECT_EQ(actual.glyphPages()[index]->pixels(), expected.glyphPages()[index]->pixels());
    }
    for(usize index = 0u; index < actual.sdfPages().size(); ++index){
        EXPECT_EQ(actual.sdfPages()[index].get(), expected.sdfPages()[index].get());
        EXPECT_EQ(actual.sdfPages()[index]->binding(), expected.sdfPages()[index]->binding());
        EXPECT_EQ(actual.sdfPages()[index]->pixels(), expected.sdfPages()[index]->pixels());
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
        ExpectRectangle(a.clip, e.clip);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextVisibleFixture::TextVisibleFixture()
    : m_arena(Name("tests/ui/text/visible"))
    , m_latin(m_arena, Name("tests/ui/fonts/latin"))
    , m_korean(m_arena, Name("tests/ui/fonts/korean"))
    , m_text(m_arena)
    , m_layout(m_arena)
    , m_skin(m_arena, Name("tests/ui/skin"))
    , m_paint(m_arena)
{}

void TextVisibleFixture::SetUp(){
    ASSERT_TRUE(loadFont(m_latin, "latin.font"));
    ASSERT_TRUE(loadFont(m_korean, "korean.font"));
    ASSERT_TRUE(installCoverageFonts());
    UiSkin::RegionVector regions(m_arena);
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/texture"), 4u, 4u, 1.0f, Move(regions));
}

bool TextVisibleFixture::loadFont(Font& font, const StringView filename){
    const ::Path<Core::Alloc::GlobalArena> path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY)
        / filename;
    Core::Assets::AssetBytes bytes(m_arena);
    if(!Tests::ReadBundledFontBytes(path, bytes))
        return false;
    font.setFontBytes(Move(bytes));
    return font.validatePayload();
}

bool TextVisibleFixture::installCoverageFonts(){
    const FontSource sources[]{
        { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_latin, 1u },
        { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), m_korean, 1u },
    };
    return m_text.setFonts(sources, 2u);
}

void TextVisibleFixture::beginPaint(const u64 generation, const DisplayMetrics& display){
    m_paint.begin(display, generation, 1u, Core::Assets::AssetRef<UiSkin>("tests/ui/skin"), m_skin);
}

SharedGlyphPage TextVisibleFixture::makePage(const u64 atlasIdentity){
    GlyphPage::Pixels pixels(m_arena);
    pixels.resize(64u, 128u);
    const GlyphPageBinding binding{
        .font = Core::Assets::AssetRef<Font>("tests/ui/independent_font"),
        .fontGeneration = 1u,
        .atlasIdentity = atlasIdentity,
        .generation = 1u,
        .index = 0u,
        .width = 8u,
        .height = 8u,
    };
    return CreateGlyphPage(m_arena, binding, Move(pixels));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

