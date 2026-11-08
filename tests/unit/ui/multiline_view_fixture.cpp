// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "multiline_view_fixture.h"

#include <tests/unit/ui/name_cases.adhd.h>

#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiMultilineViewTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ExpectPlacement(const EditBoxPlacement& actual, const EditBoxPlacement& expected){
    NWB::UiPaintAssertions::ExpectRect(actual.bounds, expected.bounds);
    NWB::UiPaintAssertions::ExpectRect(actual.frameClip, expected.frameClip);
    NWB::UiPaintAssertions::ExpectRect(actual.content, expected.content);
    NWB::UiPaintAssertions::ExpectRect(actual.clip, expected.clip);
    NWB::UiPaintAssertions::ExpectRect(actual.caret, expected.caret);
    EXPECT_FLOAT_EQ(actual.textOrigin.x, expected.textOrigin.x);
    EXPECT_FLOAT_EQ(actual.textOrigin.y, expected.textOrigin.y);
    EXPECT_FLOAT_EQ(actual.scroll, expected.scroll);
    EXPECT_FLOAT_EQ(actual.scrollY, expected.scrollY);
}

void CollectSolidRects(const DrawSnapshot& snapshot, const Color& color, PaintVector<Rect>& output){
    output.clear();
    for(const DrawCommand& command : snapshot.commands()){
        if(command.material != PaintMaterial::Solid)
            continue;
        for(u32 index = command.firstIndex; index + 5u < command.firstIndex + command.indexCount; index += 6u){
            const Vertex& first = snapshot.vertices()[snapshot.indices()[index]];
            if(first.color.r != color.r || first.color.g != color.g || first.color.b != color.b || first.color.a != color.a)
                continue;
            const Vertex& opposite = snapshot.vertices()[snapshot.indices()[index + 2u]];
            output.push_back({ first.position.x, first.position.y, opposite.position.x - first.position.x, opposite.position.y - first.position.y });
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


MultilineViewFixture::MultilineViewFixture()
    : m_arena(Name(NWB::Tests::UiCases::s_UiCaseTestsUiWidgetsMultilineView))
    , m_model(m_arena, {}, EditTextMode::Multiline)
    , m_shaper(m_arena)
    , m_layoutBuilder(m_arena, m_shaper)
    , m_view(m_arena)
    , m_font(m_arena, Name(s_MultilineViewFontPath))
    , m_text(m_arena)
    , m_skin(m_arena, Name(s_MultilineViewSkinPath))
    , m_paint(m_arena)
{
    UiSkin::RegionVector regions(m_arena);
    m_skin.setAtlas(Core::Assets::AssetRef<Texture>("tests/ui/widgets/multiline_view/texture"), 4u, 4u, 1.0f, Move(regions));
}

b8 MultilineViewFixture::shapeView(){
    if(!m_view.snapshot(m_model))
        return false;
    auto layout = m_layoutBuilder.layout({ m_view.displayText() });
    return layout && m_view.adoptLayout(Move(*layout));
}

b8 MultilineViewFixture::place(const f32 width, const f32 height, const Point previousScroll)noexcept{
    const auto placement = m_view.arrange({ 10.0f, 20.0f, width, height }, {}, s_MultilineViewViewport, previousScroll);
    if(!placement)
        return false;
    m_placement = *placement;
    return true;
}

b8 MultilineViewFixture::loadFont(){
    Core::Alloc::ScratchArena fontScratchArena(s_FontScratchArenaName);
    return InstallLatinFixtureFont(fontScratchArena, m_arena, m_font, m_text);
}

Expected<DrawSnapshot> MultilineViewFixture::paintView(const EditBoxStyle& style, const EditBoxPaintFlags& flags){
    m_paint.begin({ s_MultilineViewViewport.width, s_MultilineViewViewport.height, 1.0f, 1.0f }, 1u, 1u, Core::Assets::AssetRef<UiSkin>(s_MultilineViewSkinPath), m_skin);
    if(!m_view.paint(m_text, m_paint, m_skin, m_placement, style, flags))
        return MakeUnexpected(Failure{});
    EXPECT_FALSE(m_paint.popClip());
    return m_paint.freeze();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

