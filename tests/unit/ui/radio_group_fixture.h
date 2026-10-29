// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"

#include <impl/ecs_ui/toolkit/widgets/radio_group.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiRadioGroupTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiWidgetTests;

inline constexpr AStringView s_RadioPanelKey = "panel";
inline constexpr AStringView s_RadioGroupKey = "radio";

namespace RadioCallbackSite{
    enum Enum : u8{ None, Instance, Revision, Count, Key, Text, Enabled };
};

namespace RadioMutation{
    enum Enum : u8{ None, Select, SelectAwayBack, Revision, Replacement, ResetBuilder, PanelBuilder, ClosePopup };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Radio group source with expiring label views and reentrant callback hooks.
class RadioSource final : public IListDataSource{
public:
    virtual ~RadioSource()noexcept override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override;
    [[nodiscard]] virtual u64 revision()const override;
    [[nodiscard]] virtual u64 rowCount()const override;
    [[nodiscard]] virtual u64 key(u64 index)const override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 value)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 reverse)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual b8 enabled(u64 index)const override;
    void resetCounters()const noexcept;
    void armSelection(RadioGroupState& state, RadioCallbackSite::Enum site, u64 keyValue, b8 awayAndBack = false)noexcept;
    void armSource(RadioCallbackSite::Enum site, b8 replacement = false)noexcept;
    void armReentry(Builder& builder, RadioCallbackSite::Enum site, b8 panel = false)noexcept;
    void armClose(PopupState& popup, RadioCallbackSite::Enum site)noexcept;


private:
    [[nodiscard]] u64 rawCount()const noexcept;
    [[nodiscard]] u64 rawKey(u64 index)const noexcept;
    void callback(RadioCallbackSite::Enum site)const;


public:
    mutable u64 m_generation = 1801u;
    mutable u64 m_contentRevision = 1u;
    u64 m_count = 5u;
    u64 m_removed = 0u;
    u64 m_disabled = 30u;
    mutable u64 m_calls = 0u;
    mutable u64 m_textCalls = 0u;
    mutable u64 m_mutations = 0u;
    mutable u64 m_callbacksAfterMutation = 0u;


private:
    RadioGroupState* m_state = nullptr;
    Builder* m_builder = nullptr;
    PopupState* m_popup = nullptr;
    u64 m_selected = 0u;
    mutable RadioCallbackSite::Enum m_site = RadioCallbackSite::None;
    RadioMutation::Enum m_mutation = RadioMutation::None;
    mutable Array<char, 4u> m_label{};


public:
    b8 m_reverse = false;
    b8 m_duplicate = false;
    b8 m_allDisabled = false;
    mutable b8 m_beginAccepted = false;
    mutable b8 m_endAccepted = false;
};

struct RadioAcceptedFrame{
    Array<HitTarget, 128u> targets{};
    usize count = 0u;
    usize popupCount = 0u;
    u64 generation = 0u;
    WidgetId focus;
    WidgetId capture;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RadioGroupFixture : public WidgetFixture{
protected:
    using WidgetFixture::press;


protected:
    virtual void SetUp()override;
    void configureRadioSkin(b8 fallbackOnly = false);
    [[nodiscard]] b8 declare(const u64 generation, const RadioGroupOptions& options = {}, const Rect& bounds = { 10.0f, 10.0f, 340.0f, 320.0f });
    [[nodiscard]] b8 declareSource(u64 generation, const IListDataSource& source, RadioGroupState& state, const RadioGroupOptions& options = {}, const Rect& bounds = { 10.0f, 10.0f, 340.0f, 320.0f });
    [[nodiscard]] b8 prepare(u64 generation, const RadioGroupOptions& options = {}, const Rect& bounds = { 10.0f, 10.0f, 340.0f, 320.0f });
    [[nodiscard]] b8 accept(u64 generation, const RadioGroupOptions& options = {}, const Rect& bounds = { 10.0f, 10.0f, 340.0f, 320.0f });
    [[nodiscard]] WidgetId host()const;
    [[nodiscard]] const RadioGroupChoicePlacement* choice(u64 keyValue, const RadioGroupState& state)const noexcept;
    [[nodiscard]] Point choicePoint(u64 keyValue)const noexcept;
    [[nodiscard]] RadioAcceptedFrame accepted()const noexcept;
    void expectAccepted(const RadioAcceptedFrame& saved, b8 focus = true, b8 popupScopes = true)const;
    [[nodiscard]] usize regionQuads(const DrawSnapshot& snapshot, const Name& name)const noexcept;


protected:
    RadioSource m_source;
    RadioGroupState m_state;
    RadioGroupResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

