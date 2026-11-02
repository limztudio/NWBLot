// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>

#include <global/simd/result.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 FitsCoordinateXY(const SIMDVectorDouble value)noexcept{
    const SIMDVectorDouble limit = VectorDoubleReplicate(static_cast<f64>(Limit<f32>::s_Max));
    return VectorDoubleIsFinite(value, VectorDoubleComponentMask::s_DoubleXY) && (VectorDoubleMoveMask(VectorDoubleInBounds(value, limit)) & VectorDoubleComponentMask::s_DoubleXY) == VectorDoubleComponentMask::s_DoubleXY;
}

[[nodiscard]] static Expected<SIMDVectorDoubleResult> TranslateInk(const SIMDVectorDouble position, const SIMDVectorDouble ink)noexcept{
    const SIMDVectorDouble origin = VectorDoubleAdd(position, ink);
    const SIMDVectorDouble end = VectorDoubleAdd(origin, VectorDoubleSwizzle<2, 3, 2, 3>(ink));
    if(!FitsCoordinateXY(origin) || !FitsCoordinateXY(end))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle rectangle = ConvertVectorDoubleToSingle(VectorDoubleSelect(ink, origin, s_DoubleSIMDMaskXY));
    const SIMDVectorSingle roundedEnd = VectorAdd(VectorSelect(VectorZero(), rectangle, s_SIMDMaskXY), VectorSwizzle<2, 3, 2, 3>(rectangle));
    if(!VectorIsFinite(roundedEnd, VectorComponentMask::s_XY))
        return MakeUnexpected(Failure{});
    return SIMDVectorDoubleResult{ ConvertVectorSingleToDouble(rectangle) };
}

[[nodiscard]] static b8 ValidateRun(const ShapedRun& run, StringView text, TextDirection::Enum direction)noexcept{
    if(
        !IsFinite(run.metrics.ascender) || !IsFinite(run.metrics.descender) || !IsFinite(run.metrics.lineGap)
        || run.metrics.ascender < 0.0f || run.metrics.descender < 0.0f || run.metrics.lineGap < 0.0f
        || run.metrics.ascender + run.metrics.descender <= 0.0f || run.glyphs.size() > s_TextMaxBytes
        || !FitsTextCoordinate(static_cast<f64>(run.metrics.ascender) + static_cast<f64>(run.metrics.descender) + static_cast<f64>(run.metrics.lineGap))
    )
        return false;
    u32 previous = direction == TextDirection::LeftToRight ? 0u : static_cast<u32>(text.size());
    for(const ShapedGlyph& glyph : run.glyphs){
        if(
            glyph.byteBegin >= glyph.byteEnd || !IsUtf8ScalarBoundary(text, glyph.byteBegin) || !IsUtf8ScalarBoundary(text, glyph.byteEnd)
            || !IsFinite(glyph.advance.x) || !IsFinite(glyph.advance.y) || glyph.advance.x < 0.0f || glyph.advance.y != 0.0f
            || !IsFinite(glyph.offset.x) || !IsFinite(glyph.offset.y) || !HasValidUiRectDimensions(glyph.ink)
            || !FitsTextCoordinate(static_cast<f64>(glyph.ink.x) + static_cast<f64>(glyph.ink.width))
            || !FitsTextCoordinate(static_cast<f64>(glyph.ink.y) + static_cast<f64>(glyph.ink.height))
        )
            return false;
        if(glyph.coverage.known){
            const Rect& coverage = glyph.coverage.ink;
            if(
                !HasValidUiRectDimensions(coverage)
                || !FitsTextCoordinate(static_cast<f64>(coverage.x) + static_cast<f64>(coverage.width))
                || !FitsTextCoordinate(static_cast<f64>(coverage.y) + static_cast<f64>(coverage.height))
            )
                return false;
        }
        if(direction == TextDirection::LeftToRight ? glyph.byteBegin < previous : glyph.byteBegin > previous)
            return false;
        previous = glyph.byteBegin;
    }
    return true;
}

