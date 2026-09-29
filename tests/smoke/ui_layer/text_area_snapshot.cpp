// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "text_area_snapshot.h"
#include "text_area_scene.h"

#include <core/common/log.h>

#include <global/bit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_area_snapshot{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_RectNames[]{ NWB_TEXT("bounds"), NWB_TEXT("content"), NWB_TEXT("clip"), NWB_TEXT("caret"),
    NWB_TEXT("reset"), NWB_TEXT("long"), NWB_TEXT("readonly"), NWB_TEXT("enabled"), NWB_TEXT("viewport"),
    NWB_TEXT("clipboard"), NWB_TEXT("outside"), NWB_TEXT("x_track"), NWB_TEXT("x_thumb"), NWB_TEXT("y_track"),
    NWB_TEXT("y_thumb"), NWB_TEXT("corner") };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static u64 TextHash(const AStringView text){
    u32 hash = 2166136261u;
    for(const char value : text)
        hash = (hash ^ static_cast<u8>(value)) * 16777619u;
    return hash;
}

[[nodiscard]] static bool SameRect(const Impl::Ui::Rect& first, const Impl::Ui::Rect& second){
    return first.x == second.x && first.y == second.y && first.width == second.width && first.height == second.height;
}

[[nodiscard]] static Impl::Ui::Color Encode(const u64 value){
    return { static_cast<f32>(value & 15u) / 15.0f, static_cast<f32>((value >> 4u) & 15u) / 15.0f,
        static_cast<f32>((value >> 8u) & 15u) / 15.0f, 1.0f };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiTextAreaSmokeScene::observeState(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    using namespace __hidden_ui_text_area_snapshot;
    if(!m_observed.snapshot(m_model) || m_observed.shape(context.text, { {}, 16.0f }) != TextLayoutStatus::Success)
        return false;
    const EditBoxPlacement& placement = m_state.placement();
    const ScrollViewportPlacement& scrollbars = m_state.scrollbars();
    const auto& geometry = m_observed.caretGeometry();
    Rect localCaret;
    if(!geometry.caretRect(m_observed.displayCaret(), localCaret) || geometry.lines().size() > 32u)
        return false;
    UiTextAreaSnapshot current;
    current.display = context.display;
    current.scroll = m_state.scroll();
    current.measure = m_observed.layout().measure();
    current.maximum = { static_cast<f32>(scrollbars.horizontal.maximum), static_cast<f32>(scrollbars.vertical.maximum) };
    current.lineHeight = localCaret.height;
    current.rectangles[0u] = placement.bounds;
    current.rectangles[1u] = placement.content;
    current.rectangles[2u] = placement.clip;
    current.rectangles[3u] = placement.caret;
    for(u32 index = 0u; index < 6u; ++index){
        current.rectangles[4u + index] = { placement.bounds.x + static_cast<f32>(index % 3u) * 120.0f,
            placement.bounds.y - (index < 3u ? 76.0f : 38.0f), 112.0f, 30.0f };
    }
    current.rectangles[10u] = { placement.bounds.x, placement.bounds.y + placement.bounds.height + 8.0f, 160.0f, 30.0f };
    current.rectangles[11u] = scrollbars.horizontal.track;
    current.rectangles[12u] = scrollbars.horizontal.thumb;
    current.rectangles[13u] = scrollbars.vertical.track;
    current.rectangles[14u] = scrollbars.vertical.thumb;
    current.rectangles[15u] = scrollbars.corner;
    for(u32 index = 0u; index < geometry.lines().size(); ++index){
        Rect rectangle;
        if(!geometry.rangeOnLine(m_observed.selectionRange(), index, placement.caret.width, rectangle))
            return false;
        if(rectangle.width == 0.0f)
            continue;
        rectangle.x += placement.textOrigin.x;
        rectangle.y += placement.textOrigin.y;
        current.selections[current.selectionCount++] = rectangle;
    }
    const bool coherent = Abs(placement.caret.x - placement.textOrigin.x - localCaret.x) < 0.02f
        && Abs(placement.caret.y - placement.textOrigin.y - localCaret.y) < 0.02f
        && current.scroll.x == placement.scroll && current.scroll.y == placement.scrollY
        && SameRect(placement.content, scrollbars.viewport) && SameRect(placement.clip, scrollbars.contentClip)
        && current.scroll.x == scrollbars.horizontal.offset && current.scroll.y == scrollbars.vertical.offset;
    const auto& navigation = m_state.navigation();
    current.values = { static_cast<u64>(m_model.text().size()), TextHash(m_model.text()), static_cast<u64>(m_model.anchor()),
        static_cast<u64>(m_model.caret()), static_cast<u64>(m_state.focused()), static_cast<u64>(m_enabled),
        static_cast<u64>(m_readOnly), static_cast<u64>(m_compact), static_cast<u64>(m_longDocument),
        static_cast<u64>(navigation.hasPreferredX()), BitCast<u32>(navigation.preferredX()), m_submits, m_cancels, m_blurs,
        m_abandons, m_outside, m_clipboardSeeds, static_cast<u64>(m_clipboardToken.valid()), m_model.revision(),
        m_model.externalRevision(), m_model.selectionGeneration(), static_cast<u64>(coherent), geometry.lines().size(),
        static_cast<u64>(m_model.canUndo()), static_cast<u64>(m_model.canRedo()) };
    bool changed = current.values != m_snapshot.values || current.selectionCount != m_snapshot.selectionCount
        || current.scroll.x != m_snapshot.scroll.x || current.scroll.y != m_snapshot.scroll.y
        || current.maximum.x != m_snapshot.maximum.x || current.maximum.y != m_snapshot.maximum.y
        || current.measure.x != m_snapshot.measure.x || current.measure.y != m_snapshot.measure.y || current.lineHeight != m_snapshot.lineHeight;
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameRect(current.rectangles[index], m_snapshot.rectangles[index]);
    for(usize index = 0u; index < current.selections.size(); ++index)
        changed |= !SameRect(current.selections[index], m_snapshot.selections[index]);
    const auto& previous = m_snapshot.display;
    const bool displayChanged = context.display.logicalWidth != previous.logicalWidth
        || context.display.logicalHeight != previous.logicalHeight || context.display.pixelScaleX != previous.pixelScaleX
        || context.display.pixelScaleY != previous.pixelScaleY;
    if(m_snapshot.sequence != 0u && !changed && !displayChanged)
        return true;
    current.sequence = m_snapshot.sequence + 1u;
    m_snapshot = current;
    if(displayChanged){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: display logical={}x{} scale={}x{}")
            , current.display.logicalWidth, current.display.logicalHeight, current.display.pixelScaleX, current.display.pixelScaleY
        );
    }
    const auto& value = current.values;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: state sequence={} values={},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{},{}")
        , current.sequence, value[0], value[1], value[2], value[3], value[4], value[5], value[6], value[7], value[8], value[9]
        , value[10], value[11], value[12], value[13], value[14], value[15], value[16], value[17], value[18], value[19]
        , value[20], value[21], value[22], value[23], value[24]
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: metrics sequence={} scroll={},{} measure={},{} line_height={} selections={} maximum={},{}")
        , current.sequence, current.scroll.x, current.scroll.y, current.measure.x, current.measure.y
        , current.lineHeight, current.selectionCount, current.maximum.x, current.maximum.y
    );
    for(usize index = 0u; index < current.rectangles.size(); ++index){
        const Rect& rectangle = current.rectangles[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: geometry sequence={} {}={},{},{},{}")
            , current.sequence, s_RectNames[index], rectangle.x, rectangle.y, rectangle.width, rectangle.height
        );
    }
    for(u32 index = 0u; index < current.selectionCount; ++index){
        const Rect& rectangle = current.selections[index];
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiTextAreaSmoke: geometry sequence={} selection_{}={},{},{},{}")
            , current.sequence, index, rectangle.x, rectangle.y, rectangle.width, rectangle.height
        );
    }
    return true;
}

void UiTextAreaSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(usize index = 0u; index <= m_snapshot.values.size(); ++index){
        const u64 value = index == m_snapshot.values.size() ? m_snapshot.sequence : m_snapshot.values[index];
        const u32 parts = index == 1u || index == 10u || (index >= 18u && index <= 20u) ? 3u : 2u;
        for(u32 part = 0u; part < parts; ++part){
            context.paint.fillRect({ 8.0f + static_cast<f32>(marker) * 8.0f, y, 6.0f, 10.0f },
                __hidden_ui_text_area_snapshot::Encode(value >> (part * 12u)));
            ++marker;
        }
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

