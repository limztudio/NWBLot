// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/edit_caret_geometry.h>

#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditCaretTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

namespace ShaperFault{
    enum Enum : u8{ None, LeadingGap, TrailingGap, Overlap, InconsistentGlyphEnd, NonfinitePlacement };
};

class CaretShaper final : public ITextShaper{
public:
    explicit CaretShaper(Core::Alloc::GlobalArena& arena)
        : m_arena(arena)
    {}
    virtual ~CaretShaper()override = default;


public:
    [[nodiscard]] virtual TextLayoutStatus::Enum shape(const ShapeRequest& request, ShapedRun& output)override{
        ShapedRun run(m_arena);
        run.metrics = variedHeight && request.text == "TALL" ? FontMetrics{ 10.0f, 4.0f, 6.0f } : FontMetrics{ 8.0f, 2.0f, 2.0f };
        if(fault == ShaperFault::Overlap){
            run.glyphs.push_back({ {}, 1u, 0u, 2u, {}, { 10.0f, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 1u, 3u, {}, { 10.0f, 0.0f }, {} });
        }
        else if(fault == ShaperFault::InconsistentGlyphEnd){
            run.glyphs.push_back({ {}, 1u, 0u, 1u, {}, { 10.0f, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 0u, 3u, {}, {}, {} });
        }
        else if(fault == ShaperFault::NonfinitePlacement){
            run.glyphs.push_back({ {}, 1u, 0u, 1u, {}, { Limit<f32>::s_Max, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 1u, 2u, { Limit<f32>::s_Max, 0.0f }, {}, {} });
        }
        else if(request.text == "ffi")
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 30.0f, 0.0f }, {} });
        else if(request.text == "A\xcc\x81" "B"){
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, {} });
            run.glyphs.push_back({ {}, 2u, 3u, 4u, {}, { 10.0f, 0.0f }, {} });
        }
        else if(request.text == "e\xcc\x81")
            run.glyphs.push_back({ {}, 1u, 0u, 3u, {}, { 10.0f, 0.0f }, {} });
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
        if(request.direction == TextDirection::RightToLeft){
            for(usize index = 0u; index < run.glyphs.size() / 2u; ++index)
                Swap(run.glyphs[index], run.glyphs[run.glyphs.size() - index - 1u]);
        }
        output = Move(run);
        return TextLayoutStatus::Success;
    }


private:
    Core::Alloc::GlobalArena& m_arena;


public:
    ShaperFault::Enum fault = ShaperFault::None;
    f32 scalarAdvance = 10.0f;
    bool variedHeight = false;
    bool zeroAdvance = false;
};

class EditCaretFixture : public testing::Test{
public:
    EditCaretFixture()
        : m_arena(Name("tests/ui/widgets/edit_caret"))
        , m_shaper(m_arena)
        , m_builder(m_arena, m_shaper)
        , m_geometry(m_arena)
        , m_mapping(m_arena)
        , m_boundaries(m_arena)
    {}


protected:
    [[nodiscard]] bool layout(const StringView text, TextLayout& output,
        const TextDirection::Enum direction = TextDirection::LeftToRight){
        return m_builder.layout({ text, 16.0f, direction }, output) == TextLayoutStatus::Success;
    }

    [[nodiscard]] bool identityMapping(const StringView text){
        if(!GraphemeSegmentation::build(text, m_boundaries))
            return false;
        m_mapping.clear();
        m_mapping.reserve(m_boundaries.size());
        for(const usize boundary : m_boundaries)
            m_mapping.push_back({ boundary, boundary });
        return true;
    }

    [[nodiscard]] bool adopt(const StringView text, const usize committedBytes,
        const EditTextMode::Enum mode = EditTextMode::Multiline){
        TextLayout candidate(m_arena);
        return layout(text, candidate) && m_geometry.adoptLayout(Move(candidate), text, m_mapping, committedBytes, mode);
    }

    [[nodiscard]] bool adoptText(const StringView text, const EditTextMode::Enum mode = EditTextMode::Multiline){
        return identityMapping(text) && adopt(text, text.size(), mode);
    }

    void expectCaret(const usize byte, const f32 x, const f32 y, const f32 height = 12.0f)const{
        Rect rectangle;
        ASSERT_TRUE(m_geometry.caretRect(byte, rectangle));
        EXPECT_FLOAT_EQ(rectangle.x, x);
        EXPECT_FLOAT_EQ(rectangle.y, y);
        EXPECT_FLOAT_EQ(rectangle.width, 0.0f);
        EXPECT_FLOAT_EQ(rectangle.height, height);
    }

    void expectRange(const EditBoxRange range, const u32 line, const Rect expected, const f32 cap = 1.0f)const{
        Rect rectangle{ 999.0f, 999.0f, 999.0f, 999.0f };
        ASSERT_TRUE(m_geometry.rangeOnLine(range, line, cap, rectangle));
        EXPECT_FLOAT_EQ(rectangle.x, expected.x);
        EXPECT_FLOAT_EQ(rectangle.y, expected.y);
        EXPECT_FLOAT_EQ(rectangle.width, expected.width);
        EXPECT_FLOAT_EQ(rectangle.height, expected.height);
    }


protected:
    Core::Alloc::GlobalArena m_arena;
    CaretShaper m_shaper;
    TextLayoutBuilder m_builder;
    EditCaretGeometry m_geometry;
    PaintVector<EditCaretMapping> m_mapping;
    EditBoundaryVector m_boundaries;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

