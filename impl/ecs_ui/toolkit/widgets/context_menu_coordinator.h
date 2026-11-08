// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class ContextMenuCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using ListFrame = BuilderScopeFrame::ListFrame;
    using ContextMenuFrame = BuilderScopeFrame::ContextMenuFrame;


public:
    explicit ContextMenuCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] ContextMenuResult declareMenu(AStringView stableKey, AStringView anchorKey, const IListDataSource& source, ContextMenuState& state, const ContextMenuOptions& options);
    [[nodiscard]] b8 paintMenus();


private:
    [[nodiscard]] b8 prepareContextMenu(ContextMenuFrame& frame, ContextMenuResult& result);
    [[nodiscard]] b8 applyContextMenuInput(ContextMenuFrame& frame, ContextMenuResult& result);
    void snapshotContextMenu(ContextMenuFrame& frame)noexcept;
    [[nodiscard]] b8 paintContextMenuPopup(ContextMenuFrame& frame);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

