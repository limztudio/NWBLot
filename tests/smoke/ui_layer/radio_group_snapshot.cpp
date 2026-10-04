// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "radio_group_snapshot.h"

#include "smoke_geometry.h"
#include "radio_group_scene.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_radio_group_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr Array<TStringView, 41u> s_RectNames{
    GLB_TEXT("before"), GLB_TEXT("group"), GLB_TEXT("after"), GLB_TEXT("open"),
    GLB_TEXT("disabled_group"), GLB_TEXT("parent"), GLB_TEXT("popup_group"), GLB_TEXT("popup_close"),
    GLB_TEXT("row10"), GLB_TEXT("indicator10"), GLB_TEXT("mark10"),
    GLB_TEXT("row20"), GLB_TEXT("indicator20"), GLB_TEXT("mark20"),
    GLB_TEXT("row30"), GLB_TEXT("indicator30"), GLB_TEXT("mark30"),
    GLB_TEXT("row40"), GLB_TEXT("indicator40"), GLB_TEXT("mark40"),
    GLB_TEXT("row50"), GLB_TEXT("indicator50"), GLB_TEXT("mark50"),
    GLB_TEXT("disabled_row30"), GLB_TEXT("disabled_indicator30"), GLB_TEXT("disabled_mark30"),
    GLB_TEXT("popup_row10"), GLB_TEXT("popup_indicator10"), GLB_TEXT("popup_mark10"),
    GLB_TEXT("popup_row20"), GLB_TEXT("popup_indicator20"), GLB_TEXT("popup_mark20"),
    GLB_TEXT("popup_row30"), GLB_TEXT("popup_indicator30"), GLB_TEXT("popup_mark30"),
    GLB_TEXT("popup_row40"), GLB_TEXT("popup_indicator40"), GLB_TEXT("popup_mark40"),
    GLB_TEXT("popup_row50"), GLB_TEXT("popup_indicator50"), GLB_TEXT("popup_mark50")
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void CopyChoices(UiRadioGroupSnapshot& snapshot, const Impl::Ui::RadioGroupPlacement& placement, const u32 offset){
    for(u32 index = 0u; index < placement.count; ++index){
        const Impl::Ui::RadioGroupChoicePlacement& row = placement.rows[index];
        if(row.key < 10u || row.key > 50u || row.key % 10u != 0u)
            continue;
        const usize destination = offset + static_cast<usize>(row.key / 10u - 1u) * 3u;
        snapshot.rectangles[destination] = row.rectangle;
        snapshot.rectangles[destination + 1u] = row.indicator;
        snapshot.rectangles[destination + 2u] = row.mark;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiRadioGroupSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_radio_group_snapshot;
    UiRadioGroupSnapshot current;
    current.display = context.display;
    current.rectangles[1u] = m_state.placement().bounds;
    current.rectangles[4u] = m_disabled.placement().bounds;
    CopyChoices(current, m_state.placement(), 8u);
    for(u32 index = 0u; index < m_disabled.placement().count; ++index){
        const RadioGroupChoicePlacement& row = m_disabled.placement().rows[index];
        if(row.key == 30u){
            current.rectangles[23u] = row.rectangle;
            current.rectangles[24u] = row.indicator;
            current.rectangles[25u] = row.mark;
        }
    }
    if(m_parent.isOpen()){
        current.rectangles[5u] = m_parent.placement().bounds;
        current.rectangles[6u] = m_popupRadio.placement().bounds;
        CopyChoices(current, m_popupRadio.placement(), 26u);
    }
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 focusScope = 0u;
    u32 rootButton = 0u;
    constexpr u32 rootSlots[]{ 0u, 2u, 3u };
    for(const HitTarget& target : input.targets()){
        u32 code = 0u;
        if(!target.control.valid() && target.activatable){
            if(!target.popup.valid() && rootButton < 3u){
                const u32 slot = rootSlots[rootButton++];
                current.rectangles[slot] = target.rectangle;
                code = slot + 1u;
            }
            else if(target.popup.instanceGeneration == m_parent.instanceGeneration()){
                if(m_parent.isOpen())
                    current.rectangles[7u] = target.rectangle;
                code = 6u;
            }
        }
        else if(target.control.instanceGeneration == m_state.inputGeneration() && !target.owner.valid())
            code = 2u;
        else if(target.control.instanceGeneration == m_popupRadio.inputGeneration() && !target.owner.valid())
            code = 5u;
        if(target.id == input.focus()){
            focusCode = code;
            focusScope = static_cast<u32>(target.popup.valid());
        }
    }
    current.values = { m_state.selectedKey(), m_state.cursorKey(), focusCode, m_changes, m_activations,
        m_beforeClicks, m_afterClicks, static_cast<u64>(m_enabled), m_source.revision(), m_source.instanceGeneration(),
        static_cast<u64>(m_source.reversed()), m_source.removedKey(), static_cast<u64>(m_parent.isOpen()),
        m_popupRadio.selectedKey(), m_popupRadio.cursorKey(), m_popupChanges, m_popupActivations,
        static_cast<u64>(input.popupCount()), focusScope, m_source.labelReads(), m_disabled.selectedKey(),
        m_source.rowCount() };
    const DisplayMetrics& previous = m_snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth
        || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX
        || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = current.values != m_snapshot.values;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiRadioGroupSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight
            , current.display.pixelScaleX, current.display.pixelScaleY
        );
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(GLB_TEXT("UiRadioGroupSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},"
        "{},{},{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , value[8], value[9], value[10], value[11], value[12], value[13], value[14], value[15], value[16]
        , value[17], value[18], value[19], value[20], value[21]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        LogSmokeRect(GLB_TEXT("UiRadioGroupSmoke"), current.sequence, s_RectNames[index], current.rectangles[index]);
}

void UiRadioGroupSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 18.0f;
    for(usize index = 0u; index <= m_snapshot.values.size(); ++index){
        const u64 value = index == m_snapshot.values.size() ? m_snapshot.sequence : m_snapshot.values[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

