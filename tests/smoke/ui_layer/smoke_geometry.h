// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "marker_positions.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <core/common/log.h>
#include <core/alloc/general.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{

template<typename Source>
[[nodiscard]] Expected<u64> FindAdjacentEnabledRow(const Source& source, const u64 start, const bool reverse)noexcept(
    noexcept(static_cast<bool>(start >= source.rowCount()))
    && noexcept(static_cast<bool>(start + 1u >= source.rowCount()))
    && noexcept(static_cast<bool>(source.enabled(start)))
    && noexcept(static_cast<bool>(source.enabled(DeclVal<u64&>())))
){
    u64 index = 0u;
    if(start >= source.rowCount())
        return MakeUnexpected(Failure{});
    if(source.enabled(start)){
        index = start;
        return index;
    }
    if(reverse){
        if(start == 0u)
            return MakeUnexpected(Failure{});
        index = start - 1u;
    }
    else{
        if(start + 1u >= source.rowCount())
            return MakeUnexpected(Failure{});
        index = start + 1u;
    }
    return source.enabled(index) ? Expected<u64>{ index } : MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u64 value)noexcept{
    const SIMDVectorSingle channels = VectorSet(static_cast<f32>(value & 15u), static_cast<f32>((value >> 4u) & 15u), static_cast<f32>((value >> 8u) & 15u), 15.0f);
    const SIMDVectorSingle color = VectorDivide(channels, VectorReplicate(15.0f));
    return { VectorGetX(color), VectorGetY(color), VectorGetZ(color), 1.0f };
}

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u32 value)noexcept{
    return EncodeSmokeColor(static_cast<u64>(value));
}

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const f32 value)noexcept{
    return EncodeSmokeColor(static_cast<u64>(VectorGetX(VectorAdd(VectorMax(VectorZero(), VectorReplicate(value)), VectorReplicate(0.5f)))));
}

// Lanes are x, y, width and height.
[[nodiscard]] inline Impl::Ui::Rect StoreSmokeRect(const SIMDVectorSingle rectangle)noexcept{
    return { VectorGetX(rectangle), VectorGetY(rectangle), VectorGetZ(rectangle), VectorGetW(rectangle) };
}

[[nodiscard]] inline bool SameSmokeRect(const Impl::Ui::Rect& left, const Impl::Ui::Rect& right)noexcept{
    return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height;
}

// The accepted uniform content extent owns row height; smoke input never supplies a sizing policy.
[[nodiscard]] inline Expected<Impl::Ui::Rect> SmokeListRowBounds(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    if(placement.rowCount == 0u)
        return MakeUnexpected(Failure{});
    const f32 height = static_cast<f32>(VectorDoubleGetX(VectorDoubleDivide(VectorDoubleReplicate(placement.contentHeight), VectorDoubleReplicate(static_cast<f64>(placement.rowCount)))));
    return Impl::Ui::ScrollLayout::RowBounds(index, placement, height);
}

[[nodiscard]] inline Impl::Ui::Rect VisibleSmokeListRow(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    const auto rectangle = SmokeListRowBounds(placement, index);
    if(!rectangle)
        return {};
    const auto visible = Impl::Ui::IntersectUiRects<Impl::Ui::UiRectPrecision::PreciseExtents>(*rectangle, placement.contentClip);
    return visible ? *visible : Impl::Ui::Rect{};
}

inline void LogSmokeRect(const TStringView tag, const u32 sequence, const TStringView name, const Impl::Ui::Rect& rectangle){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} {}={},{},{},{}"), tag, sequence, name, rectangle.x, rectangle.y, rectangle.width, rectangle.height);
}

[[nodiscard]] inline u64 HashSmokeText(const AStringView text)noexcept{
    u32 hash = 2166136261u;
    for(const char value : text)
        hash = (hash ^ static_cast<u8>(value)) * 16777619u;
    return hash;
}

template<u32 Step, u32 Width, typename ValuesT>
inline void PaintKeyedSmokeValueMarkers(Impl::UiPaintContext& context, const ValuesT& values, const u64 sequence){
    const auto positions = BuildSmokeMarkerPositions<(TupleSize_V<ValuesT> + 1u) * 2u, 12u, Step>();
    const f32 y = VectorGetX(VectorSubtract(VectorReplicate(context.display.logicalHeight), VectorReplicate(20.0f)));
    for(usize index = 0u; index <= values.size(); ++index){
        const u64 value = index == values.size() ? sequence : values[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ positions[marker], y, static_cast<f32>(Width), 12.0f }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}

// Progress, slider, and image snapshots paint one 12-bit marker swatch per value part plus sequence swatches along the bottom edge.
template<typename ValuesT, typename BitsT>
inline void PaintSmokeValueMarkers(Impl::UiPaintContext& context, const ValuesT& values, const BitsT& bits, const u64 sequence){
    const f32 y = VectorGetX(VectorSubtract(VectorReplicate(context.display.logicalHeight), VectorReplicate(18.0f)));
    const auto positions = BuildSmokeMarkerPositions<TupleSize_V<ValuesT> * 2u + TupleSize_V<BitsT> * 6u + 2u, 12u, 12u>();
    usize marker = 0u;
    for(const u64 value : values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
    for(const u64 bit : bits){
        for(u32 part = 0u; part < 6u; ++part){
            context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(bit >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(sequence >> (part * 12u)));
    }
}

[[nodiscard]] inline u32 HashSmokeText32(const AStringView text)noexcept{
    return static_cast<u32>(HashSmokeText(text));
}

[[nodiscard]] inline bool SameSmokePlacement(const Impl::Ui::EditBoxPlacement& left, const Impl::Ui::EditBoxPlacement& right)noexcept{
    return SameSmokeRect(left.bounds, right.bounds) && SameSmokeRect(left.content, right.content) && SameSmokeRect(left.caret, right.caret) && left.scroll == right.scroll;
}

inline void LogSmokeEditGeometry(const TStringView tag, const u32 sequence, const TStringView field, const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection){
    const auto& bounds = placement.bounds;
    const auto& content = placement.content;
    const auto& caret = placement.caret;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} field={} bounds={},{},{},{} content={},{},{},{}"), tag, sequence, field, bounds.x, bounds.y, bounds.width, bounds.height, content.x, content.y, content.width, content.height);
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

