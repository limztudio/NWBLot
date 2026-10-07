// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.h>

#include <core/common/log.h>
#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u64 value)noexcept{
    return { static_cast<f32>(value & 15u) / 15.0f, static_cast<f32>((value >> 4u) & 15u) / 15.0f,
        static_cast<f32>((value >> 8u) & 15u) / 15.0f, 1.0f };
}

[[nodiscard]] inline bool SameSmokeRect(const Impl::Ui::Rect& left, const Impl::Ui::Rect& right)noexcept{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

inline void LogSmokeRect(const TStringView tag, const u32 sequence, const TStringView name, const Impl::Ui::Rect& rectangle){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} {}={},{},{},{}")
        , tag, sequence, name, rectangle.x, rectangle.y, rectangle.width, rectangle.height
    );
}

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u32 value)noexcept{
    return EncodeSmokeColor(static_cast<u64>(value));
}

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const f32 value)noexcept{
    return EncodeSmokeColor(static_cast<u64>(Max(0.0f, value) + 0.5f));
}

[[nodiscard]] inline u64 HashSmokeText(const AStringView text){
    u32 hash = 2166136261u;
    for(const char value : text)
        hash = (hash ^ static_cast<u8>(value)) * 16777619u;
    return hash;
}

// Progress, slider, and image snapshots paint one 12-bit marker swatch per
// value part plus sequence swatches along the bottom edge.
template<typename ValuesT, typename BitsT>
inline void PaintSmokeValueMarkers(Impl::UiPaintContext& context, const ValuesT& values, const BitsT& bits, const u64 sequence){
    const f32 y = context.display.logicalHeight - 18.0f;
    usize marker = 0u;
    for(const u64 value : values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(value >> (part * 12u)));
        }
    }
    for(const u64 bit : bits){
        for(u32 part = 0u; part < 6u; ++part){
            context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
                EncodeSmokeColor(bit >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ 12.0f + static_cast<f32>(marker++) * 12.0f, y, 8.0f, 8.0f },
            EncodeSmokeColor(sequence >> (part * 12u)));
    }
}

[[nodiscard]] inline u32 HashSmokeText32(const AStringView text){
    return static_cast<u32>(HashSmokeText(text));
}

[[nodiscard]] inline bool SameSmokePlacement(const Impl::Ui::EditBoxPlacement& left, const Impl::Ui::EditBoxPlacement& right){
    return
        SameSmokeRect(left.bounds, right.bounds) && SameSmokeRect(left.content, right.content)
        && SameSmokeRect(left.caret, right.caret) && left.scroll == right.scroll
    ;
}

inline void LogSmokeEditGeometry(const TStringView tag, const u32 sequence, const TStringView field,
    const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection){
    const auto& bounds = placement.bounds;
    const auto& content = placement.content;
    const auto& caret = placement.caret;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} field={} bounds={},{},{},{} content={},{},{},{}")
        , tag, sequence, field, bounds.x, bounds.y, bounds.width, bounds.height
        , content.x, content.y, content.width, content.height
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: selection sequence={} field={} caret={},{},{},{} selection={},{},{},{} scroll={}")
        , tag, sequence, field, caret.x, caret.y, caret.width, caret.height
        , selection.x, selection.y, selection.width, selection.height, placement.scroll
    );
}



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

