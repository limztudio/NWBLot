// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"
#include "widget_id_assertion.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSearchComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiComboTests;

inline constexpr AStringView s_CallbackQuery = "callback";
inline constexpr AStringView s_EmptyResultQuery = "None";
inline constexpr AStringView s_SecondHalfQuery = "Second";
inline constexpr AStringView s_PreeditText = "preedit";

namespace FilterMode{
    enum Enum : u8{ All, Second, Empty };
};

class FilterView final : public IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return generation; }
    [[nodiscard]] virtual u64 revision()const noexcept override{ return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const noexcept override;
    [[nodiscard]] virtual u64 key(u64 index)const noexcept override;
    [[nodiscard]] virtual Expected<u64> indexOf(u64 value)const noexcept override;
    [[nodiscard]] virtual Expected<u64> findEnabled(u64 start, b8 reverse)const override;
    [[nodiscard]] virtual StringView text(u64 index)const override;
    [[nodiscard]] virtual b8 enabled(u64 index)const override;


public:
    const ComboSource* full = nullptr;
    u64 generation = 1701u;
    u64 contentRevision = 1u;
    mutable EditModel* textMutation = nullptr;
    mutable EditModel* enabledMutation = nullptr;
    FilterMode::Enum mode = FilterMode::All;
};

class SearchSource final : public ISearchableListDataSource{
public:
    explicit SearchSource(Core::Alloc::GlobalArena& arena)noexcept;


public:
    [[nodiscard]] virtual u64 instanceGeneration()const noexcept override{ return full.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const noexcept override{ return full.revision(); }
    [[nodiscard]] virtual u64 rowCount()const noexcept override{ return full.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const noexcept override{ return full.key(index); }
    [[nodiscard]] virtual Expected<u64> indexOf(const u64 key)const noexcept override{ return full.indexOf(key); }
    [[nodiscard]] virtual Expected<u64> findEnabled(const u64 start, const b8 reverse)const noexcept override{ return full.findEnabled(start, reverse); }
    [[nodiscard]] virtual StringView text(const u64 index)const noexcept override{ return full.text(index); }
    [[nodiscard]] virtual b8 enabled(u64 index)const override;
    [[nodiscard]] virtual b8 filter(AStringView query)override;
    [[nodiscard]] virtual const IListDataSource& filtered()const noexcept override{ return alternateView ? alternate : view; }


public:
    ComboSource full;
    FilterView view;
    FilterView alternate;
    EditModel* filterMutation = nullptr;
    mutable EditModel* fullMutation = nullptr;
    u64 filterCalls = 0u;
    b8 alternateView = false;


private:
    AString<Core::Alloc::GlobalArena> m_query;
};

class SearchEditHost final : public IEditBoxHost{
public:
    explicit SearchEditHost(Context& context)noexcept : m_context(context){}


public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState& widget, const EditBoxOptions& options, EditModel& model)noexcept override;
    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState& widget, const EditBoxOptions& options, const PopupToken& popup, EditModel& model)override;
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, const EditBoxOptions&, const PopupToken&, EditModel&, IEditActionSink&)noexcept override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, const EditBoxOptions&, const PopupToken&, EditModel&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)noexcept override{ return {}; }
    [[nodiscard]] virtual b8 publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, const EditBoxOptions& options)override;
    void text(AStringView value)noexcept;


public:
    PopupToken lastPopup;
    PopupToken loanContextPopup;
    PopupToken publishContextPopup;
    EditModel* publishMutation = nullptr;
    u64 loans = 0u;
    u64 contextScopedLoans = 0u;
    u64 publications = 0u;
    b8 submitted = false;
    b8 cancelled = false;
    b8 preedit = false;
    b8 publishedComposing = false;


private:
    Context& m_context;
    AStringView m_nextText;
    b8 m_changeText = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SearchFixture : public ComboFixture{
protected:
    [[nodiscard]] static SearchComboOptions Options()noexcept;


public:
    SearchFixture();


protected:
    virtual void SetUp()override;
    [[nodiscard]] b8 declareSearch(u64 generation);
    [[nodiscard]] b8 prepareSearch(const u64 generation){ return declareSearch(generation) && finishPanel(); }
    [[nodiscard]] b8 acceptSearch(const u64 generation){ return prepareSearch(generation) && m_context.commitFrame(generation); }
    [[nodiscard]] WidgetId query(const AStringView key = s_ComboKey)const{ return UiTestSupport::RequireWidgetId(id(key, s_ComboPanelKey), "query"); }
    [[nodiscard]] b8 openSearch(u64 generation);


protected:
    SearchSource m_searchSource;
    SearchComboState m_search;
    SearchEditHost m_host;
    SearchComboResult m_searchResult;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

