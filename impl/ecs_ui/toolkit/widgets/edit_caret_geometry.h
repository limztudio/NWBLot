// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/edit/model.h>
#include <impl/ecs_ui/toolkit/edit/grapheme.h>
#include <impl/ecs_ui/toolkit/text/layout.h>

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EditBoxCaretStop{
    usize committedByte = 0u;
    usize displayByte = 0u;
    f32 x = 0.0f;
    u32 lineIndex = 0u;
};

struct EditBoxRange{
    usize begin = 0u;
    usize end = 0u;
};

struct EditCaretMapping{
    usize committedByte = 0u;
    usize displayByte = 0u;
};

struct EditCaretLine{
    usize byteBegin = 0u;
    usize byteEnd = 0u;
    usize breakEnd = 0u;
    u32 firstStop = 0u;
    u32 stopCount = 0u;
    f32 top = 0.0f;
    f32 height = 0.0f;
    f32 advance = 0.0f;
};

// Queries owned projections of validated caret geometry; copied hosts share line, midpoint and preedit endpoint behavior.
[[nodiscard]] Expected<usize> HitEditCaretGeometry(Span<const EditCaretLine> lines, Span<const EditBoxCaretStop> stops, Point localPoint)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns source bytes, exact font versions and LTR hard-line caret data; no model or service is retained.
class EditCaretGeometry final{
public:
    explicit EditCaretGeometry(Core::Alloc::GlobalArena& arena)noexcept;
    EditCaretGeometry(EditCaretGeometry&&)noexcept = default;
    EditCaretGeometry(const EditCaretGeometry&) = delete;


public:
    EditCaretGeometry& operator=(EditCaretGeometry&&)noexcept = default;

    EditCaretGeometry& operator=(const EditCaretGeometry&) = delete;


public:
    // Mapping is supplied by the committed/preedit snapshot; adoption is atomic.
    [[nodiscard]] b8 adoptLayout(TextLayout&& layout, StringView expectedText, Span<const EditCaretMapping> mapping, usize committedBytes, EditTextMode::Enum mode);
    // Native preedit can address scalar edges inside graphemes; rectangles are local and have zero width.
    [[nodiscard]] Expected<Rect> caretRect(usize displayByte)const noexcept;
    [[nodiscard]] Expected<usize> hitTest(Point localPoint)const noexcept;
    [[nodiscard]] Expected<usize> verticalTarget(usize displayCaret, b8 down, f32 preferredX)const noexcept;
    // A valid nonintersecting range yields an empty rectangle. Selected LF bytes use the supplied trailing cap.
    [[nodiscard]] Expected<Rect> rangeOnLine(EditBoxRange range, u32 lineIndex, f32 breakWidth)const noexcept;

    [[nodiscard]] const TextLayout& layout()const noexcept{ return m_layout; }

    [[nodiscard]] const PaintVector<EditBoxCaretStop>& caretStops()const noexcept{ return m_stops; }

    [[nodiscard]] const PaintVector<EditCaretLine>& lines()const noexcept{ return m_lines; }

    [[nodiscard]] EditTextMode::Enum textMode()const noexcept{ return m_mode; }

    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }


private:
    [[nodiscard]] Expected<usize> nearestStop(u32 lineIndex, Point point)const noexcept;


private:
    NotNull<Core::Alloc::GlobalArena*> m_arena;
    TextLayout m_layout;
    EditBoundaryVector m_boundaries;
    PaintVector<EditBoxCaretStop> m_stops;
    PaintVector<EditCaretLine> m_lines;
    EditTextMode::Enum m_mode = EditTextMode::SingleLine;
    b8 m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

