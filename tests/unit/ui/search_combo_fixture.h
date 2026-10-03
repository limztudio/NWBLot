// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "combo_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiSearchComboTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Impl::Ui;
using namespace UiComboTests;

namespace FilterMode{
    enum Enum : u8{ All, Second, Empty };
};

class FilterView final : public IListDataSource{
public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return generation; }
    [[nodiscard]] virtual u64 revision()const override{ return contentRevision; }
    [[nodiscard]] virtual u64 rowCount()const override{
        return mode == FilterMode::Empty ? 0u : mode == FilterMode::Second ? full->count / 2u : full->count;
    }

    [[nodiscard]] virtual u64 key(const u64 index)const override{
        return mode == FilterMode::Second ? index * 2u + 2u : index + 1u;
    }

    [[nodiscard]] virtual bool indexOf(const u64 value, u64& index)const override{
        if(value == 0u || value > full->count || mode == FilterMode::Empty || (mode == FilterMode::Second && value % 2u != 0u))
            return false;
        index = mode == FilterMode::Second ? value / 2u - 1u : value - 1u;
        return true;
    }

    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        if(start >= rowCount())
            return false;
        index = start;
        if(enabled(index))
            return true;
        if(reverse){
            if(index == 0u)
                return false;
            --index;
        }
        else{
            if(index + 1u == rowCount())
                return false;
            ++index;
        }
        return enabled(index);
    }

    [[nodiscard]] virtual StringView text(const u64 index)const override{
        if(textMutation){
            EditModel* query = textMutation;
            textMutation = nullptr;
            if(!query->setText("callback"))
                ADD_FAILURE() << "callback query change failed";
        }
        return full->text(key(index) - 1u);
    }

    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        if(enabledMutation){
            EditModel* query = enabledMutation;
            enabledMutation = nullptr;
            if(!query->setText("None"))
                ADD_FAILURE() << "enabled callback query change failed";
        }
        return index < rowCount() && full->enabled(key(index) - 1u);
    }


public:
    const ComboSource* full = nullptr;
    u64 generation = 1701u;
    u64 contentRevision = 1u;
    FilterMode::Enum mode = FilterMode::All;
    mutable EditModel* textMutation = nullptr;
    mutable EditModel* enabledMutation = nullptr;
};

class SearchSource final : public ISearchableListDataSource{
public:
    explicit SearchSource(Core::Alloc::GlobalArena& arena)
        : m_query(arena)
    {
        view.full = &full;
        alternate.full = &full;
    }


public:
    [[nodiscard]] virtual u64 instanceGeneration()const override{ return full.instanceGeneration(); }
    [[nodiscard]] virtual u64 revision()const override{ return full.revision(); }
    [[nodiscard]] virtual u64 rowCount()const override{ return full.rowCount(); }
    [[nodiscard]] virtual u64 key(const u64 index)const override{ return full.key(index); }
    [[nodiscard]] virtual bool indexOf(const u64 key, u64& index)const override{ return full.indexOf(key, index); }
    [[nodiscard]] virtual bool findEnabled(const u64 start, const bool reverse, u64& index)const override{
        return full.findEnabled(start, reverse, index);
    }
    [[nodiscard]] virtual StringView text(const u64 index)const override{ return full.text(index); }
    [[nodiscard]] virtual bool enabled(const u64 index)const override{
        if(fullMutation){
            EditModel* query = fullMutation;
            fullMutation = nullptr;
            if(!query->setText("None"))
                ADD_FAILURE() << "full source callback query change failed";
        }
        return full.enabled(index);
    }

    [[nodiscard]] virtual bool filter(const AStringView query)override{
        ++filterCalls;
        if(filterMutation){
            EditModel* model = filterMutation;
            filterMutation = nullptr;
            if(!model->setText("callback"))
                return false;
        }
        const AStringView previous{ m_query.data(), m_query.size() };
        if(previous != query){
            m_query.assign(query.data(), query.size());
            ++view.contentRevision;
        }
        view.mode = query == "Second" ? FilterMode::Second : query == "None" ? FilterMode::Empty : FilterMode::All;
        return true;
    }

