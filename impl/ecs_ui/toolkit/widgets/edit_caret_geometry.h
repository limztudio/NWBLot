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
[[nodiscard]] bool HitEditCaretGeometry(const PaintVector<EditCaretLine>& lines, const PaintVector<EditBoxCaretStop>& stops,
    Point localPoint, usize& committedByte);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns source bytes, exact font versions and LTR hard-line caret data; no model or service is retained.
class EditCaretGeometry final{
public:
    explicit EditCaretGeometry(Core::Alloc::GlobalArena& arena);
    EditCaretGeometry(EditCaretGeometry&&) = default;
    EditCaretGeometry& operator=(EditCaretGeometry&&) = default;


public:
    EditCaretGeometry(const EditCaretGeometry&) = delete;
    EditCaretGeometry& operator=(const EditCaretGeometry&) = delete;


public:
    // Mapping is supplied by the committed/preedit snapshot; adoption and failed query outputs are atomic.
    [[nodiscard]] bool adoptLayout(TextLayout&& layout, StringView expectedText,
        const PaintVector<EditCaretMapping>& mapping, usize committedBytes, EditTextMode::Enum mode);
    // Native preedit can address scalar edges inside graphemes; rectangles are local and have zero width.
    [[nodiscard]] bool caretRect(usize displayByte, Rect& output)const;
    [[nodiscard]] bool hitTest(Point localPoint, usize& committedByte)const;
    [[nodiscard]] bool verticalTarget(usize displayCaret, bool down, f32 preferredX, usize& committedByte)const;
    // A valid nonintersecting range yields an empty rectangle. Selected LF bytes use the supplied trailing cap.
    [[nodiscard]] bool rangeOnLine(EditBoxRange range, u32 lineIndex, f32 breakWidth, Rect& output)const;


public:
    [[nodiscard]] const TextLayout& layout()const{ return m_layout; }
    [[nodiscard]] const PaintVector<EditBoxCaretStop>& caretStops()const{ return m_stops; }
    [[nodiscard]] const PaintVector<EditCaretLine>& lines()const{ return m_lines; }
    [[nodiscard]] EditTextMode::Enum textMode()const{ return m_mode; }
    [[nodiscard]] bool ready()const{ return m_ready; }


private:
    [[nodiscard]] bool nearestStop(u32 lineIndex, Point point, usize& committedByte)const;


private:
    NotNull<Core::Alloc::GlobalArena*> m_arena;
    TextLayout m_layout;
    EditBoundaryVector m_boundaries;
    PaintVector<EditBoxCaretStop> m_stops;
    PaintVector<EditCaretLine> m_lines;
    EditTextMode::Enum m_mode = EditTextMode::SingleLine;
    bool m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

