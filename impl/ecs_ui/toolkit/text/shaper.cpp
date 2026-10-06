// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "shaper.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_shaper{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static usize ClusterEnd(const PaintVector<RawShapedGlyph>& glyphs, usize begin)noexcept{
    usize end = begin + 1u;
    while(end < glyphs.size() && glyphs[end].byteBegin == glyphs[begin].byteBegin)
        ++end;
    return end;
}

[[nodiscard]] static bool HasMissing(const PaintVector<RawShapedGlyph>& glyphs, usize begin, usize end)noexcept{
    for(usize index = begin; index < end; ++index){
        if(glyphs[index].glyphId == 0u)
            return true;
    }
    return false;
}

static void Append(ShapedRun& run, const SharedFontFace& face, const PaintVector<RawShapedGlyph>& glyphs, usize begin, usize end){
    for(usize index = begin; index < end; ++index){
        const RawShapedGlyph& glyph = glyphs[index];
        run.glyphs.push_back({ face, glyph.glyphId, glyph.byteBegin, glyph.byteEnd,
            glyph.offset, glyph.advance, glyph.ink, glyph.coverage });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextShaper::TextShaper(Core::Alloc::GlobalArena& arena)
    : m_arena(arena)
    , m_fonts(arena)
    , m_primary(arena)
    , m_fallback(arena)
    , m_spans(arena)
{}

bool TextShaper::setFonts(const FontSource* sources, usize count){
    if(!sources || count == 0u || count > 8u)
        return false;
    PaintVector<SharedFontFace> fonts(m_arena);
    fonts.reserve(count);
    for(usize index = 0u; index < count; ++index){
        const FontSource& source = sources[index];
        if(!source.identity.valid() || source.generation == 0u || !source.font.validatePayload())
            return false;
        SharedFontFace face = MakeFontFace(m_arena, source);
        if(!face || !face->valid())
            return false;
        fonts.push_back(Move(face));
    }
    m_fonts = Move(fonts);
    return true;
}

TextLayoutStatus::Enum TextShaper::shape(const ShapeRequest& request, ShapedRun& output){
    const TextLayoutStatus::Enum validation = ValidateTextRequest(request, false);
    if(validation != TextLayoutStatus::Success)
        return validation;
    if(m_fonts.empty())
        return TextLayoutStatus::FontFailure;
    ShapedRun run(m_arena);
    if(
        !m_fonts[0]->metrics(request.fontSize, run.metrics)
        || !m_fonts[0]->shape(request, 0u, static_cast<u32>(request.text.size()), m_primary)
    )
        return TextLayoutStatus::FontFailure;
    run.glyphs.reserve(m_primary.size());
    m_spans.clear();
    m_spans.reserve(m_primary.size());
    usize begin = 0u;
    while(begin < m_primary.size()){
        const usize end = __hidden_ui_text_shaper::ClusterEnd(m_primary, begin);
        FontSpan span{ 0u, begin, end, m_primary[begin].byteBegin, m_primary[begin].byteEnd };
        if(__hidden_ui_text_shaper::HasMissing(m_primary, begin, end)){
            span.fontIndex = m_fonts.size();
            for(usize fontIndex = 1u; fontIndex < m_fonts.size(); ++fontIndex){
                if(!m_fonts[fontIndex]->shape(request, span.byteBegin, span.byteEnd, m_fallback))
                    return TextLayoutStatus::FontFailure;
                if(__hidden_ui_text_shaper::HasMissing(m_fallback, 0u, m_fallback.size()))
                    continue;
                span.fontIndex = fontIndex;
                break;
            }
            if(span.fontIndex == m_fonts.size())
                return TextLayoutStatus::MissingGlyph;
        }
        if(!m_spans.empty() && m_spans.back().fontIndex == span.fontIndex){
            FontSpan& previous = m_spans.back();
            previous.glyphEnd = span.glyphEnd;
            previous.byteBegin = Min(previous.byteBegin, span.byteBegin);
            previous.byteEnd = Max(previous.byteEnd, span.byteEnd);
        }
        else
            m_spans.push_back(span);
        begin = end;
    }
    for(const FontSpan& span : m_spans){
        const SharedFontFace& face = m_fonts[span.fontIndex];
        if(span.fontIndex == 0u)
            __hidden_ui_text_shaper::Append(run, face, m_primary, span.glyphBegin, span.glyphEnd);
        else{
            if(!face->shape(request, span.byteBegin, span.byteEnd, m_fallback))
                return TextLayoutStatus::FontFailure;
            if(__hidden_ui_text_shaper::HasMissing(m_fallback, 0u, m_fallback.size()))
                return TextLayoutStatus::MissingGlyph;
            FontMetrics metrics;
            if(!face->metrics(request.fontSize, metrics))
                return TextLayoutStatus::FontFailure;
            run.metrics.ascender = Max(run.metrics.ascender, metrics.ascender);
            run.metrics.descender = Max(run.metrics.descender, metrics.descender);
            run.metrics.lineGap = Max(run.metrics.lineGap, metrics.lineGap);
            __hidden_ui_text_shaper::Append(run, face, m_fallback, 0u, m_fallback.size());
        }
    }
    output = Move(run);
    return TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

