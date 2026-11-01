// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/edit/commands.h>
#include <impl/ecs_ui/toolkit/input/bindings.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiNumericEditTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiComboTests;

inline constexpr AStringView s_NumericPanelKey = "panel";
inline constexpr AStringView s_IntegerEditKey = "integer";
inline constexpr AStringView s_DecimalEditKey = "decimal";

namespace NumericEventKind{
    enum Enum : u8{ Text, Key, Preedit, Action };
};

struct NumericEvent{
    AString<Core::Alloc::GlobalArena> text;
    InputCommandIntent intent;
    EditAction::Enum action = EditAction::Submit;
    NumericEventKind::Enum kind = NumericEventKind::Text;


public:
    explicit NumericEvent(Core::Alloc::GlobalArena& arena)noexcept : text(arena){}
};

class NumericHost final : public IEditBoxHost{
public:
    explicit NumericHost(Core::Alloc::GlobalArena& arena);
    virtual ~NumericHost()noexcept override = default;


public:
    void text(AStringView value);
    void key(i32 key, b8 control = false);
    void replace(AStringView value);
    void preedit(AStringView value);
    void action(EditAction::Enum action);

    [[nodiscard]] virtual EditBoxResult edit(const WidgetState& widget, const EditBoxOptions& options, EditModel& draft)noexcept override;
    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState&, const EditBoxOptions&, const PopupToken&, EditModel&)noexcept override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState& widget, const EditBoxOptions& options, const PopupToken& popup, EditModel& draft, IEditActionSink& sink)override;
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, const EditBoxOptions&, const PopupToken&, EditModel&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)noexcept override{ return {}; }
    [[nodiscard]] virtual b8 publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, const EditBoxOptions& options)override;


public:
    AString<Core::Alloc::GlobalArena> displayed;
    IntegerEditModel* onLoanInteger = nullptr;
    IntegerEditModel* onPublishInteger = nullptr;
    FloatEditModel* onPublishFloat = nullptr;
    Builder* resetBuilder = nullptr;
    PopupToken lastPopup;
    i64 mutationValue = 99;
    usize loans = 0u;
    usize ordinaryLoans = 0u;
    usize publishes = 0u;
    b8 focused = true;
    b8 mutationApplied = false;


private:
    Core::Alloc::GlobalArena& m_arena;
    PaintVector<NumericEvent> m_events;
    InputBindings m_bindings;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NumericFixture : public WidgetFixture{
public:
    NumericFixture();


protected:
    [[nodiscard]] b8 beginNumeric(const u64 generation){
        return begin(generation) && m_builder.beginPanel(s_NumericPanelKey, { 20.0f, 20.0f, 740.0f, 520.0f });
    }
    [[nodiscard]] b8 beginParent(const u64 generation){
        return begin(generation) && m_builder.beginPopup("parent", m_parent, UiWidgetTests::LargeLoanParentOptions());
    }
    [[nodiscard]] b8 acceptNumeric(){
        return finishPanel() && m_context.commitFrame(m_context.readyGeneration());
    }
    void useHost()noexcept{ m_builder.setEditHost(&m_host); }


protected:
    IntegerEditModel m_integer;
    FloatEditModel m_float;
    EditBoxState m_integerState;
    EditBoxState m_floatState;
    PopupState m_parent;
    PopupState m_child;
    NumericHost m_host;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

