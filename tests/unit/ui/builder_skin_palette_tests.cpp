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


TEST_F(UiBuilderSkinPaletteTests, EveryPaletteRoleReachesTheShippedWidgetStyles){
    const UiSkinPalette palette = MakePalette(0.1f);
    m_skin.setPalette(palette);
    ASSERT_TRUE(m_skin.validatePayload());
    m_builder.style().fontSize = 19.0f;
    m_builder.style().gap = 13.0f;
    m_builder.style().button = Name("custom.button");
    m_builder.setSkin(m_skin);
    const auto& colors = palette.colors;

    ExpectColor(m_builder.style().text, colors[UiSkinColorRole::TextNormal]);
    ExpectColor(m_builder.style().disabledText, colors[UiSkinColorRole::TextDisabled]);
    ExpectColor(m_builder.tooltipStyle().text, colors[UiSkinColorRole::TextTooltip]);
    ExpectColor(m_builder.editStyle().background, colors[UiSkinColorRole::EditBackground]);
    ExpectColor(m_builder.editStyle().text, colors[UiSkinColorRole::TextNormal]);
    ExpectColor(m_builder.editStyle().disabledText, colors[UiSkinColorRole::TextDisabled]);
    ExpectColor(m_builder.editStyle().selection, colors[UiSkinColorRole::EditSelection]);
    ExpectColor(m_builder.editStyle().inactiveSelection, colors[UiSkinColorRole::EditInactiveSelection]);
    ExpectColor(m_builder.editStyle().caret, colors[UiSkinColorRole::EditCaret]);
    ExpectColor(m_builder.editStyle().preedit, colors[UiSkinColorRole::EditPreedit]);
    ExpectColor(m_builder.scrollbarStyle().trackColor, colors[UiSkinColorRole::ScrollbarTrack]);
    ExpectColor(m_builder.scrollbarStyle().thumbColor, colors[UiSkinColorRole::ScrollbarThumb]);
    ExpectColor(m_builder.scrollbarStyle().disabledColor, colors[UiSkinColorRole::ScrollbarDisabled]);
    ExpectColor(m_builder.popupStyle().backdrop, colors[UiSkinColorRole::PopupBackdrop]);
    ExpectColor(m_builder.radioGroupStyle().hoverTint, colors[UiSkinColorRole::ControlHoverTint]);
    ExpectColor(m_builder.radioGroupStyle().pressedTint, colors[UiSkinColorRole::ControlPressedTint]);
    ExpectColor(m_builder.radioGroupStyle().disabledTint, colors[UiSkinColorRole::ControlDisabledTint]);
    ExpectColor(m_builder.sliderStyle().hoverTint, colors[UiSkinColorRole::ControlHoverTint]);
    ExpectColor(m_builder.sliderStyle().pressedTint, colors[UiSkinColorRole::ControlPressedTint]);
    ExpectColor(m_builder.sliderStyle().disabledTint, colors[UiSkinColorRole::ControlDisabledTint]);
    ExpectColor(m_builder.progressStyle().trackTint, colors[UiSkinColorRole::ProgressTrackTint]);
    ExpectColor(m_builder.progressStyle().fillTint, colors[UiSkinColorRole::ProgressFillTint]);
    EXPECT_FLOAT_EQ(m_builder.style().fontSize, 19.0f);
    EXPECT_FLOAT_EQ(m_builder.style().gap, 13.0f);
    EXPECT_EQ(m_builder.style().button, Name("custom.button"));
}

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

