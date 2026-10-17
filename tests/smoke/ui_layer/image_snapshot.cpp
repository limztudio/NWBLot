// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_snapshot.h"

#include "passive_scene_support.h"
#include "image_scene.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 16u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("natural_sprite"), NWB_TEXT("natural_slice"),
    NWB_TEXT("fixed_sprite"), NWB_TEXT("fixed_slice"), NWB_TEXT("stretch"), NWB_TEXT("tinted"), NWB_TEXT("transparent"),
    NWB_TEXT("frozen"), NWB_TEXT("external"), NWB_TEXT("external_clip"), NWB_TEXT("parent_image"), NWB_TEXT("child_image"),
    NWB_TEXT("parent"), NWB_TEXT("child")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_image_snapshot;
    UiImageSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(input, current.rectangles, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, 14u, [](const usize index){ return index != 11u; });
    const u32 focusCode = observation.focusCode;
    const u32 imageTargets = observation.passiveTargets;
    current.values[0u] = static_cast<u64>(m_phase);
    current.values[1u] = focusCode;
    current.values[2u] = m_beforeClicks;
    current.values[3u] = m_afterClicks;
    current.values[4u] = static_cast<u64>(m_parent.isOpen());
    current.values[5u] = static_cast<u64>(m_child.isOpen());
    current.values[6u] = input.popupCount();
    current.values[7u] = imageTargets;
    const auto update = PublishPassiveSmokeSnapshot(current, m_snapshot, [&]()noexcept{ return current.values != m_snapshot.values; });
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{}"), current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9]);
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiImageSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintPassiveSmokeValueMarkers(context, m_snapshot.values, m_snapshot.sequence);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

