// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "slider_snapshot.h"

#include "smoke_geometry.h"
#include "slider_scene.h"

#include <core/common/log.h>

#include <global/bit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_slider_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 29u> s_RectNames{
    GLOBAL_TEXT("before"), GLOBAL_TEXT("after"), GLOBAL_TEXT("open"), GLOBAL_TEXT("parent"), GLOBAL_TEXT("popup_close"),
    GLOBAL_TEXT("main_bounds"), GLOBAL_TEXT("main_clip"), GLOBAL_TEXT("main_travel"), GLOBAL_TEXT("main_track"),
    GLOBAL_TEXT("main_center"), GLOBAL_TEXT("main_thumb"), GLOBAL_TEXT("disabled_bounds"), GLOBAL_TEXT("disabled_clip"),
    GLOBAL_TEXT("disabled_travel"), GLOBAL_TEXT("disabled_track"), GLOBAL_TEXT("disabled_center"), GLOBAL_TEXT("disabled_thumb"),
    GLOBAL_TEXT("constant_bounds"), GLOBAL_TEXT("constant_clip"), GLOBAL_TEXT("constant_travel"), GLOBAL_TEXT("constant_track"),
    GLOBAL_TEXT("constant_center"), GLOBAL_TEXT("constant_thumb"), GLOBAL_TEXT("popup_bounds"), GLOBAL_TEXT("popup_clip"),
    GLOBAL_TEXT("popup_travel"), GLOBAL_TEXT("popup_track"), GLOBAL_TEXT("popup_center"), GLOBAL_TEXT("popup_thumb")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CopyPlacement(UiSliderSnapshot& snapshot, const Impl::Ui::SliderPlacement& placement, const u32 offset){
    snapshot.rectangles[offset] = placement.bounds;
    snapshot.rectangles[offset + 1u] = placement.clip;
    snapshot.rectangles[offset + 2u] = placement.travelBounds;
    snapshot.rectangles[offset + 3u] = placement.track;
    snapshot.rectangles[offset + 4u] = placement.centerTravel;
    snapshot.rectangles[offset + 5u] = placement.thumb;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSliderSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_slider_snapshot;
    UiSliderSnapshot current;
    current.display = context.display;
    CopyPlacement(current, m_state.placement(), 5u);
    CopyPlacement(current, m_disabled.placement(), 11u);
    CopyPlacement(current, m_constant.placement(), 17u);
    if(m_parent.isOpen()){
        current.rectangles[3u] = m_parent.placement().bounds;
        CopyPlacement(current, m_popupSlider.placement(), 23u);
    }
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 focusScope = 0u;
    u32 rootButton = 0u;
    for(const HitTarget& target : input.targets()){
        u32 code = 0u;
        if(!target.control.valid() && target.activatable){
            if(!target.popup.valid() && rootButton < 3u){
                current.rectangles[rootButton] = target.rectangle;
                code = rootButton == 0u ? 1u : rootButton == 1u ? 3u : 4u;
                ++rootButton;
            }
            else if(target.popup.instanceGeneration == m_parent.instanceGeneration()){
                if(m_parent.isOpen())
                    current.rectangles[4u] = target.rectangle;
                code = 6u;
            }
        }
        else if(target.control.instanceGeneration == m_state.inputGeneration() && !target.owner.valid())
            code = 2u;
        else if(target.control.instanceGeneration == m_popupSlider.inputGeneration() && !target.owner.valid())
            code = 5u;
        if(target.id == input.focus()){
            focusCode = code;
            focusScope = static_cast<u32>(target.popup.valid());
        }
    }
    const bool open = m_parent.isOpen();
    current.values = { focusCode, m_changes, m_popupChanges, m_beforeClicks, m_afterClicks,
        static_cast<u64>(m_enabled), static_cast<u64>(m_narrowRange), static_cast<u64>(m_coarseStep),
        static_cast<u64>(open), static_cast<u64>(input.popupCount()), focusScope,
        static_cast<u64>(m_state.result().valid), static_cast<u64>(m_state.result().dragging),
        static_cast<u64>(open && m_popupSlider.result().valid), static_cast<u64>(open && m_popupSlider.result().dragging),
        m_externalIntents };
    current.bits = { BitCast<u64>(m_state.value()), BitCast<u64>(m_disabled.value()), BitCast<u64>(m_constant.value()),
        BitCast<u64>(m_popupSlider.value()) };
    const DisplayMetrics& previous = m_snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth
        || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX
        || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = current.values != m_snapshot.values || current.bits != m_snapshot.bits;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("UiSliderSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight
            , current.display.pixelScaleX, current.display.pixelScaleY
        );
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(GLOBAL_TEXT("UiSliderSmoke: state sequence={} values={},{},{},{},{},{},{},{},"
        "{},{},{},{},{},{},{},{} bits={},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15]
        , current.bits[0], current.bits[1], current.bits[2], current.bits[3]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(GLOBAL_TEXT("UiSliderSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiSliderSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    using namespace __hidden_ui_slider_snapshot;
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(const u64 value : m_snapshot.values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
    for(const u64 bits : m_snapshot.bits){
        for(u32 part = 0u; part < 6u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(bits >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
            EncodeSmokeColor(m_snapshot.sequence >> (part * 12u)));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

