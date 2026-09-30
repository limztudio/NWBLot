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


TEST_F(UiBuilderSkinPaletteTests, SkinRebindReplacesOnlyColorsAndLegacySkinRestoresDefaults){
    const UiSkinPalette first = MakePalette(0.1f);
    const UiSkinPalette second = MakePalette(0.3f);
    m_skin.setPalette(first);
    m_builder.setSkin(m_skin);
    m_builder.style().fontSize = 21.0f;
    m_builder.style().text = { 0.9f, 0.8f, 0.7f, 1.0f };
    ExpectColor(m_builder.style().text, { 0.9f, 0.8f, 0.7f, 1.0f });

    m_skin.setPalette(second);
    m_builder.setSkin(m_skin);
    ExpectColor(m_builder.style().text, second.colors[UiSkinColorRole::TextNormal]);
    ExpectColor(m_builder.tooltipStyle().text, second.colors[UiSkinColorRole::TextTooltip]);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 21.0f);

    UiSkin legacy(m_arena, Name("tests/ui/legacy_skin"));
    UiSkin::RegionVector regions(m_arena);
    for(const UiSkinRegion& region : m_skin.regions())
        regions.push_back(region);
    legacy.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
    ASSERT_TRUE(legacy.validatePayload());
    m_builder.setSkin(legacy);
    const WidgetStyle widgetDefault;
    const EditBoxStyle editDefault;
    const TooltipStyle tooltipDefault;
    ExpectColor(m_builder.style().text, { widgetDefault.text.r, widgetDefault.text.g, widgetDefault.text.b, widgetDefault.text.a });
    ExpectColor(m_builder.editStyle().selection,
        { editDefault.selection.r, editDefault.selection.g, editDefault.selection.b, editDefault.selection.a });
    ExpectColor(m_builder.tooltipStyle().text,
        { tooltipDefault.text.r, tooltipDefault.text.g, tooltipDefault.text.b, tooltipDefault.text.a });
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 21.0f);
}

TEST_F(UiBuilderSkinPaletteTests, SkinTypographySetsDefaultAndPreservesExplicitStyleSize){
    m_builder.style().fontSize = WidgetStyle{}.fontSize;
    m_skin.setPalette(MakePalette(0.1f));
    m_skin.setTypography({ 20.0f });
    m_builder.setSkin(m_skin);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 20.0f);

    m_skin.setTypography({ 22.0f });
    m_builder.setSkin(m_skin);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 22.0f);

    m_builder.style().fontSize = 19.0f;
    m_skin.setTypography({ 24.0f });
    m_builder.setSkin(m_skin);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 19.0f);

    UiSkin legacy(m_arena, Name("tests/ui/legacy_skin"));
    UiSkin::RegionVector regions(m_skin.regions().begin(), m_skin.regions().end(), m_arena);
    legacy.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
    m_builder.setSkin(legacy);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 19.0f);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

