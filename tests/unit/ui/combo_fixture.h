// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "widget_fixture.h"
#include "widget_id_assertion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiWidgetTests;

inline constexpr Rect s_ComboPanelBounds{ 10.0f, 10.0f, 320.0f, 240.0f };
inline constexpr AStringView s_ComboPanelKey = "panel";
inline constexpr AStringView s_ComboKey = "combo";
inline constexpr AStringView s_SecondComboKey = "other";
// The combo popup list part and the list's own row container share the "rows" text.
inline constexpr AStringView s_ComboRowsKey = "rows";
inline constexpr AStringView s_ListRowsKey = "rows";


// Arithmetic combo source with stable keys across reorder and removal.
class ComboSource : public IListDataSource{
public:
    virtual ~ComboSource()noexcept override = default;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ observeCallback(); return generation; }
    [[nodiscard]] virtual u64 revision()const override{ observeCallback(); return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const noexcept override{ observeCallback(); return count; }
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 value)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 backwards)const noexcept override;
    [[nodiscard]] virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual b8 enabled(u64 index)const noexcept override;
    void resetCounters()const noexcept;


protected:
    virtual void observeCallback()const noexcept{}
    [[nodiscard]] u64 rawKey(const u64 index)const noexcept{
        return UiWidgetTests::ArithmeticFixtureRowKey(count, removed, reverse, index);
    }


public:
    u64 count = 100000u;
    u64 generation = 701u;
    u64 contentRevision = 1u;
    u64 removed = 0u;
    u64 disabled = 0u;
    mutable u64 keyCalls = 0u;
    mutable u64 lookupCalls = 0u;
    mutable u64 searchCalls = 0u;
    mutable u64 textCalls = 0u;
    mutable u64 enabledCalls = 0u;
    b8 reverse = false;
};

[[nodiscard]] ComboOptions Options()noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ComboFixture : public WidgetFixture{
protected:
    using WidgetFixture::press;


protected:
    [[nodiscard]] b8 declareCombo(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = s_ComboPanelBounds){
        return declareSource(generation, m_source, m_state, options, bounds);
    }
    [[nodiscard]] b8 declareSource(u64 generation, const IListDataSource& source, ComboState& state, const ComboOptions& options = Options(), const Rect& bounds = s_ComboPanelBounds);
    [[nodiscard]] b8 prepare(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = s_ComboPanelBounds){
        return declareCombo(generation, options, bounds) && finishPanel();
    }
    [[nodiscard]] b8 accept(const u64 generation, const ComboOptions& options = Options(), const Rect& bounds = s_ComboPanelBounds){
        return prepare(generation, options, bounds) && m_context.commitFrame(generation);
    }
    [[nodiscard]] WidgetId popup(const AStringView key = s_ComboKey)const{ return UiTestSupport::RequireWidgetId(id(key, s_ComboPanelKey), "popup"); }
    [[nodiscard]] WidgetId list(const AStringView key = s_ComboKey)const{ return UiTestSupport::RequireWidgetId(id(key, s_ComboPanelKey), s_ComboRowsKey); }
    [[nodiscard]] WidgetId row(const u64 key, const AStringView combo = s_ComboKey)const{
        return UiTestSupport::RequireWidgetPartId(UiTestSupport::RequireWidgetId(list(combo), s_ListRowsKey), key);
    }
    [[nodiscard]] b8 openByPointer(u64 generation);


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

