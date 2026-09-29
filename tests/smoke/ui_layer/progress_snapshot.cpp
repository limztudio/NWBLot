// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_snapshot.h"
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


[[nodiscard]] static bool SameRect(const Impl::Ui::Rect& left, const Impl::Ui::Rect& right){
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

[[nodiscard]] static Impl::Ui::Color Encode(const u64 value){
    return { static_cast<f32>(value & 15u) / 15.0f, static_cast<f32>((value >> 4u) & 15u) / 15.0f,
        static_cast<f32>((value >> 8u) & 15u) / 15.0f, 1.0f };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiProgressSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_progress_snapshot;
    UiProgressSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 rootButton = 0u;
    u32 progressTargets = 0u;
    for(const HitTarget& target : input.targets()){
        u32 code = 0u;
        if(target.activatable && !target.popup.valid() && rootButton < 2u){
            current.rectangles[rootButton] = target.rectangle;
            code = rootButton + 1u;
            ++rootButton;
        }
        else if(target.activatable && target.popup.instanceGeneration == m_parent.instanceGeneration())
            code = 3u;
        else if(target.activatable && target.popup.instanceGeneration == m_child.instanceGeneration())
            code = 4u;
        if(target.id == input.focus())
            focusCode = code;
        for(usize index = 2u; index < 13u; ++index){
            if(index == 10u || current.rectangles[index].width == 0.0f)
                continue;
            if(SameRect(target.rectangle, current.rectangles[index]))
                ++progressTargets;
        }
    }
    current.values = { static_cast<u64>(m_phase), focusCode, m_beforeClicks, m_afterClicks,
        static_cast<u64>(m_parent.isOpen()), static_cast<u64>(m_child.isOpen()), input.popupCount(), progressTargets };
    const DisplayMetrics& previous = m_snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth
        || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX
        || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = current.values != m_snapshot.values || current.bits != m_snapshot.bits;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight
            , current.display.pixelScaleX, current.display.pixelScaleY
        );
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: state sequence={} values={},{},{},{},{},{},{},{} bits={},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7]
        , current.bits[0], current.bits[1]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index){
        const Rect& rectangle = current.rectangles[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiProgressSmoke: geometry sequence={} {}={},{},{},{}")
            , current.sequence, s_RectNames[index], rectangle.x, rectangle.y, rectangle.width, rectangle.height
        );
    }
}

void UiProgressSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    using namespace __hidden_ui_progress_snapshot;
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(const u64 value : m_snapshot.values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                Encode(value >> (part * 12u)));
        }
    }
    for(const u64 bits : m_snapshot.bits){
        for(u32 part = 0u; part < 6u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                Encode(bits >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
            Encode(m_snapshot.sequence >> (part * 12u)));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

