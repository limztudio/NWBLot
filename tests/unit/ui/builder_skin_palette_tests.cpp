// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_builder_skin_palette_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

[[nodiscard]] static UiSkinPalette MakePalette(const f32 offset){
    UiSkinPalette palette;
    for(u32 index = 0u; index < UiSkinColorRole::Count; ++index){
        const f32 value = offset + static_cast<f32>(index) * 0.02f;
        palette.colors[index] = { value, value + 0.01f, value + 0.02f, 0.5f + static_cast<f32>(index) * 0.01f };
    }
    return palette;
}

static void ExpectColor(const Color& actual, const UiSkinColor& expected){
    EXPECT_FLOAT_EQ(actual.r, expected.r);
    EXPECT_FLOAT_EQ(actual.g, expected.g);
    EXPECT_FLOAT_EQ(actual.b, expected.b);
    EXPECT_FLOAT_EQ(actual.a, expected.a);
}

class UiBuilderSkinPaletteTests : public WidgetFixture{};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiBuilderSkinPaletteTests, SkinChangeInsidePanelFailsWithoutReplacingActiveStyle){
    m_builder.style().fontSize = WidgetStyle{}.fontSize;
    const UiSkinPalette first = MakePalette(0.1f);
    m_skin.setPalette(first);
    m_skin.setTypography({ 20.0f });
    m_builder.setSkin(m_skin);
    ASSERT_FLOAT_EQ(m_builder.style().fontSize, 20.0f);

    UiSkin replacement(m_arena, Name("tests/ui/replacement_skin"));
    UiSkin::RegionVector regions(m_skin.regions().begin(), m_skin.regions().end(), m_arena);
    replacement.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
    replacement.setPalette(MakePalette(0.3f));
    replacement.setTypography({ 22.0f });
    ASSERT_TRUE(replacement.validatePayload());

    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 200.0f, 100.0f }));
    m_builder.setSkin(replacement);
    EXPECT_TRUE(m_builder.failed());
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 20.0f);
    ExpectColor(m_builder.style().text, first.colors[UiSkinColorRole::TextNormal]);
    EXPECT_FALSE(m_builder.endPanel());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

