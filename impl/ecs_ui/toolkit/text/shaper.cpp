// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shaper.h"

#include <global/simd/vector_arithmetic.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_shaper{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void Append(const SharedFontFace& face, const Span<const RawShapedGlyph> glyphs, usize begin, usize end, ShapedRun& run){
    for(usize index = begin; index < end; ++index){
        const RawShapedGlyph& glyph = glyphs[index];
        run.glyphs.push_back({ face, glyph.glyphId, glyph.byteBegin, glyph.byteEnd, glyph.offset, glyph.advance, glyph.ink, glyph.coverage });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 TextShaper::setFonts(Core::Alloc::ScratchArena& scratchArena, const Span<const FontSource> sources){
    const usize count = sources.size();
    if(count == 0u || count > s_TextMaxFontSources)
        return false;
    PaintVector<SharedFontFace> fonts(m_arena);
    fonts.reserve(count);
    for(usize index = 0u; index < count; ++index){
        const FontSource& source = sources[index];
        if(!source.identity.valid() || source.generation == 0u || !source.font.validatePayload())
            return false;
        auto face = MakeFontFace(scratchArena, m_arena, source);
        if(!face || !(*face)->valid())
            return false;
        fonts.push_back(Move(*face));
    }
    m_fonts = Move(fonts);
    return true;
}

Expected<ShapedRun, TextLayoutStatus::Enum> TextShaper::shape(const ShapeRequest& request){
    const TextLayoutStatus::Enum validation = ValidateTextRequest(request, false);
    if(validation != TextLayoutStatus::Success)
        return MakeUnexpected(validation);
    if(m_fonts.empty())
        return MakeUnexpected(TextLayoutStatus::FontFailure);
    ShapedRun run(m_arena);
    const auto primaryMetrics = m_fonts[0]->metrics(request.fontSize);
    if(!primaryMetrics || !m_fonts[0]->shape(request, 0u, static_cast<u32>(request.text.size()), m_primary))
        return MakeUnexpected(TextLayoutStatus::FontFailure);
    run.metrics = *primaryMetrics;
    run.glyphs.reserve(m_primary.size());
    m_spans.clear();
    m_spans.reserve(m_primary.size());
    usize begin = 0u;
    while(begin < m_primary.size()){
        const usize end = FindClusterEnd(m_primary, begin);
        TextFontSpan span{ 0u, begin, end, m_primary[begin].byteBegin, m_primary[begin].byteEnd };
        if(ClusterHasMissingGlyph(m_primary, begin, end)){
            span.fontIndex = m_fonts.size();
            for(usize fontIndex = 1u; fontIndex < m_fonts.size(); ++fontIndex){
                if(!m_fonts[fontIndex]->shape(request, span.byteBegin, span.byteEnd, m_fallback))
                    return MakeUnexpected(TextLayoutStatus::FontFailure);
                if(ClusterHasMissingGlyph(m_fallback, 0u, m_fallback.size()))
                    continue;
                span.fontIndex = fontIndex;
                break;
            }
            if(span.fontIndex == m_fonts.size())
                return MakeUnexpected(TextLayoutStatus::MissingGlyph);
        }
        if(!m_spans.empty() && m_spans.back().fontIndex == span.fontIndex){
            TextFontSpan& previous = m_spans.back();
            previous.glyphEnd = span.glyphEnd;
            previous.byteBegin = Min(previous.byteBegin, span.byteBegin);
            previous.byteEnd = Max(previous.byteEnd, span.byteEnd);
        }
        else
            m_spans.push_back(span);
        begin = end;
    }
    for(const TextFontSpan& span : m_spans){
        const SharedFontFace& face = m_fonts[span.fontIndex];
        if(span.fontIndex == 0u)
            __hidden_ui_text_shaper::Append(face, m_primary, span.glyphBegin, span.glyphEnd, run);
        else{
            if(!face->shape(request, span.byteBegin, span.byteEnd, m_fallback))
                return MakeUnexpected(TextLayoutStatus::FontFailure);
            if(ClusterHasMissingGlyph(m_fallback, 0u, m_fallback.size()))
                return MakeUnexpected(TextLayoutStatus::MissingGlyph);
            const auto metrics = face->metrics(request.fontSize);
            if(!metrics)
                return MakeUnexpected(TextLayoutStatus::FontFailure);
            const SIMDVectorSingle currentMetrics = VectorSet(run.metrics.ascender, run.metrics.descender, run.metrics.lineGap, 0.0f);
            const SIMDVectorSingle candidateMetrics = VectorSet(metrics->ascender, metrics->descender, metrics->lineGap, 0.0f);
            const SIMDVectorSingle maximumMetrics = VectorSelect(candidateMetrics, currentMetrics, VectorGreater(currentMetrics, candidateMetrics));
            run.metrics.ascender = VectorGetX(maximumMetrics);
            run.metrics.descender = VectorGetY(maximumMetrics);
            run.metrics.lineGap = VectorGetZ(maximumMetrics);
            __hidden_ui_text_shaper::Append(face, m_fallback, 0u, m_fallback.size(), run);
        }
    }
    return run;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

