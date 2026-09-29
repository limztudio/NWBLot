// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_snapshot.h"
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


void UiImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_image_snapshot;
    UiImageSnapshot current = m_declared;
    current.display = context.display;
    const InputRouter& input = context.ui.input();
    u32 focusCode = 0u;
    u32 rootButton = 0u;
    u32 imageTargets = 0u;
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
        for(usize index = 2u; index < 14u; ++index){
            if(index == 11u || current.rectangles[index].width == 0.0f)
                continue;
            if(SameRect(target.rectangle, current.rectangles[index]))
                ++imageTargets;
        }
    }
    current.values[0u] = static_cast<u64>(m_phase);
    current.values[1u] = focusCode;
    current.values[2u] = m_beforeClicks;
    current.values[3u] = m_afterClicks;
    current.values[4u] = static_cast<u64>(m_parent.isOpen());
    current.values[5u] = static_cast<u64>(m_child.isOpen());
    current.values[6u] = input.popupCount();
    current.values[7u] = imageTargets;
    const DisplayMetrics& previous = m_snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth
        || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX
        || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = current.values != m_snapshot.values;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight
            , current.display.pixelScaleX, current.display.pixelScaleY
        );
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9]
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index){
        const Rect& rectangle = current.rectangles[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiImageSmoke: geometry sequence={} {}={},{},{},{}")
            , current.sequence, s_RectNames[index], rectangle.x, rectangle.y, rectangle.width, rectangle.height
        );
    }
}

void UiImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    using namespace __hidden_ui_image_snapshot;
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(const u64 value : m_snapshot.values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                Encode(value >> (part * 12u)));
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

