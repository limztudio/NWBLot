// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ComboCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ComboFrame = BuilderScopeFrame::ComboFrame;


public:
    explicit ComboCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ComboResult declareCombo(AStringView stableKey, const IListDataSource& source, const ComboOptions& options, const SearchComboOptions* searchOptions, ComboState& state, SearchComboState* search, ISearchableListDataSource* searchSource);
    [[nodiscard]] SearchComboResult searchComboBox(AStringView stableKey, ISearchableListDataSource& source, SearchComboState& state, const SearchComboOptions& options);
    [[nodiscard]] b8 paintCombo(const Item& item, const LayoutBox& box);
    [[nodiscard]] b8 paintCombos();


private:
    [[nodiscard]] b8 paintComboPopup(ComboFrame& frame);
    [[nodiscard]] b8 applyComboInput(const WidgetState& field, ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 applyComboListInput(ComboFrame& frame, ComboResult& result);
    [[nodiscard]] b8 prepareComboSearch(const WidgetState& field, ComboFrame& frame);
    void snapshotComboQuery(ComboFrame& frame)noexcept;
    [[nodiscard]] b8 paintComboQuery(ComboFrame& frame, LayoutBox& content);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

