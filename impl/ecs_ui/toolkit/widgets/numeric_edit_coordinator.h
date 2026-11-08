// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/builder_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Builder;


class NumericEditCoordinator final{
private:
    using IntegerEditFrame = BuilderScopeFrame::IntegerEditFrame;
    using FloatEditFrame = BuilderScopeFrame::FloatEditFrame;


public:
    explicit NumericEditCoordinator(Builder& builder)noexcept
        : m_builder(builder)
    {}


public:
    [[nodiscard]] NumericEditBoxResult integerEdit(AStringView stableKey, IntegerEditModel& model, EditBoxState& state, const IntegerEditOptions& options);
    [[nodiscard]] NumericEditBoxResult floatEdit(AStringView stableKey, FloatEditModel& model, EditBoxState& state, const FloatEditOptions& options);


private:
    template<typename FrameT, typename ModelT, typename OptionsT, typename ValidateBoundsF, typename FrameListT>
    [[nodiscard]] NumericEditBoxResult declareNumericEdit(AStringView stableKey, ModelT& model, EditBoxState& state, const OptionsT& options, ValidateBoundsF validateBounds, FrameListT& frames);


private:
    Builder& m_builder;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

