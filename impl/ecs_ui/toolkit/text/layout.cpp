// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout.h"

#include <global/simd/vector_arithmetic.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ByteBoundary(StringView text, u32 offset)noexcept{
    return offset <= text.size() && (offset == text.size() || (static_cast<u8>(text[offset]) & 0xc0u) != 0x80u);
}

[[nodiscard]] static bool FitsCoordinate(f64 value)noexcept{
    return IsFinite(value) && value >= -static_cast<f64>(Limit<f32>::s_Max) && value <= static_cast<f64>(Limit<f32>::s_Max);
}

[[nodiscard]] static Expected<SIMDVectorDouble> TranslateInk(const SIMDVectorDouble position, const SIMDVectorDouble ink)noexcept{
    const SIMDVectorDouble origin = VectorDoubleAdd(position, ink);
    const SIMDVectorDouble end = VectorDoubleAdd(origin, VectorDoubleSwizzle<2, 3, 2, 3>(ink));
    if(!FitsCoordinate(VectorDoubleGetX(origin)) || !FitsCoordinate(VectorDoubleGetY(origin)) || !FitsCoordinate(VectorDoubleGetX(end)) || !FitsCoordinate(VectorDoubleGetY(end)))
        return MakeUnexpected(Failure{});
    const SIMDVectorSingle rectangle = ConvertVectorDoubleToSingle(VectorDoubleSelect(ink, origin, s_DoubleSIMDMaskXY));
    const SIMDVectorSingle roundedEnd = VectorAdd(VectorSelect(VectorZero(), rectangle, s_SIMDMaskXY), VectorSwizzle<2, 3, 2, 3>(rectangle));
    if(!IsFinite(VectorGetX(roundedEnd)) || !IsFinite(VectorGetY(roundedEnd)))
        return MakeUnexpected(Failure{});
    return ConvertVectorSingleToDouble(rectangle);
}

[[nodiscard]] static bool ValidateRun(const ShapedRun& run, StringView text, TextDirection::Enum direction)noexcept{
    if(
        !IsFinite(run.metrics.ascender) || !IsFinite(run.metrics.descender) || !IsFinite(run.metrics.lineGap)
        || run.metrics.ascender < 0.0f || run.metrics.descender < 0.0f || run.metrics.lineGap < 0.0f
        || run.metrics.ascender + run.metrics.descender <= 0.0f || run.glyphs.size() > s_TextMaxBytes
        || !FitsCoordinate(static_cast<f64>(run.metrics.ascender) + run.metrics.descender + run.metrics.lineGap)
    )
        return false;
    u32 previous = direction == TextDirection::LeftToRight ? 0u : static_cast<u32>(text.size());
    for(const ShapedGlyph& glyph : run.glyphs){
        if(
            glyph.byteBegin >= glyph.byteEnd || !ByteBoundary(text, glyph.byteBegin) || !ByteBoundary(text, glyph.byteEnd)
            || !IsFinite(glyph.advance.x) || !IsFinite(glyph.advance.y) || glyph.advance.x < 0.0f || glyph.advance.y != 0.0f
            || !IsFinite(glyph.offset.x) || !IsFinite(glyph.offset.y) || !IsFinite(glyph.ink.x) || !IsFinite(glyph.ink.y)
            || !IsFinite(glyph.ink.width) || !IsFinite(glyph.ink.height) || glyph.ink.width < 0.0f || glyph.ink.height < 0.0f
            || !FitsCoordinate(static_cast<f64>(glyph.ink.x) + glyph.ink.width)
            || !FitsCoordinate(static_cast<f64>(glyph.ink.y) + glyph.ink.height)
        )
            return false;
        if(glyph.coverage.known){
            const Rect& coverage = glyph.coverage.ink;
            if(
                !IsFinite(coverage.x) || !IsFinite(coverage.y) || !IsFinite(coverage.width) || !IsFinite(coverage.height)
                || coverage.width < 0.0f || coverage.height < 0.0f
                || !FitsCoordinate(static_cast<f64>(coverage.x) + coverage.width)
                || !FitsCoordinate(static_cast<f64>(coverage.y) + coverage.height)
            )
                return false;
        }
        if(direction == TextDirection::LeftToRight ? glyph.byteBegin < previous : glyph.byteBegin > previous)
            return false;
        previous = glyph.byteBegin;
    }
    return true;
}

