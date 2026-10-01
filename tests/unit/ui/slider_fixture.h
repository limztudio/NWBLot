// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/slider.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSliderTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiWidgetTests;

namespace SliderCallbackMutation{
    enum Enum : u8{ None, SameValue, AwayAndBack, SetValue, ResetBuilder, PanelBuilder, ClosePopup };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A later deferred list label can mutate an earlier slider loan or try to reenter its finalizing Builder.
class SliderCallbackSource final : public UiComboTests::ComboSource{
public:
    SliderCallbackSource();


public:
    [[nodiscard]] virtual StringView text(u64 index)const override;
    void arm(SliderCallbackMutation::Enum mutation, SliderState* state = nullptr,
        Builder* builder = nullptr, PopupState* popup = nullptr);
    void clearCounters()const;


public:
    mutable u32 m_calls = 0u;
    mutable u32 m_mutations = 0u;
    mutable bool m_beginAccepted = false;
    mutable bool m_endAccepted = false;


private:
    mutable SliderCallbackMutation::Enum m_mutation = SliderCallbackMutation::None;
    SliderState* m_state = nullptr;
    Builder* m_builder = nullptr;
    PopupState* m_popup = nullptr;
};

struct SliderAcceptedFrame{
    Array<HitTarget, 64u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
    WidgetId focus;
    WidgetId capture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Point SliderCenter(const Rect& rectangle);
void ExpectSliderRect(const Rect& actual, const Rect& expected);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SliderFixture : public WidgetFixture{
protected:
    [[nodiscard]] static SliderOptions options();
    [[nodiscard]] static ListOptions siblingOptions();


protected:
    virtual void SetUp()override;
    void configureSliderSkin();
    [[nodiscard]] bool declare(u64 generation, const SliderOptions& settings = options(),
        const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool prepare(u64 generation, const SliderOptions& settings = options(),
        const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool accept(u64 generation, const SliderOptions& settings = options(),
        const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool sibling(AStringView key = "later");
    [[nodiscard]] WidgetId host()const;
    [[nodiscard]] WidgetId track()const;
    [[nodiscard]] WidgetId thumb()const;
    [[nodiscard]] Point trackPoint(f64 normalized, const SliderState& state)const;
    [[nodiscard]] Point thumbPoint()const;
    void press(Core::Key::Enum key, bool repeat = false);
    [[nodiscard]] SliderAcceptedFrame accepted()const;
    void expectAccepted(const SliderAcceptedFrame& saved, bool popupFocus = true)const;
    [[nodiscard]] usize regionQuads(const DrawSnapshot& snapshot, const Name& region)const;


protected:
    SliderState m_state;
    SliderCallbackSource m_laterSource;
    ListState m_laterState;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

