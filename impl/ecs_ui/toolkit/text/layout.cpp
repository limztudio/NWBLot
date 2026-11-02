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


[[nodiscard]] static b8 ValidateRun(const ShapedRun& run, const StringView text, const TextDirection::Enum direction)noexcept{
    if(!ValidateShapedRunMetrics(run.metrics, run.glyphs.size()))
        return false;
    u32 previous = direction == TextDirection::LeftToRight ? 0u : static_cast<u32>(text.size());
    for(const ShapedGlyph& glyph : run.glyphs){
        if(!ValidateShapedGlyph(glyph.byteBegin, glyph.byteEnd, glyph.offset, glyph.advance, glyph.ink, glyph.coverage, text, direction, previous))
            return false;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


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
    Double4 inkBounds(0.0, 0.0, 0.0, 0.0);
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
                Double4 ink;
                if(!TryTranslateTextInk(position, glyph.ink, ink) || !TryIncludeTextInk(ink, inkBounds, hasInk))
                    return MakeUnexpected(TextLayoutStatus::InvalidParameters);
                Double4 translatedCoverage;
                if(glyph.coverage.known && !TryTranslateTextInk(position, glyph.coverage.ink, translatedCoverage))
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
    result.m_inkBounds = { static_cast<f32>(inkBounds.x), static_cast<f32>(inkBounds.y), static_cast<f32>(inkBounds.z), static_cast<f32>(inkBounds.w) };
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