[[nodiscard]] static bool IncludeInk(SIMDVectorDouble& bounds, bool& hasInk, const SIMDVectorDouble ink)noexcept{
    if(VectorDoubleGetZ(ink) <= 0.0 || VectorDoubleGetW(ink) <= 0.0)
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
    if(!FitsCoordinate(VectorDoubleGetX(size)) || !FitsCoordinate(VectorDoubleGetY(size)))
        return false;
    const SIMDVectorDouble rectangle = VectorDoubleSelect(VectorDoubleSwizzle<0, 1, 0, 1>(size), origin, s_DoubleSIMDMaskXY);
    const SIMDVectorSingle rounded = ConvertVectorDoubleToSingle(rectangle);
    const SIMDVectorSingle roundedEnd = VectorAdd(VectorSelect(VectorZero(), rounded, s_SIMDMaskXY), VectorSwizzle<2, 3, 2, 3>(rounded));
    if(!IsFinite(VectorGetX(roundedEnd)) || !IsFinite(VectorGetY(roundedEnd)))
        return false;
    bounds = ConvertVectorSingleToDouble(rounded);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayout::TextLayout(Core::Alloc::GlobalArena& arena)noexcept
    : m_text(arena)
    , m_glyphs(arena)
    , m_clusters(arena)
    , m_lines(arena)
{}

TextHit TextLayout::hitTest(Point point)const noexcept{
    TextHit hit;
    if(m_lines.empty() || !IsFinite(point.x) || !IsFinite(point.y))
        return hit;
    usize lineIndex = m_lines.size() - 1u;
    for(usize index = 0u; index < m_lines.size(); ++index){
        if(point.y < m_lines[index].top + m_lines[index].height){
            lineIndex = index;
            break;
        }
    }
    const TextLine& line = m_lines[lineIndex];
    hit.lineIndex = static_cast<u32>(lineIndex);
    hit.byteOffset = line.byteBegin;
    hit.inside = point.x >= 0.0f && point.x <= line.advance && point.y >= line.top && point.y < line.top + line.height;
    if(line.clusterCount == 0u)
        return hit;
    f32 nearest = Limit<f32>::s_Max;
    for(u32 index = line.firstCluster; index < line.firstCluster + line.clusterCount; ++index){
        const TextCluster& cluster = m_clusters[index];
        const SIMDVectorSingle difference = VectorSubtract(VectorReplicate(point.x), VectorSet(cluster.leadingX, cluster.trailingX, cluster.leadingX, cluster.trailingX));
        const SIMDVectorSingle magnitude = VectorSelect(difference, VectorNegate(difference), VectorLess(difference, VectorZero()));
        const f32 leadingDistance = VectorGetX(magnitude);
        const f32 trailingDistance = VectorGetY(magnitude);
        if(leadingDistance <= nearest){
            nearest = leadingDistance;
            hit.byteOffset = cluster.byteBegin;
            hit.edge = TextCaretEdge::Leading;
        }
        if(trailingDistance < nearest){
            nearest = trailingDistance;
            hit.byteOffset = cluster.byteEnd;
            hit.edge = TextCaretEdge::Trailing;
        }
    }
    return hit;
}

Expected<Rect> TextLayout::caretRect(u32 byteOffset, TextCaretEdge::Enum edge)const noexcept{
    if(edge != TextCaretEdge::Leading && edge != TextCaretEdge::Trailing)
        return MakeUnexpected(Failure{});
    const TextCluster* alternate = nullptr;
    for(const TextCluster& cluster : m_clusters){
        if(edge == TextCaretEdge::Leading ? cluster.byteBegin == byteOffset : cluster.byteEnd == byteOffset){
            const TextLine& line = m_lines[cluster.lineIndex];
            return Rect{ edge == TextCaretEdge::Leading ? cluster.leadingX : cluster.trailingX, line.top, 0.0f, line.height };
        }
        if(cluster.byteBegin == byteOffset || cluster.byteEnd == byteOffset)
            alternate = &cluster;
    }
    if(alternate){
        const TextLine& line = m_lines[alternate->lineIndex];
        const f32 x = alternate->byteBegin == byteOffset ? alternate->leadingX : alternate->trailingX;
        return Rect{ x, line.top, 0.0f, line.height };
    }
    for(const TextLine& line : m_lines){
        if(byteOffset == line.byteBegin || byteOffset == line.byteEnd){
            const bool atBegin = byteOffset == line.byteBegin;
            const f32 x = (atBegin == (m_direction == TextDirection::LeftToRight)) ? 0.0f : line.advance;
            return Rect{ x, line.top, 0.0f, line.height };
        }
    }
    return MakeUnexpected(Failure{});
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutBuilder::TextLayoutBuilder(Core::Alloc::GlobalArena& arena, ITextShaper& shaper)noexcept
    : m_arena(arena)
    , m_shaper(shaper)
{}

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
    bool hasInk = false;
    SIMDVectorDouble inkBounds = VectorDoubleZero();
    usize byteBegin = 0u;
    while(true){
        usize byteEnd = byteBegin;
        while(byteEnd < request.text.size() && request.text[byteEnd] != '\r' && request.text[byteEnd] != '\n')
            ++byteEnd;
        usize breakEnd = byteEnd;
        if(breakEnd < request.text.size())
            breakEnd += request.text[breakEnd] == '\r' ? 2u : 1u;
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
        const SIMDVectorDouble baselineAndAscent = VectorDoubleAdd(VectorDoubleSet(line.top, run.metrics.ascender, 0.0, 0.0), VectorDoubleSet(run.metrics.ascender, run.metrics.descender, 0.0, 0.0));
        const SIMDVectorDouble baselineHeightValue = VectorDoubleSelect(baselineAndAscent, VectorDoubleAdd(baselineAndAscent, VectorDoubleSet(0.0, run.metrics.lineGap, 0.0, 0.0)), s_DoubleSIMDMaskY);
        const f64 baseline = VectorDoubleGetX(baselineHeightValue);
        const f64 height = VectorDoubleGetY(baselineHeightValue);
        if(!__hidden_ui_text_layout::FitsCoordinate(baseline) || !__hidden_ui_text_layout::FitsCoordinate(line.top + height))
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
                const SIMDVectorDouble placement = VectorDoubleAdd(VectorDoubleSet(pen, line.baseline, pen, 0.0), VectorDoubleSet(glyph.offset.x, glyph.offset.y, glyph.advance.x, 0.0));
                const f64 x = VectorDoubleGetX(placement);
                const f64 y = VectorDoubleGetY(placement);
                const f64 nextPen = VectorDoubleGetZ(placement);
                if(!__hidden_ui_text_layout::FitsCoordinate(x) || !__hidden_ui_text_layout::FitsCoordinate(y) || !__hidden_ui_text_layout::FitsCoordinate(nextPen))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                const Point position{ static_cast<f32>(x), static_cast<f32>(y) };
                const SIMDVectorDouble positionValue = VectorDoubleSet(position.x, position.y, 0.0, 0.0);
                const auto ink = __hidden_ui_text_layout::TranslateInk(positionValue, VectorDoubleSet(glyph.ink.x, glyph.ink.y, glyph.ink.width, glyph.ink.height));
                if(!ink || !__hidden_ui_text_layout::IncludeInk(inkBounds, hasInk, *ink))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                if(glyph.coverage.known && !__hidden_ui_text_layout::TranslateInk(positionValue, VectorDoubleSet(glyph.coverage.ink.x, glyph.coverage.ink.y, glyph.coverage.ink.width, glyph.coverage.ink.height)))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                result.m_glyphs.push_back({ glyph.face, glyph.glyphId,
                    line.byteBegin + glyph.byteBegin, line.byteBegin + glyph.byteEnd, position, glyph.ink, glyph.coverage }
                );
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
        const SIMDVectorSingle measured = VectorSet(result.m_measure.x, result.m_measure.y, 0.0f, 0.0f);
        const SIMDVectorSingle advance = VectorSet(pen, line.height, 0.0f, 0.0f);
        const SIMDVectorSingle widest = VectorSelect(advance, measured, VectorGreater(measured, advance));
        const SIMDVectorSingle accumulated = VectorAdd(VectorSelect(VectorZero(), measured, s_SIMDMaskY), VectorSelect(VectorZero(), advance, s_SIMDMaskY));
        const SIMDVectorSingle extent = VectorSelect(widest, accumulated, s_SIMDMaskY);
        result.m_measure = { VectorGetX(extent), VectorGetY(extent) };
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

