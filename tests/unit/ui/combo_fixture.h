// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiWidgetTests;


// Keys remain stable through reorder and removal; the large source needs no row allocation.
class ComboSource : public IListDataSource{
public:
    virtual ~ComboSource()override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return generation; }
    [[nodiscard]] virtual u64 revision()const override{ return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const override{ return count; }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        ++keyCalls;
        return rawKey(index);
    }

    [[nodiscard]] virtual Expected<u64> indexOf(const u64 value)const override{
        u64 index = 0u;
        ++lookupCalls;
        if(value == 0u || value == removed || value > count + (removed != 0u ? 1u : 0u))
            return MakeUnexpected(Failure{});
        const u64 forward = value - 1u - (removed != 0u && value > removed ? 1u : 0u);
        index = reverse ? count - 1u - forward : forward;
        return index < count ? Expected<u64>{ index } : MakeUnexpected(Failure{});
    }

    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const bool backwards)const override{
        ++searchCalls;
        return UiWidgetTests::FindSingleDisabledFixtureRow(count, start, backwards, disabled, [this](const u64 index){ return rawKey(index); });
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        ++textCalls;
        return index % 2u == 0u ? "First" : "Second";
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        ++enabledCalls;
        return index < count && rawKey(index) != disabled;
    }

    void resetCounters()const{
        keyCalls = 0u;
        lookupCalls = 0u;
        searchCalls = 0u;
        textCalls = 0u;
        enabledCalls = 0u;
    }


protected:
    [[nodiscard]] u64 rawKey(const u64 index)const{
        const u64 forward = reverse ? count - 1u - index : index;
        const u64 value = forward + 1u;
        return value + (removed != 0u && value >= removed ? 1u : 0u);
    }


public:
    u64 count = 100000u;
    u64 generation = 701u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    bool reverse = false;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 searchCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable u64 enabledCalls = 0u;
};

[[nodiscard]] inline ComboOptions Options(){
    ComboOptions options;
    options.width = { LayoutSizePolicy::Fixed, 220.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    options.popupHeight = 144.0f;
    options.placeholder = "Choose";
    return options;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComboFixture : public WidgetFixture{
protected:
    [[nodiscard]] bool declareCombo(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = { 10.0f, 10.0f, 320.0f, 240.0f }){
        return declareSource(generation, m_source, m_state, options, bounds);
    }

    [[nodiscard]] bool declareSource(const u64 generation, const IListDataSource& source, ComboState& state, const ComboOptions& options = Options(), const Rect& bounds = { 10.0f, 10.0f, 320.0f, 240.0f }){
        if(!begin(generation) || !m_builder.beginPanel("panel", bounds))
            return false;
        m_result = m_builder.comboBox("combo", source, state, options);
        return m_result.valid;
    }

    [[nodiscard]] bool prepare(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = { 10.0f, 10.0f, 320.0f, 240.0f }){
        return declareCombo(generation, options, bounds) && finishPanel();
    }

    [[nodiscard]] bool accept(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = { 10.0f, 10.0f, 320.0f, 240.0f }){
        return prepare(generation, options, bounds) && m_context.commitFrame(generation);
    }

    [[nodiscard]] WidgetId host(const AStringView key = "combo")const{ return id(key, "panel"); }
    [[nodiscard]] WidgetId popup(const AStringView key = "combo")const{ return MakeWidgetId(host(key), "popup"); }
    [[nodiscard]] WidgetId list(const AStringView key = "combo")const{ return MakeWidgetId(host(key), "rows"); }

    [[nodiscard]] WidgetId row(const u64 key, const AStringView combo = "combo")const{
        return MakeWidgetPartId(MakeWidgetId(list(combo), "rows"), key);
    }

    using WidgetFixture::press;

    [[nodiscard]] bool openByPointer(const u64 generation){
        const HitTarget* field = target(host());
        if(!field)
            return false;
        click(Center(field->rectangle));
        return accept(generation) && m_state.isOpen();
    }


protected:
    ComboSource m_source;
    ComboState m_state;
    ComboResult m_result;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

