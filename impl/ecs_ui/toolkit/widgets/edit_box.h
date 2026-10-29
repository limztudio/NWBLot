// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/widgets/style.adhd.h>
#include "edit_caret_geometry.h"

#include <impl/assets_ui_skin/region_names.h>
#include <impl/ecs_ui/toolkit/edit/model.h>
#include <impl/ecs_ui/toolkit/edit/grapheme.h>
#include <impl/ecs_ui/toolkit/text/service.h>


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
    f32 scroll = 0.0f;
    f32 scrollY = 0.0f;
};

inline constexpr Color s_EditBoxSelectionColor = { 0.20f, 0.40f, 0.78f, 0.75f };
inline constexpr Color s_EditBoxInactiveSelectionColor = { 0.28f, 0.31f, 0.38f, 0.55f };
inline constexpr Color s_EditBoxCaretColor = { 0.95f, 0.97f, 1.0f, 1.0f };
inline constexpr Color s_EditBoxPreeditColor = { 0.50f, 0.72f, 1.0f, 1.0f };

struct EditBoxStyle{
    Name normal = UiSkinToolkitRegions::s_EditNormalRegionName;
    Name hover = UiSkinToolkitRegions::s_EditHoverRegionName;
    Name focused = UiSkinToolkitRegions::s_EditFocusedRegionName;
    Name disabled = UiSkinToolkitRegions::s_EditDisabledRegionName;
    Name fallback = UiSkinToolkitRegions::s_ButtonNormalRegionName;
    Name focus = UiSkinToolkitRegions::s_FocusOverlayRegionName;
    Color background = s_StyleInsetColor;
    Color text = s_StyleTextColor;
    Color disabledText = s_StyleDisabledTextColor;
    Color selection = s_EditBoxSelectionColor;
    Color inactiveSelection = s_EditBoxInactiveSelectionColor;
    Color caret = s_EditBoxCaretColor;
    Color preedit = s_EditBoxPreeditColor;
    Insets padding = { s_StyleSpacingMedium, s_StyleSpacingSmall, s_StyleSpacingMedium, s_StyleSpacingSmall };
};

struct EditBoxPaintFlags{
    b8 enabled = true;
    b8 hovered = false;
    b8 focused = false;
    b8 readOnly = false;
    b8 caretVisible = true;
    b8 preeditCaretVisible = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Owns this declaration's display bytes and immutable font versions; the edit model is borrowed only during snapshot().
// Hard-line LTR geometry interpolates grapheme caret stops inside ligatures; it does not provide wrap or paragraph bidi.
class EditBoxView final{
public:
    explicit EditBoxView(Core::Alloc::GlobalArena& arena)noexcept;
    EditBoxView(EditBoxView&&)noexcept = default;
    EditBoxView(const EditBoxView&) = delete;


public:
    EditBoxView& operator=(EditBoxView&&)noexcept = default;

    EditBoxView& operator=(const EditBoxView&) = delete;


public:
    [[nodiscard]] b8 snapshot(const EditModel& model);

    // request.text is supplied by the owned snapshot. Font size remains logical; TextService applies paint DPI.
    [[nodiscard]] TextLayoutStatus::Enum shape(TextService& text, ShapeRequest request = {});

    // Admits matching source bytes and mode-appropriate hard lines with LTR edges. Failure preserves prior geometry.
    [[nodiscard]] b8 adoptLayout(TextLayout&& layout);
    // Shape at display scale before arranging; measurement keeps line, caret, hit-test, composition coords.
    [[nodiscard]] b8 measure(TextService& text, const DisplayMetrics& display);
    [[nodiscard]] Expected<EditBoxPlacement> arrange(const Rect& bounds, const Insets& padding, const Rect& clip, Point previousScroll, f32 caretWidth = 1.0f, b8 revealCaret = true)const noexcept;
    // Uses a caller-reserved content viewport, such as a two-axis scrollbar layout.
    [[nodiscard]] Expected<EditBoxPlacement> arrangeViewport(const Rect& bounds, const Rect& viewport, const Rect& clip, Point previousScroll, f32 caretWidth = 1.0f, b8 revealCaret = true)const noexcept;
    [[nodiscard]] Expected<usize> hitTest(Point point, const EditBoxPlacement& placement)const noexcept;

    [[nodiscard]] b8 paint(TextService& text, PaintBuilder& paint, const UiSkin& skin, const EditBoxPlacement& placement, const EditBoxStyle& style = {}, const EditBoxPaintFlags& flags = {})const;

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

