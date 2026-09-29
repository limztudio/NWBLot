// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "context.h"
#include "layout/tree.h"
#include "text/service.h"
#include "widgets/style.h"
#include "widgets/window.h"
#include "widgets/edit_box_state.h"
#include "widgets/popup.h"
#include "widgets/popup_style.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Transient declarations shape text, arrange a panel, then freeze paint and hit targets in the same order.
class Builder final : NoCopy{
private:
    struct Item{
        WidgetState state;
        TextLayout text;
        EditBoxView editView;
        EditBoxState* editState = nullptr;
        EditBoxOptions editOptions;
        EditBoxPaintFlags editFlags;
        Insets padding;
        f32 checkboxExtent = 0.0f;
        u32 node = 0u;
        bool enabled = true;
        bool checked = false;

        explicit Item(Core::Alloc::GlobalArena& arena)
            : text(arena)
            , editView(arena)
        {}
        Item(Item&&) = default;
        Item& operator=(Item&&) = default;
        Item(const Item&) = delete;
        Item& operator=(const Item&) = delete;
    };

    struct WindowFrame{
        WindowState* state = nullptr;
        WindowOptions options;
        WindowMetrics metrics;
        WidgetState titleState;
        WidgetState collapseState;
        WidgetState resizeState;
        TextLayout title;
        bool firstUse = false;

        explicit WindowFrame(Core::Alloc::GlobalArena& arena)
            : title(arena)
        {}
    };


public:
    Builder(Core::Alloc::GlobalArena& arena, Context& context, PaintBuilder& paint, TextService& text);


public:
    [[nodiscard]] bool beginPanel(AStringView stableKey, const Rect& bounds, LayoutDirection::Enum direction = LayoutDirection::Column);
    [[nodiscard]] bool endPanel();
    // Returns content visibility. A collapsed window still opens a scope and requires endWindow().
    // The borrowed WindowState must remain alive through that matching endWindow().
    [[nodiscard]] bool beginWindow(AStringView stableKey, StringView title, WindowState& state, const WindowOptions& options = {});
    [[nodiscard]] bool endWindow();
    // Popups begin after the preceding panel/window is balanced. Only a visible begin requires endPopup().
    [[nodiscard]] bool beginPopup(AStringView stableKey, PopupState& state, const PopupOptions& options = {});
    [[nodiscard]] bool endPopup();
    [[nodiscard]] bool beginRow(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] bool beginColumn(AStringView stableKey, const ContainerOptions& options = {});
    [[nodiscard]] bool endContainer();
    [[nodiscard]] bool label(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    // Return one action at most per declaration. Disabled controls discard stale activation and remain pointer barriers.
    [[nodiscard]] bool button(AStringView stableKey, StringView text, const WidgetOptions& options = {});
    [[nodiscard]] bool checkbox(AStringView stableKey, StringView text, bool& checked, const WidgetOptions& options = {});
    [[nodiscard]] bool separator(AStringView stableKey, const SeparatorOptions& options = {});
    [[nodiscard]] EditBoxResult editBox(AStringView stableKey, EditModel& model, EditBoxState& state, const EditBoxOptions& options = {});
    [[nodiscard]] bool balanced()const{ return !m_panelActive && !m_windowActive && !m_popupState; }
    void reset();
    void setSkin(const UiSkin& skin){ m_skin = &skin; }
    [[nodiscard]] WidgetStyle& style(){ return m_style; }
    [[nodiscard]] EditBoxStyle& editStyle(){ return m_editStyle; }
    [[nodiscard]] PopupStyle& popupStyle(){ return m_popupStyle; }
    void setEditHost(IEditBoxHost* host){ m_editHost = host; }
    void setDeltaSeconds(f32 delta){ m_deltaSeconds = delta; }
    // Valid while a window scope is open, including a collapsed window.
    [[nodiscard]] const WindowMetrics& windowMetrics()const{ return m_window.metrics; }


private:
    [[nodiscard]] bool beginContainer(AStringView stableKey, LayoutDirection::Enum direction, const ContainerOptions& options);
    [[nodiscard]] Item* addItem(AStringView stableKey, StringView text, WidgetKind::Enum kind, const WidgetOptions& options);
    [[nodiscard]] const UiSkinRegion* region(const Name& preferred, const Name& fallback)const;
    void buttonMetrics(Point& size, Insets& padding)const;
    [[nodiscard]] bool paintPanel();
    [[nodiscard]] bool paintWindow();
    [[nodiscard]] bool paintWindowTitle();
    [[nodiscard]] bool paintWindowResize();
    [[nodiscard]] bool paintItems();
    [[nodiscard]] bool paintItem(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintEditBox(const Item& item, const LayoutBox& box);
    [[nodiscard]] bool paintPopup();
    [[nodiscard]] bool synchronizePopup();
    [[nodiscard]] Rect visibleClip(const Rect& clip)const;


private:
    Core::Alloc::GlobalArena& m_arena;
    Context& m_context;
    PaintBuilder& m_paint;
    TextService& m_text;
    const UiSkin* m_skin = nullptr;
    WidgetStyle m_style;
    EditBoxStyle m_editStyle;
    PopupStyle m_popupStyle;
    PopupState* m_popupState = nullptr;
    PopupOptions m_popupOptions;
    PopupPlacement m_popupPlacement;
    IEditBoxHost* m_editHost = nullptr;
    f32 m_deltaSeconds = 0.0f;
    LayoutTree m_layout;
    PaintVector<Item> m_items;
    PaintVector<u32> m_stack;
    WindowFrame m_window;
    WidgetState m_panelState;
    Rect m_bounds;
    bool m_panelActive = false;
    bool m_windowActive = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

