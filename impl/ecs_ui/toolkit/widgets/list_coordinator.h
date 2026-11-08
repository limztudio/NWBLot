// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ListCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ListRow = BuilderScopeFrame::ListRow;


public:
    explicit ListCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ListResult virtualList(AStringView stableKey, const IListDataSource& source, ListState& state, const ListOptions& options);
    [[nodiscard]] const UiSkinRegion* prepareListFrame(const IListDataSource& source, ListState& state, const ListOptions& options, const ControlToken& token, u64 rowCount, b8 focusOnCommit, WidgetId commandFocus, ListFrame& outFrame);
    [[nodiscard]] b8 paintList(const Item& item, const LayoutBox& box);


private:
    [[nodiscard]] b8 prepareListMetrics(ListFrame& frame);
    [[nodiscard]] b8 paintListRows(const Item& item, const ListFrame& frame, const ScrollPlacement& placement);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

