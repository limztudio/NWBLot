// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_selection_probe.h"

#include <global/simd/vector.h>
#include <global/termination.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Impl::Ui::Rect CaptureEditSelection(Core::Alloc::GlobalArena& arena, Impl::Ui::TextService& text, const Impl::Ui::EditModel& model, const Impl::Ui::EditBoxPlacement& placement, const f32 fontSize){
    using namespace Impl::Ui;
    NWB_FATAL_ASSERT(model.textMode() == EditTextMode::SingleLine);
    EditBoxView view(arena);
    const bool captured = view.snapshot(model);
    NWB_FATAL_ASSERT(captured);
    const EditBoxRange range = view.selectionRange();
    if(range.begin == range.end || placement.content.width <= 0.0f || placement.content.height <= 0.0f)
        return {};
    const bool shaped = view.shape(text, { {}, fontSize }) == TextLayoutStatus::Success;
    NWB_FATAL_ASSERT(shaped);
    auto selection = view.caretGeometry().rangeOnLine(range, 0u, placement.caret.width);
    NWB_FATAL_ASSERT(selection.has_value());
    if(selection->width > 0.0f){
        const SIMDVectorSingle origin = VectorAdd(VectorSet(selection->x, selection->y, 0.0f, 0.0f), VectorSet(placement.textOrigin.x, placement.textOrigin.y, 0.0f, 0.0f));
        selection->x = VectorGetX(origin);
        selection->y = VectorGetY(origin);
    }
    return *selection;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

