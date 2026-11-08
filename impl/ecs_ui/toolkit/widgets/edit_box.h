// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_caret_geometry.h"
#include <impl/ecs_ui/toolkit/widgets/edit_box_snapshot_builder.adhd.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_visual_paint.adhd.h>

#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>
#include <impl/ecs_ui/toolkit/text/service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns this declaration's display bytes and immutable font versions; the edit model is borrowed only during snapshot().
// Hard-line LTR geometry interpolates grapheme caret stops inside ligatures; it does not provide wrap or paragraph bidi.
class EditBoxView final{
public:
    explicit EditBoxView(Core::Alloc::GlobalArena& arena)noexcept
        : m_arena(&arena)
        , m_display(arena)
        , m_mapping(arena)
        , m_geometry(arena)
    {}
    EditBoxView(EditBoxView&&)noexcept = default;
    EditBoxView(const EditBoxView&) = delete;


public:
    EditBoxView& operator=(EditBoxView&&)noexcept = default;

    EditBoxView& operator=(const EditBoxView&) = delete;


public:
    [[nodiscard]] b8 snapshot(const EditModel& model){
        return EditBoxSnapshotBuilder{}.commit(
            model.text(),
            model.graphemeBoundaries(),
            model.composition(),
            model.textMode(),
            model.selectionStart(),
            model.selectionEnd(),
            model.caret(),
            model.revision(),
            m_display,
            m_mapping,
            m_selection,
            m_preedit,
            m_replacement,
            m_caret,
            m_committedBytes,
            m_revision,
            m_textMode,
            m_composing,
            m_ready
        );
    }

    // request.text is supplied by the owned snapshot. Font size remains logical; TextService applies paint DPI.
    [[nodiscard]] TextLayoutStatus::Enum shape(TextService& text, ShapeRequest request = {}){
        const TextLayoutStatus::Enum validation = PrepareEditBoxShapeRequest(m_revision, displayText(), m_textMode, request);
        if(validation != TextLayoutStatus::Success)
            return validation;
        auto candidate = text.layout(request);
        if(!candidate)
            return candidate.error();
        return adoptLayout(Move(*candidate)) ? TextLayoutStatus::Success : TextLayoutStatus::FontFailure;
    }

    // Admits matching source bytes and mode-appropriate hard lines with LTR edges. Failure preserves prior geometry.
    [[nodiscard]] b8 adoptLayout(TextLayout&& layout){
        if(m_revision == 0u)
            return false;
        EditCaretGeometry candidate(*m_arena);
        if(!candidate.adoptLayout(Move(layout), displayText(), m_mapping, m_committedBytes, m_textMode))
            return false;
        if(!candidate.caretRect(m_caret) || !candidate.caretRect(m_selection.begin) || !candidate.caretRect(m_selection.end))
            return false;
        const Point logicalSize = candidate.layout().measure();
        m_geometry = Move(candidate);
        m_visualBounds = { 0.0f, 0.0f, logicalSize.x, logicalSize.y };
        m_ready = true;
        return true;
    }
    // Shape at display scale before arranging; measurement keeps line, caret, hit-test, composition coords.
    [[nodiscard]] b8 measure(TextService& text, const DisplayMetrics& display){
        return MeasureEditBoxViewVisual(m_ready, text, layout(), display, m_visualBounds);
    }
    [[nodiscard]] Expected<EditBoxPlacement> arrange(const Rect& bounds, const Insets& padding, const Rect& clip, const Point previousScroll, const f32 caretWidth = 1.0f, const b8 revealCaret = true)const noexcept{
        return TryArrangeEditBox(
            m_ready,
            bounds,
            padding,
            clip,
            previousScroll,
            caretWidth,
            revealCaret,
            m_textMode,
            m_visualBounds,
            layout().measure(),
            m_geometry.layout().utf8(),
            m_geometry.layout().lines(),
            m_geometry.layout().clusters(),
            m_geometry.boundaries(),
            m_geometry.lines(),
            m_caret,
            m_selection,
            m_preedit,
            m_composing
        );
    }
    // Uses a caller-reserved content viewport, such as a two-axis scrollbar layout.
    [[nodiscard]] Expected<EditBoxPlacement> arrangeViewport(const Rect& bounds, const Rect& viewport, const Rect& clip, const Point previousScroll, const f32 caretWidth = 1.0f, const b8 revealCaret = true)const noexcept{
        return TryArrangeEditBoxViewport(
            m_ready,
            bounds,
            viewport,
            clip,
            previousScroll,
            caretWidth,
            revealCaret,
            m_textMode,
            m_visualBounds,
            layout().measure(),
            m_geometry.layout().utf8(),
            m_geometry.layout().lines(),
            m_geometry.layout().clusters(),
            m_geometry.boundaries(),
            m_geometry.lines(),
            m_caret,
            m_selection,
            m_preedit,
            m_composing
        );
    }
    [[nodiscard]] Expected<usize> hitTest(const Point point, const EditBoxPlacement& placement)const noexcept{
        return TryHitTestEditBoxView(m_ready, m_geometry.ready(), point, placement, m_geometry.lines(), m_geometry.caretStops());
    }

