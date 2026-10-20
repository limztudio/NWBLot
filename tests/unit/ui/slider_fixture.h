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

inline constexpr AStringView s_SliderPanelKey = "panel";
inline constexpr AStringView s_SliderKey = "slider";

namespace SliderCallbackMutation{
    enum Enum : u8{ None, SameValue, AwayAndBack, SetValue, ResetBuilder, PanelBuilder, ClosePopup };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A later deferred list label can mutate an earlier slider loan or try to reenter its finalizing Builder.
class SliderCallbackSource final : public UiComboTests::ComboSource{
public:
    SliderCallbackSource()noexcept;


public:
    [[nodiscard]] virtual StringView text(u64 index)const override;
    void arm(SliderCallbackMutation::Enum mutation, SliderState* state = nullptr, Builder* builder = nullptr, PopupState* popup = nullptr)noexcept;
    void clearCounters()const noexcept;


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


class SliderFixture : public WidgetFixture{
protected:
    using WidgetFixture::press;


protected:
    [[nodiscard]] static SliderOptions Options()noexcept;
    [[nodiscard]] static ListOptions SiblingOptions()noexcept;


protected:
    virtual void SetUp()override;
    void configureSliderSkin();
    [[nodiscard]] bool declare(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool prepare(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool accept(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] bool sibling(AStringView key = "later");
    [[nodiscard]] WidgetId host()const;
    [[nodiscard]] WidgetId track()const;
    [[nodiscard]] WidgetId thumb()const;
    [[nodiscard]] Point trackPoint(f64 normalized, const SliderState& state)const noexcept;
    [[nodiscard]] Point thumbPoint()const noexcept;
    [[nodiscard]] SliderAcceptedFrame accepted()const noexcept;
    void expectAccepted(const SliderAcceptedFrame& saved, bool popupFocus = true)const;


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

