// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "smoke_geometry.h"

#include <global/hash_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Impl::Ui::Color EncodeSmokeColor(const u64 value)noexcept{
    const SIMDVectorSingle channels = VectorSet(static_cast<f32>(value & 15u), static_cast<f32>((value >> 4u) & 15u), static_cast<f32>((value >> 8u) & 15u), 15.0f);
    const SIMDVectorSingle color = VectorDivide(channels, VectorReplicate(15.0f));
    return { VectorGetX(color), VectorGetY(color), VectorGetZ(color), 1.0f };
}

Expected<Impl::Ui::Rect> SmokeListRowBounds(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    if(placement.rowCount == 0u)
        return MakeUnexpected(Failure{});
    const f32 height = static_cast<f32>(static_cast<f64>(placement.contentHeight) / static_cast<f64>(placement.rowCount));
    return Impl::Ui::ScrollLayout::RowBounds(index, placement, height);
}

Impl::Ui::Rect VisibleSmokeListRow(const Impl::Ui::ScrollPlacement& placement, const u64 index)noexcept{
    const auto rectangle = SmokeListRowBounds(placement, index);
    if(!rectangle)
        return {};
    Impl::Ui::Rect visible;
    return Impl::Ui::TryIntersectUiRects(*rectangle, placement.contentClip, Impl::Ui::UiRectPrecision::PreciseExtents, visible) ? visible : Impl::Ui::Rect{};
}

void LogSmokeRect(const TStringView tag, const u32 sequence, const TStringView name, const Impl::Ui::Rect& rectangle){
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: geometry sequence={} {}={},{},{},{}"), tag, sequence, name, rectangle.x, rectangle.y, rectangle.width, rectangle.height);
}

u64 HashSmokeText(const AStringView text)noexcept{
    return ComputeFnv32TextHash(text);
}

void ObserveSmokeDisplay(const TStringView tag, const Impl::Ui::DisplayMetrics& display, Impl::Ui::DisplayMetrics& inOutLastDisplay, b8& inOutDisplayChanged){
    if(inOutLastDisplay.logicalWidth == display.logicalWidth && inOutLastDisplay.logicalHeight == display.logicalHeight && inOutLastDisplay.pixelScaleX == display.pixelScaleX && inOutLastDisplay.pixelScaleY == display.pixelScaleY)
        return;
    inOutLastDisplay = display;
    inOutDisplayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}: display logical={}x{} scale={}x{}"), tag, display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY);
}

void LogSmokeEditGeometry(const TStringView tag, const u32 sequence, const TStringView field, const Impl::Ui::EditBoxPlacement& placement, const Impl::Ui::Rect& selection){
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

