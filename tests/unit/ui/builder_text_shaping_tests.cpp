// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_builder_text_shaping_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using namespace NWB::Impl::Ui;
using namespace NWB::UiWidgetTests;

static constexpr StringView s_Jamo = "\xE1\x84\x92\xE1\x85\xA1\xE1\x86\xAB";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiBuilderTextShapingTests : public WidgetFixture{
protected:
    [[nodiscard]] bool installKoreanFallback(){
        const auto path = ::Path<Core::Alloc::GlobalArena>(m_arena, NWB_TEST_FONT_DIRECTORY) / "korean.font";
        Core::Assets::AssetBytes bytes(m_arena);
        auto bytesResult = Tests::ReadBundledFontBytes(path, bytes.get_allocator().arena());
        if(!bytesResult)
            return false;
        bytes = Move(*bytesResult);
        Font korean(m_arena, Name("tests/ui/fonts/korean"));
        korean.setFontBytes(Move(bytes));
        if(!korean.validatePayload())
            return false;
        const FontSource sources[]{
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/latin"), m_font, 1u },
            { Core::Assets::AssetRef<Font>("tests/ui/fonts/korean"), korean, 1u },
        };
        return m_text.setFonts(sources, LengthOf(sources));
    }

    void addEditRegion(){
        UiSkin::RegionVector regions(m_arena);
        for(const UiSkinRegion& region : m_skin.regions())
            regions.push_back(region);
        UiSkinRegion edit;
        edit.name = Name("edit.normal");
        edit.rectangle = { 88u, 0u, 8u, 8u };
        regions.push_back(edit);
        m_skin.setAtlas(m_skin.texture(), m_skin.atlasWidth(), m_skin.atlasHeight(), m_skin.referenceDensity(), Move(regions));
        m_builder.setSkin(m_skin);
    }

    [[nodiscard]] static u32 GlyphQuads(const DrawSnapshot& snapshot){
        u32 count = 0u;
        for(const DrawCommand& command : snapshot.commands()){
            if(command.material == PaintMaterial::Glyph || command.material == PaintMaterial::SdfGlyph)
                count += command.indexCount / 6u;
        }
        return count;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(UiBuilderTextShapingTests, InvalidPolicyPreservesPreviousShapingAndOpenScopeMutationFails){
    ASSERT_TRUE(installKoreanFallback());
    addEditRegion();
    ASSERT_TRUE(m_skin.validatePayload());
    ASSERT_TRUE(m_builder.setTextShaping(TextScriptTag('H', 'a', 'n', 'g'), "ko"));
    EXPECT_FALSE(m_builder.setTextShaping(0u, "ko"));
    EXPECT_FALSE(m_builder.setTextShaping(TextScriptTag('L', 'a', 't', 'n'), "ko!"));
    ASSERT_FALSE(m_builder.failed());
    EditModel model(m_arena);
    ASSERT_TRUE(model.setText(s_Jamo));
    EditBoxState state;

    ASSERT_TRUE(begin(1u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 400.0f, 180.0f }));
    ASSERT_TRUE(m_builder.label("label", s_Jamo));
    ASSERT_TRUE(m_builder.editBox("edit", model, state).valid);
    ASSERT_TRUE(finishPanel());
    const DrawSnapshot snapshot = m_paint.freeze();
    EXPECT_EQ(GlyphQuads(snapshot), 2u);
    ASSERT_TRUE(m_context.commitFrame(1u));

    ASSERT_TRUE(begin(2u));
    ASSERT_TRUE(m_builder.beginPanel("panel", { 0.0f, 0.0f, 400.0f, 100.0f }));
    EXPECT_FALSE(m_builder.setTextShaping(TextScriptTag('L', 'a', 't', 'n'), "en"));
    EXPECT_TRUE(m_builder.failed());
    EXPECT_FALSE(m_builder.endPanel());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

