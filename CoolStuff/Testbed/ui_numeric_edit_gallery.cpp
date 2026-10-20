// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "ui_numeric_edit_gallery.h"
#include "ui_gallery_constants.h"

#include <core/alloc/scratch.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiNumericEditGallery::UiNumericEditGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_integer(arena)
    , m_float(arena)
{
    // 2^53 + 1 is not exact in f64, so it shows integer editing keeps every digit.
    constexpr i64 s_InitialInteger = 9007199254740993ll;
    constexpr f64 s_InitialDecimal = 1.25;
    const bool initialized = m_integer.setValue(s_InitialInteger) && m_float.setValue(s_InitialDecimal);
    NWB_FATAL_ASSERT(initialized);
}

void UiNumericEditGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    constexpr f32 s_PanelHeight = 200.0f;
    constexpr f64 s_DecimalLimit = 10.0;
    Builder& ui = context.ui;
    if(!ui.beginPanel("numeric_edit_gallery", { x, y, s_OrdinaryGalleryWidth, s_PanelHeight }))
        return;
    const f32 previousFontSize = ui.style().fontSize;
    ui.style().fontSize = s_GalleryFontSize;
    bool valid = ui.label(s_TitleKey, "Exact integer / decimal editing");
    IntegerEditOptions integer;
    const NumericEditBoxResult first = ui.integerEdit("integer", m_integer, m_integerState, integer);
    FloatEditOptions floating;
    floating.bounds = { -s_DecimalLimit, s_DecimalLimit, NumericBoundsPolicy::Clamp };
    const NumericEditBoxResult second = ui.floatEdit("float", m_float, m_floatState, floating);
    const auto text = StringFormat(scratchArena, "Committed: {} / {}", m_integer.value(), m_float.value());
    valid = ui.label("committed", { text.data(), text.size() }) && first.edit.valid && second.edit.valid && valid;
    valid = ui.label(s_HintKey,"Enter: commit, Escape: restore") && valid;
    valid = ui.endPanel() && valid;
    ui.style().fontSize = previousFontSize;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom numeric editor declaration failed"));
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

