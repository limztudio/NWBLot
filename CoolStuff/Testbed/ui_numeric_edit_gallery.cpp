// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_numeric_edit_gallery.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNumericEditGallery::UiNumericEditGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_integer(arena)
    , m_float(arena)
{
    const bool initialized = m_integer.setValue(9007199254740993ll) && m_float.setValue(1.25);
    NWB_FATAL_ASSERT(initialized);
}

void UiNumericEditGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("numeric_edit_gallery", { x, y, 280.0f, 200.0f }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = 14.0f;
    bool valid = ui.label("title", "Exact integer / decimal editing");
    IntegerEditOptions integer;
    const NumericEditBoxResult first = ui.integerEdit("integer", m_integer, m_integerState, integer);
    FloatEditOptions floating;
    floating.bounds = { -10.0, 10.0, NumericBoundsPolicy::Clamp };
    const NumericEditBoxResult second = ui.floatEdit("float", m_float, m_floatState, floating);
    const auto text = StringFormat(scratchArena, "Committed: {} / {}", m_integer.value(), m_float.value());
    valid = ui.label("committed", { text.data(), text.size() }) && first.edit.valid && second.edit.valid && valid;
    valid = ui.label("hint", "Enter: commit, Escape: restore") && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom numeric editor declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

