// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/edit/model.h>
#include <impl/ecs_ui/toolkit/edit/grapheme.h>
#include <impl/ecs_ui/toolkit/text/layout.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_visual_paint.adhd.h>

#include <global/not_null.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns source bytes, exact font versions and LTR hard-line caret data; no model or service is retained.
class EditCaretGeometry final{
public:
    explicit EditCaretGeometry(Core::Alloc::GlobalArena& arena)noexcept
        : m_layout(arena)
        , m_boundaries(arena)
        , m_stops(arena)
        , m_lines(arena)
    {}
    EditCaretGeometry(EditCaretGeometry&&)noexcept = default;
    EditCaretGeometry(const EditCaretGeometry&) = delete;


public:
    EditCaretGeometry& operator=(EditCaretGeometry&&)noexcept = default;

    EditCaretGeometry& operator=(const EditCaretGeometry&) = delete;


public:
    // Mapping is supplied by the committed/preedit snapshot; adoption is atomic.
    [[nodiscard]] b8 adoptLayout(TextLayout&& layout, const StringView expectedText, const Span<const EditCaretMapping> mapping, const usize committedBytes, const EditTextMode::Enum mode){
        if(!ValidateEditCaretClusterGlyphs(layout.clusters(), layout.glyphs()))
            return false;
        if(!AdoptEditCaretGeometryInto(
            layout.utf8(),
            layout.fontSize(),
            layout.lines(),
            layout.clusters(),
            layout.glyphs().size(),
            layout.measure(),
            layout.inkBounds(),
            expectedText,
            mapping,
            committedBytes,
            mode,
            m_boundaries,
            m_stops,
            m_lines
        ))
            return false;
        m_layout = Move(layout);
        m_mode = mode;
        m_ready = true;
        return true;
    }
    // Native preedit can address scalar edges inside graphemes; rectangles are local and have zero width.
    [[nodiscard]] Expected<Rect> caretRect(const usize displayByte)const noexcept{
        if(!m_ready)
            return MakeUnexpected(Failure{});
        return ResolveEditCaretRect(m_layout.utf8(), m_layout.lines(), m_layout.clusters(), m_boundaries, displayByte);
    }
    [[nodiscard]] Expected<usize> hitTest(const Point localPoint)const noexcept{
        if(!m_ready)
            return MakeUnexpected(Failure{});
        return HitEditCaretGeometry(m_lines, m_stops, localPoint);
    }
    [[nodiscard]] Expected<usize> verticalTarget(const usize displayCaret, const b8 down, const f32 preferredX)const noexcept{
        if(!m_ready)
            return MakeUnexpected(Failure{});
        return ResolveEditCaretVerticalTarget(m_layout.utf8(), m_layout.lines(), m_layout.clusters(), m_boundaries, m_lines, m_stops, displayCaret, down, preferredX);
    }
    // A valid nonintersecting range yields an empty rectangle. Selected LF bytes use the supplied trailing cap.
    [[nodiscard]] Expected<Rect> rangeOnLine(const EditBoxRange range, const u32 lineIndex, const f32 breakWidth)const noexcept{
        if(!m_ready)
            return MakeUnexpected(Failure{});
        return ResolveEditCaretRangeOnLine(m_layout.utf8(), m_layout.lines(), m_layout.clusters(), m_boundaries, m_lines, range, lineIndex, breakWidth);
    }

    [[nodiscard]] const TextLayout& layout()const noexcept{ return m_layout; }

    [[nodiscard]] const EditBoundaryVector& boundaries()const noexcept{ return m_boundaries; }

    [[nodiscard]] const PaintVector<EditBoxCaretStop>& caretStops()const noexcept{ return m_stops; }

    [[nodiscard]] const PaintVector<EditCaretLine>& lines()const noexcept{ return m_lines; }

    [[nodiscard]] EditTextMode::Enum textMode()const noexcept{ return m_mode; }

    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }


private:
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
