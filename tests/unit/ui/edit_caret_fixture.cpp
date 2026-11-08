// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_caret_fixture.h"
#include "fixture_text_shaping.h"

#include <tests/unit/ui/name_cases.adhd.h>

#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditCaretTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<ShapedRun, TextLayoutStatus::Enum> CaretShaper::shape(const ShapeRequest& request){
    ShapedRun run(m_arena);
    run.metrics = variedHeight && request.text == "TALL" ? FontMetrics{ 10.0f, 4.0f, 6.0f } : FontMetrics{ 8.0f, 2.0f, 2.0f };
    if(fault == ShaperFault::Overlap){
        run.glyphs.push_back({ {}, 1u, 0u, 2u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
        run.glyphs.push_back({ {}, 2u, 1u, 3u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
    }
    else if(fault == ShaperFault::InconsistentGlyphEnd){
        run.glyphs.push_back({ {}, 1u, 0u, 1u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
        run.glyphs.push_back({ {}, 2u, 0u, 3u, {}, {}, {} });
    }
    else if(fault == ShaperFault::NonfinitePlacement){
        run.glyphs.push_back({ {}, 1u, 0u, 1u, {}, { Limit<f32>::s_Max, 0.0f }, {} });
        run.glyphs.push_back({ {}, 2u, 1u, 2u, { Limit<f32>::s_Max, 0.0f }, {}, {} });
    }
    else if(request.text == "ffi")
        run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 30.0f, 0.0f }, {} });
    else if(request.text == "A\xcc\x81" "B"){
        run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
        run.glyphs.push_back({ {}, 2u, 3u, 4u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
    }
    else if(request.text == "e\xcc\x81")
        run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { s_CaretGlyphAdvance, 0.0f }, {} });
    else if(request.text == "\xe1\x84\x92\xe1\x85\xa1\xe1\x86\xab")
        run.glyphs.push_back({ {}, 1u, 0u, 9u, {}, { 12.0f, 0.0f }, {} });
    else{
        usize begin = fault == ShaperFault::LeadingGap ? 1u : 0u;
        const usize limit = request.text.size() - (fault == ShaperFault::TrailingGap && !request.text.empty() ? 1u : 0u);
        while(begin < limit){
            usize end = begin + 1u;
            while(end < limit && IsUtf8Continuation(static_cast<u8>(request.text[end])))
                ++end;
            run.glyphs.push_back({ {}, 1u, static_cast<u32>(begin), static_cast<u32>(end), {}, { scalarAdvance, 0.0f }, {} });
            begin = end;
        }
    }
    if(zeroAdvance){
        for(ShapedGlyph& glyph : run.glyphs)
            glyph.advance.x = 0.0f;
    }
    if(request.direction == TextDirection::RightToLeft)
        Tests::UiFixtureText::ReverseGlyphs(run);
    return run;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditCaretFixture::EditCaretFixture()
    : m_arena(Name(NWB::Tests::UiCases::s_UiCaseTestsUiWidgetsEditCaret))
    , m_shaper(m_arena)
    , m_builder(m_arena, m_shaper)
    , m_geometry(m_arena)
    , m_mapping(m_arena)
    , m_boundaries(m_arena)
{}

b8 EditCaretFixture::identityMapping(const StringView text){
    auto boundaries = GraphemeSegmentation::Build(m_arena, text);
    if(!boundaries)
        return false;
    m_boundaries = Move(*boundaries);
    m_mapping.clear();
    m_mapping.reserve(m_boundaries.size());
    for(const usize boundary : m_boundaries)
        m_mapping.push_back({ boundary, boundary });
    return true;
}

b8 EditCaretFixture::adopt(const StringView text, const usize committedBytes, const EditTextMode::Enum mode){
    auto candidate = layout(text);
    return candidate && m_geometry.adoptLayout(Move(*candidate), text, m_mapping, committedBytes, mode);
}

void EditCaretFixture::expectCaret(const usize byte, const f32 x, const f32 y, const f32 height)const{
    const auto rectangle = m_geometry.caretRect(byte);
    ASSERT_TRUE(rectangle);
    EXPECT_FLOAT_EQ(rectangle->x, x);
    EXPECT_FLOAT_EQ(rectangle->y, y);
    EXPECT_FLOAT_EQ(rectangle->width, 0.0f);
    EXPECT_FLOAT_EQ(rectangle->height, height);
}

void EditCaretFixture::expectRange(const EditBoxRange range, const u32 line, const Rect expected, const f32 cap)const{
    const auto rectangle = m_geometry.rangeOnLine(range, line, cap);
    ASSERT_TRUE(rectangle);
    EXPECT_FLOAT_EQ(rectangle->x, expected.x);
    EXPECT_FLOAT_EQ(rectangle->y, expected.y);
    EXPECT_FLOAT_EQ(rectangle->width, expected.width);
    EXPECT_FLOAT_EQ(rectangle->height, expected.height);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