[[nodiscard]] static b8 IncludeInk(const SIMDVectorDouble ink, SIMDVectorDouble& bounds, b8& hasInk)noexcept{
    if((VectorDoubleMoveMask(VectorDoubleLessOrEqual(VectorDoubleSwizzle<2, 3, 2, 3>(ink), VectorDoubleZero())) & VectorDoubleComponentMask::s_DoubleXY) != 0u)
        return true;
    if(!hasInk){
        bounds = ink;
        hasInk = true;
        return true;
    }
    const SIMDVectorDouble origin = VectorDoubleSelect(ink, bounds, VectorDoubleLess(bounds, ink));
    const SIMDVectorDouble boundsEnd = VectorDoubleAdd(bounds, VectorDoubleSwizzle<2, 3, 2, 3>(bounds));
    const SIMDVectorDouble inkEnd = VectorDoubleAdd(ink, VectorDoubleSwizzle<2, 3, 2, 3>(ink));
    const SIMDVectorDouble end = VectorDoubleSelect(inkEnd, boundsEnd, VectorDoubleGreater(boundsEnd, inkEnd));
    const SIMDVectorDouble size = VectorDoubleSubtract(end, origin);
    if(!FitsCoordinateXY(size))
        return false;
    const SIMDVectorDouble rectangle = VectorDoubleSelect(VectorDoubleSwizzle<0, 1, 0, 1>(size), origin, s_DoubleSIMDMaskXY);
    const SIMDVectorSingle rounded = ConvertVectorDoubleToSingle(rectangle);
    const SIMDVectorSingle roundedEnd = VectorAdd(VectorSelect(VectorZero(), rounded, s_SIMDMaskXY), VectorSwizzle<2, 3, 2, 3>(rounded));
    if(!VectorIsFinite(roundedEnd, VectorComponentMask::s_XY))
        return false;
    bounds = ConvertVectorSingleToDouble(rounded);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Rect> TextLayout::caretRect(u32 byteOffset, TextCaretEdge::Enum edge)const noexcept{
    Rect rect;
    if(!TryResolveTextCaretRect(m_lines, m_clusters, m_direction, byteOffset, edge, rect))
        return MakeUnexpected(Failure{});
    return rect;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<TextLayout, TextLayoutStatus::Enum> TextLayoutBuilder::layout(const ShapeRequest& request){
    const TextLayoutStatus::Enum validation = ValidateTextRequest(request, true);
    if(validation != TextLayoutStatus::Success)
        return MakeUnexpected(validation);
    TextLayout result(m_arena);
    if(!request.text.empty())
        result.m_text.assign(request.text.data(), request.text.size());
    result.m_fontSize = request.fontSize;
    result.m_direction = request.direction;
    result.m_glyphs.reserve(request.text.size());
    result.m_clusters.reserve(request.text.size());
    result.m_lines.reserve(1u);
    b8 hasInk = false;
    SIMDVectorDouble inkBounds = VectorDoubleZero();
    usize byteBegin = 0u;
    while(true){
        usize byteEnd = byteBegin;
        while(byteEnd < request.text.size() && request.text[byteEnd] != '\r' && request.text[byteEnd] != '\n')
            ++byteEnd;
        usize breakEnd = byteEnd;
        if(breakEnd < request.text.size())
            breakEnd += request.text[breakEnd] == '\r' ? s_TextCarriageReturnLineFeedBytes : 1u;
        ShapeRequest lineRequest = request;
        lineRequest.text = request.text.substr(byteBegin, byteEnd - byteBegin);
        const auto shapedRun = m_shaper.shape(lineRequest);
        if(!shapedRun)
            return MakeUnexpected(shapedRun.error());
        const ShapedRun& run = *shapedRun;
        if(!__hidden_ui_text_layout::ValidateRun(run, lineRequest.text, request.direction))
            return MakeUnexpected(TextLayoutStatus::FontFailure);
        TextLine line;
        line.byteBegin = static_cast<u32>(byteBegin);
        line.byteEnd = static_cast<u32>(byteEnd);
        line.breakEnd = static_cast<u32>(breakEnd);
        line.firstGlyph = static_cast<u32>(result.m_glyphs.size());
        line.glyphCount = static_cast<u32>(run.glyphs.size());
        line.firstCluster = static_cast<u32>(result.m_clusters.size());
        line.top = result.m_measure.y;
        const f64 baseline = static_cast<f64>(line.top) + static_cast<f64>(run.metrics.ascender);
        const f64 height = (static_cast<f64>(run.metrics.ascender) + static_cast<f64>(run.metrics.descender)) + static_cast<f64>(run.metrics.lineGap);
        if(!FitsTextCoordinate(baseline) || !FitsTextCoordinate(static_cast<f64>(line.top) + height))
            return MakeUnexpected(TextLayoutStatus::InvalidParameters);
        line.baseline = static_cast<f32>(baseline);
        line.height = static_cast<f32>(height);
        usize first = 0u;
        f32 pen = 0.0f;
        while(first < run.glyphs.size()){
            usize end = first + 1u;
            while(end < run.glyphs.size() && run.glyphs[end].byteBegin == run.glyphs[first].byteBegin)
                ++end;
            const f32 clusterLeft = pen;
            const u32 firstGlyph = static_cast<u32>(result.m_glyphs.size());
            for(usize index = first; index < end; ++index){
                const ShapedGlyph& glyph = run.glyphs[index];
                const f64 x = static_cast<f64>(pen) + static_cast<f64>(glyph.offset.x);
                const f64 y = static_cast<f64>(line.baseline) + static_cast<f64>(glyph.offset.y);
                const f64 nextPen = static_cast<f64>(pen) + static_cast<f64>(glyph.advance.x);
                if(!FitsTextCoordinate(x) || !FitsTextCoordinate(y) || !FitsTextCoordinate(nextPen))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                const Point position{ static_cast<f32>(x), static_cast<f32>(y) };
                const SIMDVectorDouble positionValue = VectorDoubleSet(position.x, position.y, 0.0, 0.0);
                const auto ink = __hidden_ui_text_layout::TranslateInk(positionValue, VectorDoubleSet(glyph.ink.x, glyph.ink.y, glyph.ink.width, glyph.ink.height));
                if(!ink || !__hidden_ui_text_layout::IncludeInk(ink->value, inkBounds, hasInk))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                if(glyph.coverage.known && !__hidden_ui_text_layout::TranslateInk(positionValue, VectorDoubleSet(glyph.coverage.ink.x, glyph.coverage.ink.y, glyph.coverage.ink.width, glyph.coverage.ink.height)))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                result.m_glyphs.push_back({ glyph.face, glyph.glyphId, line.byteBegin + glyph.byteBegin, line.byteBegin + glyph.byteEnd, position, glyph.ink, glyph.coverage });
                pen = static_cast<f32>(nextPen);
            }
            TextCluster cluster;
            cluster.byteBegin = line.byteBegin + run.glyphs[first].byteBegin;
            cluster.byteEnd = line.byteBegin + run.glyphs[first].byteEnd;
            cluster.firstGlyph = firstGlyph;
            cluster.glyphCount = static_cast<u32>(end - first);
            cluster.lineIndex = static_cast<u32>(result.m_lines.size());
            cluster.leadingX = request.direction == TextDirection::LeftToRight ? clusterLeft : pen;
            cluster.trailingX = request.direction == TextDirection::LeftToRight ? pen : clusterLeft;
            result.m_clusters.push_back(cluster);
            first = end;
        }
        if(!IsFinite(pen) || !IsFinite(line.top + line.height))
            return MakeUnexpected(TextLayoutStatus::InvalidParameters);
        line.advance = pen;
        line.clusterCount = static_cast<u32>(result.m_clusters.size()) - line.firstCluster;
        result.m_lines.push_back(line);
        result.m_measure = { Max(result.m_measure.x, pen), result.m_measure.y + line.height };
        if(breakEnd == byteEnd)
            break;
        byteBegin = breakEnd;
    }
    result.m_inkBounds = { static_cast<f32>(VectorDoubleGetX(inkBounds)), static_cast<f32>(VectorDoubleGetY(inkBounds)), static_cast<f32>(VectorDoubleGetZ(inkBounds)), static_cast<f32>(VectorDoubleGetW(inkBounds)) };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