    [[nodiscard]] b8 paint(TextService& text, PaintBuilder& paint, const UiSkin& skin, const EditBoxPlacement& placement, const EditBoxStyle& style = {}, const EditBoxPaintFlags& flags = {})const{
        if(!m_ready)
            return false;
        return PaintEditBoxViewVisual(
            text,
            paint,
            skin,
            layout(),
            m_geometry.layout().utf8(),
            m_geometry.layout().lines(),
            m_geometry.layout().clusters(),
            m_geometry.boundaries(),
            m_geometry.lines(),
            m_selection,
            m_preedit,
            m_composing,
            placement,
            style,
            flags
        );
    }

    [[nodiscard]] StringView displayText()const noexcept{ return { m_display.data(), m_display.size() }; }

    [[nodiscard]] const TextLayout& layout()const noexcept{ return m_geometry.layout(); }

    [[nodiscard]] const Rect& visualBounds()const noexcept{ return m_visualBounds; }

    [[nodiscard]] const PaintVector<EditBoxCaretStop>& caretStops()const noexcept{ return m_geometry.caretStops(); }

    [[nodiscard]] const EditCaretGeometry& caretGeometry()const noexcept{ return m_geometry; }

    [[nodiscard]] EditTextMode::Enum textMode()const noexcept{ return m_textMode; }

    [[nodiscard]] u64 revision()const noexcept{ return m_revision; }

    [[nodiscard]] usize committedBytes()const noexcept{ return m_committedBytes; }

    [[nodiscard]] usize displayCaret()const noexcept{ return m_caret; }

    [[nodiscard]] EditBoxRange selectionRange()const noexcept{ return m_selection; }

    [[nodiscard]] EditBoxRange preeditRange()const noexcept{ return m_preedit; }

    [[nodiscard]] EditBoxRange replacementRange()const noexcept{ return m_replacement; }

    [[nodiscard]] b8 composing()const noexcept{ return m_composing; }

    [[nodiscard]] b8 ready()const noexcept{ return m_ready; }


private:
    NotNull<Core::Alloc::GlobalArena*> m_arena;
    AString<Core::Alloc::GlobalArena> m_display;
    PaintVector<EditCaretMapping> m_mapping;
    EditCaretGeometry m_geometry;
    Rect m_visualBounds;
    EditBoxRange m_selection;
    EditBoxRange m_preedit;
    EditBoxRange m_replacement;
    usize m_caret = 0u;
    usize m_committedBytes = 0u;
    u64 m_revision = 0u;
    EditTextMode::Enum m_textMode = EditTextMode::SingleLine;
    b8 m_composing = false;
    b8 m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

