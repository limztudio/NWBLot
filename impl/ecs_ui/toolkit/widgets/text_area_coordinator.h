// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class TextAreaCoordinator final{
private:
    using Item = BuilderScopeFrame::Item;
    using TextAreaFrame = BuilderScopeFrame::TextAreaFrame;


public:
    explicit TextAreaCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] EditBoxResult declareTextArea(AStringView stableKey, EditModel& model, TextAreaState& state, const TextAreaOptions& options);
    [[nodiscard]] b8 paintTextArea(const Item& item, const LayoutBox& box);


private:
    [[nodiscard]] b8 textAreaAvailable(const EditModel& model, const TextAreaState& state)const noexcept;
    void snapshotTextArea(TextAreaFrame& frame)noexcept;
    [[nodiscard]] b8 applyTextAreaScrollInput(const Item& item, const TextAreaFrame& frame, const ControlToken& token);
    [[nodiscard]] b8 paintTextAreaScrollbars(const Item& item, const ScrollViewportPlacement& placement, const Rect& clip, const ControlToken& token);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

