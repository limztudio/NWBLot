// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/slider.h>


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
    mutable b8 m_beginAccepted = false;
    mutable b8 m_endAccepted = false;


private:
    SliderState* m_state = nullptr;
    Builder* m_builder = nullptr;
    PopupState* m_popup = nullptr;
    mutable SliderCallbackMutation::Enum m_mutation = SliderCallbackMutation::None;
};

class SliderFixture : public WidgetFixture{
protected:
    using WidgetFixture::press;


protected:
    [[nodiscard]] static SliderOptions Options()noexcept;
    [[nodiscard]] static ListOptions SiblingOptions()noexcept;


protected:
    virtual void SetUp()override;
    void configureSliderSkin();
    [[nodiscard]] b8 declare(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] b8 prepare(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] b8 accept(u64 generation, const SliderOptions& settings = Options(), const Rect& bounds = { 10.0f, 10.0f, 360.0f, 240.0f });
    [[nodiscard]] b8 sibling(AStringView key = "later");
    [[nodiscard]] WidgetId host()const;
    [[nodiscard]] WidgetId track()const;
    [[nodiscard]] WidgetId thumb()const;
    [[nodiscard]] Point trackPoint(f64 normalized, const SliderState& state)const noexcept;
    [[nodiscard]] Point thumbPoint()const noexcept;
    [[nodiscard]] AcceptedInputState<64u> accepted()const noexcept{ return CaptureAcceptedInput<64u>(m_context.input()); }
    void expectAccepted(const AcceptedInputState<64u>& saved, const b8 popupFocus = true)const{
        ExpectAcceptedInput(m_context.input(), saved, false, popupFocus, popupFocus, true);
    }


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

