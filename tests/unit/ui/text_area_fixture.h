// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/text_area.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTextAreaTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiComboTests;

inline constexpr AStringView s_TextAreaPanelKey = "panel";
inline constexpr AStringView s_TextAreaKey = "area";

namespace AreaEventKind{
    enum Enum : u8{ Text, Key, Action, Selection };
};

struct AreaEvent{
    AString<Core::Alloc::GlobalArena> text;
    usize anchor = 0u;
    usize caret = 0u;
    InputCommandIntent intent;
    EditAction::Enum action = EditAction::Submit;
    AreaEventKind::Enum kind = AreaEventKind::Text;


public:
    explicit AreaEvent(Core::Alloc::GlobalArena& arena)noexcept : text(arena){}
};

struct AreaPublication{
    AString<Core::Alloc::GlobalArena> text;
    EditBoxPlacement placement;
    PopupToken popup;


public:
    explicit AreaPublication(Core::Alloc::GlobalArena& arena)noexcept : text(arena){}
};

struct AreaResolution{
    AString<Core::Alloc::GlobalArena> text;
    EditNavigationSnapshot preferred;
    Expected<EditNavigationResult> result = MakeUnexpected(Failure{});
    usize caret = 0u;


public:
    explicit AreaResolution(Core::Alloc::GlobalArena& arena)noexcept : text(arena){}
};

class TextAreaHost final : public IEditBoxHost{
public:
    TextAreaHost(Core::Alloc::GlobalArena& arena, const Context& context);
    virtual ~TextAreaHost()noexcept override = default;


public:
    void text(AStringView value);
    void key(i32 value, b8 control = false, b8 shift = false);
    void selection(usize anchor, usize caret);
    void action(EditAction::Enum value);

    [[nodiscard]] virtual EditBoxResult edit(const WidgetState& widget, const EditBoxOptions& options, EditModel& model)noexcept override;
    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, const EditBoxOptions&, const PopupToken&, EditModel&)noexcept override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState& widget, const EditBoxOptions& options, const PopupToken& popup, EditModel& model, IEditActionSink& sink)noexcept override;
    [[nodiscard]] virtual EditBoxResult editNavigated(
        const WidgetState& widget,
        const EditBoxOptions& options,
        const PopupToken& popup,
        EditModel& model,
        EditNavigationState& navigation,
        IEditNavigationResolver& resolver,
        IEditActionSink& sink)override;
    [[nodiscard]] virtual b8 publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, const EditBoxOptions& options)override;


private:
    [[nodiscard]] b8 applyAction(EditAction::Enum action, b8 readOnly, EditModel& model, EditNavigationState& navigation, IEditActionSink& sink, EditBoxResult& result);


public:
    PaintVector<AreaPublication> publications;
    PaintVector<AreaResolution> resolutions;
    PaintVector<AString<Core::Alloc::GlobalArena>> actionTexts;
    Function<void()> loanHook;
    Function<void()> publishHook;
    WidgetId loanHookWidget;
    WidgetId publishHookWidget;
    PopupToken lastPopup;
    usize loans = 0u;
    usize ordinaryLoans = 0u;
    usize actionLoans = 0u;
    usize publishes = 0u;
    f32 viewportHeight = 80.0f;
    f32 seededColumn = 0.0f;
    b8 seedColumn = false;
    b8 focused = true;


private:
    Core::Alloc::GlobalArena& m_arena;
    const Context& m_context;
    PaintVector<AreaEvent> m_events;
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class TextAreaFixture : public WidgetFixture{
public:
    TextAreaFixture();


protected:
    [[nodiscard]] b8 beginArea(const u64 generation, const Rect& bounds = { 20.0f, 20.0f, 740.0f, 520.0f }){
        return begin(generation) && m_builder.beginPanel(s_TextAreaPanelKey, bounds);
    }
    [[nodiscard]] b8 acceptArea(){ return finishPanel() && m_context.commitFrame(m_context.readyGeneration()); }
    [[nodiscard]] b8 frameArea(u64 generation, const TextAreaOptions& options = {}, const Rect& bounds = { 20.0f, 20.0f, 740.0f, 520.0f });
    void useHost()noexcept{ m_builder.setEditHost(&m_host); }


protected:
    EditModel m_model;
    TextAreaState m_state;
    TextAreaHost m_host;
    EditBoxResult m_result;
};

class TextAreaLoanFixture : public TextAreaFixture{
protected:
    [[nodiscard]] static TextAreaOptions AreaOptions()noexcept;


protected:
    [[nodiscard]] b8 acceptBaseline();
    [[nodiscard]] b8 beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup(s_ParentPopupKey, m_parent, UiWidgetTests::LargeLoanParentOptions());
    }
    [[nodiscard]] b8 finishRoot(){ return m_context.endRoot() && m_context.finishFrame(); }
    [[nodiscard]] UiWidgetTests::AcceptedInputState<16u> accepted()const noexcept{ return UiWidgetTests::CaptureAcceptedInput<16u>(m_context.input()); }
    void expectRejected(const UiWidgetTests::AcceptedInputState<16u>& frame);


protected:
    PopupState m_parent;
    PopupState m_child;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

