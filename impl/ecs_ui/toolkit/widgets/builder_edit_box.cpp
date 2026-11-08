// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_ui/toolkit/builder.h>
#include <impl/ecs_ui/toolkit/widgets/edit_box_coordinator.h>
#include <impl/ecs_ui/toolkit/widgets/control_style_metrics.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditBoxResult Builder::declareEditBox(const AStringView stableKey, const EditBoxOptions& options, EditModel& model, EditBoxState& state, IEditActionSink* actions, IntegerEditFrame* integerFrame, FloatEditFrame* floatFrame){
    return EditBoxCoordinator(*this).declareEditBox(stableKey, options, model, state, actions, integerFrame, floatFrame);
}

Point Builder::loadEditMinimumSize()const noexcept{
    if(!m_skin)
        return Point{ s_StyleMinimumFieldWidth, 0.0f };
    return ControlStyleMetrics().editMinimumSize(*m_skin, m_editStyle);
}

b8 Builder::prepareEditBox(const EditBoxResult& result, EditModel& model, EditBoxState& state, Item& item){
    return EditBoxCoordinator(*this).prepareEditBox(result, model, state, item);
}

b8 Builder::paintEditBox(const Item& item, const LayoutBox& box, const HitTarget* navigation){
    return EditBoxCoordinator(*this).paintEditBox(item, box, navigation);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

