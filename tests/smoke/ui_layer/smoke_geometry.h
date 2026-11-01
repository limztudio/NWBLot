// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "marker_positions.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/layout/rectangle.h>

#include <core/common/log.h>
#include <core/alloc/general.h>

#include <global/simdmath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename Source>
[[nodiscard]] Expected<u64> FindAdjacentEnabledRow(const Source& source, const u64 start, const b8 reverse)noexcept(
    noexcept(static_cast<b8>(start >= source.rowCount()))
    && noexcept(static_cast<b8>(start + 1u >= source.rowCount()))
    && noexcept(static_cast<b8>(source.enabled(start)))
    && noexcept(static_cast<b8>(source.enabled(DeclVal<u64&>())))
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


[[nodiscard]] Impl::Ui::Color EncodeSmokeColor(u64 value)noexcept;

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const u32 value)noexcept{ return EncodeSmokeColor(static_cast<u64>(value)); }

[[nodiscard]] inline Impl::Ui::Color EncodeSmokeColor(const f32 value)noexcept{ return EncodeSmokeColor(static_cast<u64>(Max(0.0f, value) + 0.5f)); }

// Lanes are x, y, width and height.
[[nodiscard]] inline Impl::Ui::Rect StoreSmokeRect(const SIMDVectorSingle rectangle)noexcept{ return { VectorGetX(rectangle), VectorGetY(rectangle), VectorGetZ(rectangle), VectorGetW(rectangle) }; }

[[nodiscard]] inline b8 SameSmokeRect(const Impl::Ui::Rect& left, const Impl::Ui::Rect& right)noexcept{ return left.x == right.x && left.y == right.y && left.width == right.width && left.height == right.height; }

// Compute expected row height from the accepted content extent.
[[nodiscard]] Expected<Impl::Ui::Rect> SmokeListRowBounds(const Impl::Ui::ScrollPlacement& placement, u64 index)noexcept;

[[nodiscard]] Impl::Ui::Rect VisibleSmokeListRow(const Impl::Ui::ScrollPlacement& placement, u64 index)noexcept;

void LogSmokeRect(TStringView tag, u32 sequence, TStringView name, const Impl::Ui::Rect& rectangle);

[[nodiscard]] u64 HashSmokeText(AStringView text)noexcept;

void ObserveSmokeDisplay(TStringView tag, const Impl::Ui::DisplayMetrics& display, Impl::Ui::DisplayMetrics& inOutLastDisplay, b8& inOutDisplayChanged);

template<typename ValuesT>
inline void PaintSingleSmokeValueMarkers(const ValuesT& values, const u32 sequence, Impl::UiPaintContext& context){
    const auto positions = BuildSmokeMarkerPositions<TupleSize_V<ValuesT> + 1u, 12u, 20u>();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= values.size(); ++index){
        const u32 value = index == values.size() ? sequence : values[index];
        context.paint.fillRect({ positions[index], y, 14.0f, 12.0f }, EncodeSmokeColor(value));
    }
}

template<u32 Step, u32 Width, u32 Height = 12u, u32 BottomOffset = 20u, typename ValuesT>
inline void PaintKeyedSmokeValueMarkers(const ValuesT& values, const u64 sequence, Impl::UiPaintContext& context){
    const auto positions = BuildSmokeMarkerPositions<(TupleSize_V<ValuesT> + 1u) * 2u, 12u, Step>();
    const f32 y = context.display.logicalHeight - static_cast<f32>(BottomOffset);
    for(usize index = 0u; index <= values.size(); ++index){
        const u64 value = index == values.size() ? sequence : values[index];
        for(u32 part = 0u; part < 2u; ++part){
            const usize marker = index * 2u + part;
            context.paint.fillRect({ positions[marker], y, static_cast<f32>(Width), static_cast<f32>(Height) }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
}

// Progress, slider, and image snapshots paint one 12-bit marker swatch per value part plus sequence swatches along the bottom edge.
template<typename ValuesT, typename BitsT>
inline void PaintSmokeValueMarkers(const ValuesT& values, const BitsT& bits, const u64 sequence, Impl::UiPaintContext& context){
    const f32 y = context.display.logicalHeight - 18.0f;
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

[[nodiscard]] inline u32 HashSmokeText32(const AStringView text)noexcept{ return static_cast<u32>(HashSmokeText(text)); }

[[nodiscard]] inline b8 SameSmokePlacement(const Impl::Ui::EditBoxPlacement& left, const Impl::Ui::EditBoxPlacement& right)noexcept{
    return SameSmokeRect(left.bounds, right.bounds) && SameSmokeRect(left.content, right.content) && SameSmokeRect(left.caret, right.caret) && left.scroll == right.scroll;
}

void LogSmokeEditGeometry(TStringView tag, u32 sequence, TStringView field, const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

