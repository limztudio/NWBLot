// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_snapshot.h"

#include "passive_scene_support.h"
#include "progress_scene.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_progress_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 15u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("zero"), NWB_TEXT("quarter"), NWB_TEXT("full"),
    NWB_TEXT("below"), NWB_TEXT("above"), NWB_TEXT("tiny"), NWB_TEXT("frozen"), NWB_TEXT("external"),
    NWB_TEXT("external_clip"), NWB_TEXT("parent_progress"), NWB_TEXT("child_progress"), NWB_TEXT("parent"), NWB_TEXT("child")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiProgressSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_progress_snapshot;
    UiProgressSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(input, current.rectangles, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, 13u, [](const usize index){ return index != 10u; });
    const u32 focusCode = observation.focusCode;
    const u32 progressTargets = observation.passiveTargets;
    current.values = { static_cast<u64>(m_phase), focusCode, m_beforeClicks, m_afterClicks, static_cast<u64>(m_parent.isOpen()), static_cast<u64>(m_child.isOpen()), input.popupCount(), progressTargets };
    const auto update = PublishPassiveSmokeSnapshot(current, m_snapshot, [&]()noexcept{ return current.values != m_snapshot.values || current.bits != m_snapshot.bits; });
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: state sequence={} values={},{},{},{},{},{},{},{} bits={},{}"), current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], current.bits[0], current.bits[1]);
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiProgressSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiProgressSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintSmokeValueMarkers(context, m_snapshot.values, m_snapshot.bits, m_snapshot.sequence);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

