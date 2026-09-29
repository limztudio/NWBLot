// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "style.h"
#include "edit_caret_geometry.h"

#include <impl/ui/edit/model.h>
#include <impl/ui/edit/grapheme.h>
#include <impl/ui/text/service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EditBoxPlacement{
    Rect bounds;
    Rect frameClip;
    Rect content;
    Rect clip;
    Point textOrigin;
    Rect caret;
    Rect selection;
    Rect preeditUnderline;
    f32 scroll = 0.0f;
    f32 scrollY = 0.0f;
};

struct EditBoxStyle{
    Name normal = Name("edit.normal");
    Name hover = Name("edit.hover");
    Name focused = Name("edit.focused");
    Name disabled = Name("edit.disabled");
    Name fallback = Name("button.normal");
    Name focus = Name("focus.overlay");
    Color background = { 0.08f, 0.10f, 0.14f, 1.0f };
    Color text = { 0.92f, 0.94f, 0.98f, 1.0f };
    Color disabledText = { 0.48f, 0.50f, 0.55f, 1.0f };
    Color selection = { 0.20f, 0.40f, 0.78f, 0.75f };
    Color inactiveSelection = { 0.28f, 0.31f, 0.38f, 0.55f };
    Color caret = { 0.95f, 0.97f, 1.0f, 1.0f };
    Color preedit = { 0.50f, 0.72f, 1.0f, 1.0f };
    Insets padding = { 8.0f, 4.0f, 8.0f, 4.0f };
};

struct EditBoxPaintFlags{
    bool enabled = true;
    bool hovered = false;
    bool focused = false;
    bool readOnly = false;
    bool caretVisible = true;
    bool preeditCaretVisible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns this declaration's display bytes and immutable font versions; the edit model is borrowed only during snapshot().
// Hard-line LTR geometry interpolates grapheme caret stops inside ligatures; it does not provide wrap or paragraph bidi.
class EditBoxView final{
public:
    explicit EditBoxView(Core::Alloc::GlobalArena& arena);
    EditBoxView(EditBoxView&&) = default;
    EditBoxView& operator=(EditBoxView&&) = default;
    EditBoxView(const EditBoxView&) = delete;
    EditBoxView& operator=(const EditBoxView&) = delete;


public:
    [[nodiscard]] bool snapshot(const EditModel& model);
    // request.text is supplied by the owned snapshot. Font size remains logical; TextService applies paint DPI.
    [[nodiscard]] TextLayoutStatus::Enum shape(TextService& text, ShapeRequest request = {});
    // Admits matching source bytes and mode-appropriate hard lines with LTR edges. Failure preserves prior geometry.
    [[nodiscard]] bool adoptLayout(TextLayout&& layout);
    [[nodiscard]] bool arrange(const Rect& bounds, const Insets& padding, const Rect& clip,
        f32 previousScroll, EditBoxPlacement& output, f32 caretWidth = 1.0f)const;
    [[nodiscard]] bool arrange(const Rect& bounds, const Insets& padding, const Rect& clip,
        Point previousScroll, EditBoxPlacement& output, f32 caretWidth = 1.0f, bool revealCaret = true)const;
    [[nodiscard]] bool hitTest(Point point, const EditBoxPlacement& placement, usize& committedByte)const;
    [[nodiscard]] bool paint(TextService& text, PaintBuilder& paint, const UiSkin& skin,
        const EditBoxPlacement& placement, const EditBoxStyle& style = {}, const EditBoxPaintFlags& flags = {})const;


public:
    [[nodiscard]] StringView displayText()const{ return { m_display.data(), m_display.size() }; }
    [[nodiscard]] const TextLayout& layout()const{ return m_geometry.layout(); }
    [[nodiscard]] const PaintVector<EditBoxCaretStop>& caretStops()const{ return m_geometry.caretStops(); }
    [[nodiscard]] const EditCaretGeometry& caretGeometry()const{ return m_geometry; }
    [[nodiscard]] EditTextMode::Enum textMode()const{ return m_textMode; }
    [[nodiscard]] u64 revision()const{ return m_revision; }
    [[nodiscard]] usize committedBytes()const{ return m_committedBytes; }
    [[nodiscard]] usize displayCaret()const{ return m_caret; }
    [[nodiscard]] EditBoxRange selectionRange()const{ return m_selection; }
    [[nodiscard]] EditBoxRange preeditRange()const{ return m_preedit; }
    [[nodiscard]] EditBoxRange replacementRange()const{ return m_replacement; }
    [[nodiscard]] bool composing()const{ return m_composing; }
    [[nodiscard]] bool ready()const{ return m_ready; }


private:
    NotNull<Core::Alloc::GlobalArena*> m_arena;
    AString<Core::Alloc::GlobalArena> m_display;
    PaintVector<EditCaretMapping> m_mapping;
    EditCaretGeometry m_geometry;
    EditBoxRange m_selection;
    EditBoxRange m_preedit;
    EditBoxRange m_replacement;
    usize m_caret = 0u;
    usize m_committedBytes = 0u;
    u64 m_revision = 0u;
    EditTextMode::Enum m_textMode = EditTextMode::SingleLine;
    bool m_composing = false;
    bool m_ready = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

