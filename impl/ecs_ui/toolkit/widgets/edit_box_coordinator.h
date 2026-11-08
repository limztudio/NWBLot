// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class EditBoxCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using IntegerEditFrame = BuilderScopeFrame::IntegerEditFrame;
    using FloatEditFrame = BuilderScopeFrame::FloatEditFrame;


public:
    explicit EditBoxCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] EditBoxResult declareEditBox(AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame);
    [[nodiscard]] b8 prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item);
    [[nodiscard]] b8 paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

