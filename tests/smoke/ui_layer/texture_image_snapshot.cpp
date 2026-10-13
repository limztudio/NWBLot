// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_snapshot.h"
#include "texture_image_scene.h"
#include "passive_scene_support.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_texture_image_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 17u> s_RectNames{
    NWB_TEXT("before"), NWB_TEXT("after"), NWB_TEXT("default_tile"), NWB_TEXT("alternate_tile"), NWB_TEXT("tinted"),
    NWB_TEXT("zero_alpha"), NWB_TEXT("frozen"), NWB_TEXT("external"), NWB_TEXT("external_clip"),
    NWB_TEXT("default_atlas"), NWB_TEXT("alternate_atlas"), NWB_TEXT("skin_fill"), NWB_TEXT("parent_image"),
    NWB_TEXT("child_image"), NWB_TEXT("parent"), NWB_TEXT("child"), NWB_TEXT("builder_image")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiTextureImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_texture_image_snapshot;
    UiTextureImageSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    const auto observation = ObservePassiveSmokeTargets(input, current.rectangles, m_parent.instanceGeneration(), m_child.instanceGeneration(), 2u, current.rectangles.size(), [](const usize index){ return index != 8u && index != 14u && index != 15u; });
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
    current.values[10u] = m_replacementCount;
    current.values[11u] = static_cast<u64>(m_replacementAlternate);
    current.values[12u] = m_evictionRemaining;
    current.values[13u] = m_evictionCompleted;
    current.generations = { m_default->generation(), m_alternate->generation(), m_replacement->generation() };
    const auto update = PublishPassiveSmokeSnapshot(current, m_snapshot, [&]{ return current.values != m_snapshot.values || current.generations != m_snapshot.generations; });
    if(!update.published)
        return;
    if(update.displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: display logical={}x{} scale={}x{}"), current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY);
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: state sequence={} values={},{},{},{},{},{},{},")
        NWB_TEXT("{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5]
        , value[6], value[7], value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: sources sequence={} generations={},{},{} same_identity={}")
        , current.sequence, current.generations[0], current.generations[1], current.generations[2]
        , static_cast<u32>(m_replacement->identity().name() == m_default->identity().name())
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(NWB_TEXT("UiTextureImageSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiTextureImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    PaintPassiveSmokeValueMarkers(context, m_snapshot.values, m_snapshot.sequence);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

