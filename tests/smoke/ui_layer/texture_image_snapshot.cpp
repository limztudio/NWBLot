// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "texture_image_snapshot.h"
#include "texture_image_scene.h"

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


void UiTextureImageSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_texture_image_snapshot;
    UiTextureImageSnapshot current = m_declared;
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
        for(usize index = 2u; index < current.rectangles.size(); ++index){
            if(index == 8u || index == 14u || index == 15u || current.rectangles[index].width == 0.0f)
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
    current.values[10u] = m_replacementCount;
    current.values[11u] = static_cast<u64>(m_replacementAlternate);
    current.values[12u] = m_evictionRemaining;
    current.values[13u] = m_evictionCompleted;
    current.generations = { m_default->generation(), m_alternate->generation(), m_replacement->generation() };
    const DisplayMetrics& previous = m_snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth
        || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX
        || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = current.values != m_snapshot.values || current.generations != m_snapshot.generations;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameRect(current.rectangles[index], m_snapshot.rectangles[index]);
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight
            , current.display.pixelScaleX, current.display.pixelScaleY
        );
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
    for(usize index = 0u; index < current.rectangles.size(); ++index){
        const Rect& rectangle = current.rectangles[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextureImageSmoke: geometry sequence={} {}={},{},{},{}")
            , current.sequence, s_RectNames[index], rectangle.x, rectangle.y, rectangle.width, rectangle.height
        );
    }
}

void UiTextureImageSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    using namespace __hidden_ui_texture_image_snapshot;
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(const u64 value : m_snapshot.values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 12.0f, y, 8.0f, 8.0f },
                Encode(value >> (part * 12u)));
            ++marker;
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ 12.0f + static_cast<f32>(marker) * 12.0f, y, 8.0f, 8.0f },
            Encode(m_snapshot.sequence >> (part * 12u)));
        ++marker;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