    [[nodiscard]] virtual const IListDataSource& filtered()const override{ return alternateView ? alternate : view; }


private:
    AString<Core::Alloc::GlobalArena> m_query;


public:
    ComboSource full;
    FilterView view;
    FilterView alternate;
    EditModel* filterMutation = nullptr;
    mutable EditModel* fullMutation = nullptr;
    u64 filterCalls = 0u;
    bool alternateView = false;
};

class SearchEditHost final : public IEditBoxHost{
public:
    explicit SearchEditHost(Context& context)
        : m_context(context)
    {}


public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState&, EditModel&, const EditBoxOptions&)override{
        ++contextScopedLoans;
        return {};
    }

    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState& widget, EditModel& model,
        const EditBoxOptions& options, const PopupToken& popup)override{
        ++loans;
        lastPopup = popup;
        loanContextPopup = m_context.popupToken();
        EditBoxResult result;
        result.valid = true;
        result.focused = options.enabled && m_context.input().focus() == widget.id;
        result.submitted = submitted;
        result.cancelled = cancelled;
        submitted = false;
        cancelled = false;
        if(changeText){
            result.valid = model.setText(nextText);
            result.textChanged = result.valid;
            changeText = false;
        }
        if(preedit){
            result.valid = result.valid && model.beginComposition() && model.updateComposition("preedit", 0u, 7u);
            preedit = false;
        }
        return result;
    }

    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState&, EditModel&, const EditBoxOptions&,
        const PopupToken&, IEditActionSink&)override{ return {}; }
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState&, EditModel&, const EditBoxOptions&,
        const PopupToken&, EditNavigationState&, IEditNavigationResolver&, IEditActionSink&)override{ return {}; }

    [[nodiscard]] virtual bool publish(const WidgetState&, const EditBoxView& view,
        const EditBoxPlacement&, const EditBoxOptions&)override{
        ++publications;
        publishContextPopup = m_context.popupToken();
        publishedComposing = view.composing();
        if(publishMutation){
            EditModel* query = publishMutation;
            publishMutation = nullptr;
            return query->setText("published");
        }
        return true;
    }

    void text(const AStringView value){ nextText = value; changeText = true; }


private:
    Context& m_context;
    AStringView nextText;
    bool changeText = false;


public:
    PopupToken lastPopup;
    PopupToken loanContextPopup;
    PopupToken publishContextPopup;
    EditModel* publishMutation = nullptr;
    u64 loans = 0u;
    u64 contextScopedLoans = 0u;
    u64 publications = 0u;
    bool submitted = false;
    bool cancelled = false;
    bool preedit = false;
    bool publishedComposing = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SearchFixture : public ComboFixture{
public:
    SearchFixture()
        : m_searchSource(m_arena)
        , m_search(m_arena)
        , m_host(m_context)
    {}


protected:
    virtual void SetUp()override{
        WidgetFixture::SetUp();
        m_builder.setEditHost(&m_host);
    }

    [[nodiscard]] static SearchComboOptions options(){
        SearchComboOptions value;
        value.combo = Options();
        value.combo.popupHeight = 220.0f;
        return value;
    }

    [[nodiscard]] bool declareSearch(const u64 generation){
        if(!begin(generation) || !m_builder.beginPanel("panel", { 10.0f, 10.0f, 320.0f, 280.0f }))
            return false;
        m_searchResult = m_builder.searchComboBox("combo", m_searchSource, m_search, options());
        return m_searchResult.combo.valid;
    }

    [[nodiscard]] bool prepareSearch(const u64 generation){ return declareSearch(generation) && finishPanel(); }
    [[nodiscard]] bool acceptSearch(const u64 generation){
        return prepareSearch(generation) && m_context.commitFrame(generation);
    }

    [[nodiscard]] WidgetId query(const AStringView key = "combo")const{ return MakeWidgetId(host(key), "query"); }

    [[nodiscard]] bool openSearch(const u64 generation){
        const HitTarget* field = target(host());
        if(!field)
            return false;
        click(Center(field->rectangle));
        return acceptSearch(generation) && m_search.combo().isOpen();
    }


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

