// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/edit_caret_geometry.h>

#include <global/simplemath.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditCaretTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;

inline constexpr f32 s_CaretGlyphAdvance = 10.0f;

namespace ShaperFault{
    enum Enum : u8{ None, LeadingGap, TrailingGap, Overlap, InconsistentGlyphEnd, NonfinitePlacement };
};

class CaretShaper final : public ITextShaper{
public:
    explicit CaretShaper(Core::Alloc::GlobalArena& arena)noexcept : m_arena(arena){}
    virtual ~CaretShaper()noexcept override = default;


public:
    [[nodiscard]] virtual Expected<ShapedRun, TextLayoutStatus::Enum> shape(const ShapeRequest& request)override;


public:
    f32 scalarAdvance = s_CaretGlyphAdvance;
    ShaperFault::Enum fault = ShaperFault::None;
    b8 variedHeight = false;
    b8 zeroAdvance = false;


private:
    Core::Alloc::GlobalArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EditCaretFixture : public testing::Test{
public:
    EditCaretFixture();


protected:
    [[nodiscard]] Expected<TextLayout, TextLayoutStatus::Enum> layout(const StringView text, const TextDirection::Enum direction = TextDirection::LeftToRight){
        return m_builder.layout({ .text = text, .fontSize = 16.0f, .direction = direction });
    }
    [[nodiscard]] b8 identityMapping(StringView text);
    [[nodiscard]] b8 adopt(StringView text, usize committedBytes, EditTextMode::Enum mode = EditTextMode::Multiline);
    [[nodiscard]] b8 adoptText(const StringView text, const EditTextMode::Enum mode = EditTextMode::Multiline){
        return identityMapping(text) && adopt(text, text.size(), mode);
    }
    void expectCaret(usize byte, f32 x, f32 y, f32 height = 12.0f)const;
    void expectRange(EditBoxRange range, u32 line, Rect expected, f32 cap = 1.0f)const;


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

