// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_caret_geometry.h"

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
        const AStringView committed = model.text();
        const EditCompositionView composition = model.composition();
        usize replaced = 0u;
        usize preeditBytes = 0u;
        usize displayBytes = 0u;
        if(!TryResolveEditBoxSnapshotSizes(committed.size(), composition, replaced, preeditBytes, displayBytes))
            return false;
        AString<Core::Alloc::GlobalArena> display(*m_arena);
        display.reserve(displayBytes);
        if(composition.active){
            display.append(committed.data(), composition.replacementStart);
            if(preeditBytes != 0u)
                display.append(composition.text.data(), preeditBytes);
            display.append(committed.data() + composition.replacementEnd, committed.size() - composition.replacementEnd);
        }
        else if(!committed.empty())
            display.assign(committed.data(), committed.size());
        const StringView candidateText(display.data(), display.size());
        if(!ValidateEditBoxDisplayText(candidateText, model.textMode()))
            return false;
        PaintVector<EditCaretMapping> stops(*m_arena);
        stops.reserve(model.graphemeBoundaries().size() + 1u);
        for(const usize boundary : model.graphemeBoundaries()){
            if(!composition.active || boundary <= composition.replacementStart)
                stops.push_back({ boundary, boundary });
            if(composition.active && boundary >= composition.replacementEnd)
                stops.push_back({ boundary, boundary - replaced + preeditBytes });
        }
        m_display = Move(display);
        m_mapping = Move(stops);
        m_composing = composition.active;
        ResolveEditBoxSnapshotRanges(
            composition,
            preeditBytes,
            model.selectionStart(),
            model.selectionEnd(),
            model.caret(),
            m_preedit,
            m_replacement,
            m_selection,
            m_caret
        );
        m_committedBytes = committed.size();
        m_revision = model.revision();
        m_textMode = model.textMode();
        m_ready = false;
        return true;
    }

    // request.text is supplied by the owned snapshot. Font size remains logical; TextService applies paint DPI.
    [[nodiscard]] TextLayoutStatus::Enum shape(TextService& text, ShapeRequest request = {}){
        if(m_revision == 0u || request.direction != TextDirection::LeftToRight)
            return TextLayoutStatus::InvalidParameters;
        request.text = displayText();
        const TextLayoutStatus::Enum validation = ValidateTextRequest(request, m_textMode == EditTextMode::Multiline);
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
        if(!m_ready)
            return false;
        const auto measured = text.measure(layout(), display);
        if(!measured)
            return false;
        m_visualBounds = *measured;
        return true;
    }
    [[nodiscard]] Expected<EditBoxPlacement> arrange(const Rect& bounds, const Insets& padding, const Rect& clip, const Point previousScroll, const f32 caretWidth = 1.0f, const b8 revealCaret = true)const noexcept{
        if(!m_ready)
            return MakeUnexpected(Failure{});
        const auto viewport = TryComputeEditBoxViewport(bounds, padding, clip, previousScroll, caretWidth);
        if(!viewport)
            return MakeUnexpected(Failure{});
        return arrangeViewport(bounds, *viewport, clip, previousScroll, caretWidth, revealCaret);
    }
    // Uses a caller-reserved content viewport, such as a two-axis scrollbar layout.
    [[nodiscard]] Expected<EditBoxPlacement> arrangeViewport(const Rect& bounds, const Rect& viewport, const Rect& clip, const Point previousScroll, const f32 caretWidth = 1.0f, const b8 revealCaret = true)const noexcept{
        if(!m_ready || !ValidateEditBoxViewportArrangeInputs(bounds, viewport, clip, previousScroll, caretWidth))
            return MakeUnexpected(Failure{});
        const auto caret = m_geometry.caretRect(m_caret);
        if(!caret)
            return MakeUnexpected(Failure{});
        const auto placement = TryComputeEditBoxViewportPlacement(bounds, viewport, clip, previousScroll, caretWidth, revealCaret, m_textMode, m_visualBounds, layout().measure(), *caret);
        if(!placement)
            return MakeUnexpected(Failure{});
        auto selection = m_geometry.rangeOnLine(m_selection, 0u, caretWidth);
        if(!selection)
            return MakeUnexpected(Failure{});
        Rect preeditUnderline{};
        if(m_composing){
            const auto underline = m_geometry.rangeOnLine(m_preedit, 0u, caretWidth);
            if(!underline)
                return MakeUnexpected(underline.error());
            preeditUnderline = *underline;
        }
        if(!ValidateEditBoxArrangedSegments(*placement, caretWidth, *selection, preeditUnderline, m_composing))
            return MakeUnexpected(Failure{});
        return *placement;
    }
    [[nodiscard]] Expected<usize> hitTest(const Point point, const EditBoxPlacement& placement)const noexcept{
        if(!m_ready || !IsFinite(point.x) || !IsFinite(point.y) || !IsFinite(placement.textOrigin.x) || !IsFinite(placement.textOrigin.y))
            return MakeUnexpected(Failure{});
        return m_geometry.hitTest({ point.x - placement.textOrigin.x, point.y - placement.textOrigin.y });
    }

    [[nodiscard]] b8 paint(TextService& text, PaintBuilder& paint, const UiSkin& skin, const EditBoxPlacement& placement, const EditBoxStyle& style = {}, const EditBoxPaintFlags& flags = {})const{
        if(!m_ready || !IsValidEditBoxPaintStyle(style))
            return false;
        paint.pushClip(placement.frameClip);
        b8 painted = true;
        if(const UiSkinRegion* background = ResolveBackground(skin, style, flags))
            painted = paint.drawRegion(background->name, placement.bounds);
        else
            paint.fillRect(placement.bounds, style.background);
        if(painted && flags.focused && flags.enabled && skin.findRegion(style.focus))
            painted = paint.drawRegion(style.focus, placement.bounds);
        paint.pushClip(placement.clip);
        if(painted){
            const Color& selection = flags.focused && flags.enabled ? style.selection : style.inactiveSelection;
            if(m_selection.begin == m_selection.end)
                painted = static_cast<b8>(m_geometry.rangeOnLine(m_selection, 0u, placement.caret.width));
            else{
                for(u32 line = 0u; line < m_geometry.lines().size(); ++line){
                    auto segment = m_geometry.rangeOnLine(m_selection, line, placement.caret.width);
                    if(!segment){
                        painted = false;
                        break;
                    }
                    if(segment->width > 0.0f){
                        OffsetEditBoxSegmentOrigin(placement.textOrigin, *segment);
                        paint.fillRect(*segment, selection);
                    }
                }
            }
            painted = painted && text.paint(paint, layout(), placement.textOrigin, flags.enabled ? style.text : style.disabledText);
        }
        if(painted && m_composing && flags.focused && flags.enabled){
            for(u32 line = 0u; line < m_geometry.lines().size(); ++line){
                auto segment = m_geometry.rangeOnLine(m_preedit, line, placement.caret.width);
                if(!segment){
                    painted = false;
                    break;
                }
                if(segment->width > 0.0f){
                    PlaceEditPreeditUnderline(placement.textOrigin, placement.caret.width, *segment);
                    paint.fillRect(*segment, style.preedit);
                }
            }
        }
        if(painted && ShouldPaintEditBoxCaret(flags, m_composing))
            paint.fillRect(placement.caret, style.caret);
        const b8 contentPopped = paint.popClip();
        const b8 framePopped = paint.popClip();
        return painted && contentPopped && framePopped;
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
    [[nodiscard]] static const UiSkinRegion* ResolveBackground(const UiSkin& skin, const EditBoxStyle& style, const EditBoxPaintFlags& flags)noexcept{
        const Name preferred = ResolvePreferredEditBoxBackgroundName(style, flags);
        const UiSkinRegion* region = skin.findRegion(preferred);
        if(!region)
            region = skin.findRegion(style.normal);
        if(!region)
            region = skin.findRegion(style.fallback);
        return region;
    }


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
