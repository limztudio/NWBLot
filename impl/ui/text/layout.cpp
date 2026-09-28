// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_text_layout{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool ByteBoundary(StringView text, u32 offset){
    return offset <= text.size() && (offset == text.size() || (static_cast<u8>(text[offset]) & 0xc0u) != 0x80u);
}

[[nodiscard]] static bool ValidateRun(const ShapedRun& run, StringView text, TextDirection::Enum direction){
    if(
        !IsFinite(run.metrics.ascender) || !IsFinite(run.metrics.descender) || !IsFinite(run.metrics.lineGap)
        || run.metrics.ascender < 0.0f || run.metrics.descender < 0.0f || run.metrics.lineGap < 0.0f
        || run.metrics.ascender + run.metrics.descender <= 0.0f || run.glyphs.size() > s_TextMaxBytes
    )
        return false;
    u32 previous = direction == TextDirection::LeftToRight ? 0u : static_cast<u32>(text.size());
    for(const ShapedGlyph& glyph : run.glyphs){
        if(
            glyph.byteBegin >= glyph.byteEnd || !ByteBoundary(text, glyph.byteBegin) || !ByteBoundary(text, glyph.byteEnd)
            || !IsFinite(glyph.advance.x) || !IsFinite(glyph.advance.y) || glyph.advance.x < 0.0f || glyph.advance.y != 0.0f
            || !IsFinite(glyph.offset.x) || !IsFinite(glyph.offset.y) || !IsFinite(glyph.ink.x) || !IsFinite(glyph.ink.y)
            || !IsFinite(glyph.ink.width) || !IsFinite(glyph.ink.height) || glyph.ink.width < 0.0f || glyph.ink.height < 0.0f
        )
            return false;
        if(direction == TextDirection::LeftToRight ? glyph.byteBegin < previous : glyph.byteBegin > previous)
            return false;
        previous = glyph.byteBegin;
    }
    return true;
}

static void IncludeInk(Rect& bounds, bool& hasInk, const Rect& ink){
    if(ink.width <= 0.0f || ink.height <= 0.0f)
        return;
    if(!hasInk){
        bounds = ink;
        hasInk = true;
        return;
    }
    const f32 right = Max(bounds.x + bounds.width, ink.x + ink.width);
    const f32 bottom = Max(bounds.y + bounds.height, ink.y + ink.height);
    bounds.x = Min(bounds.x, ink.x);
    bounds.y = Min(bounds.y, ink.y);
    bounds.width = right - bounds.x;
    bounds.height = bottom - bounds.y;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayout::TextLayout(Core::Alloc::GlobalArena& arena)
    : m_text(arena)
    , m_glyphs(arena)
    , m_clusters(arena)
    , m_lines(arena)
{}

TextHit TextLayout::hitTest(Point point)const{
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
        const f32 leadingDistance = Abs(point.x - cluster.leadingX);
        const f32 trailingDistance = Abs(point.x - cluster.trailingX);
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

bool TextLayout::caretRect(u32 byteOffset, TextCaretEdge::Enum edge, Rect& output)const{
    if(edge != TextCaretEdge::Leading && edge != TextCaretEdge::Trailing)
        return false;
    const TextCluster* alternate = nullptr;
    for(const TextCluster& cluster : m_clusters){
        if(edge == TextCaretEdge::Leading ? cluster.byteBegin == byteOffset : cluster.byteEnd == byteOffset){
            const TextLine& line = m_lines[cluster.lineIndex];
            output = { edge == TextCaretEdge::Leading ? cluster.leadingX : cluster.trailingX, line.top, 0.0f, line.height };
            return true;
        }
        if(cluster.byteBegin == byteOffset || cluster.byteEnd == byteOffset)
            alternate = &cluster;
    }
    if(alternate){
        const TextLine& line = m_lines[alternate->lineIndex];
        const f32 x = alternate->byteBegin == byteOffset ? alternate->leadingX : alternate->trailingX;
        output = { x, line.top, 0.0f, line.height };
        return true;
    }
    for(const TextLine& line : m_lines){
        if(byteOffset == line.byteBegin || byteOffset == line.byteEnd){
            const bool atBegin = byteOffset == line.byteBegin;
            const f32 x = (atBegin == (m_direction == TextDirection::LeftToRight)) ? 0.0f : line.advance;
            output = { x, line.top, 0.0f, line.height };
            return true;
        }
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutBuilder::TextLayoutBuilder(Core::Alloc::GlobalArena& arena, ITextShaper& shaper)
    : m_arena(arena)
    , m_shaper(shaper)
    , m_run(arena)
{}

TextLayoutStatus::Enum TextLayoutBuilder::layout(const ShapeRequest& request, TextLayout& output){
    const TextLayoutStatus::Enum validation = ValidateTextRequest(request, true);
    if(validation != TextLayoutStatus::Success)
        return validation;
    TextLayout result(m_arena);
    if(!request.text.empty())
        result.m_text.assign(request.text.data(), request.text.size());
    result.m_fontSize = request.fontSize;
    result.m_direction = request.direction;
    result.m_glyphs.reserve(request.text.size());
    result.m_clusters.reserve(request.text.size());
    result.m_lines.reserve(1u);
    bool hasInk = false;
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
        const TextLayoutStatus::Enum status = m_shaper.shape(lineRequest, m_run);
        if(status != TextLayoutStatus::Success)
            return status;
        const ShapedRun& run = m_run;
        if(!__hidden_ui_text_layout::ValidateRun(run, lineRequest.text, request.direction))
            return TextLayoutStatus::FontFailure;
        TextLine line;
        line.byteBegin = static_cast<u32>(byteBegin);
        line.byteEnd = static_cast<u32>(byteEnd);
        line.breakEnd = static_cast<u32>(breakEnd);
        line.firstGlyph = static_cast<u32>(result.m_glyphs.size());
        line.glyphCount = static_cast<u32>(run.glyphs.size());
        line.firstCluster = static_cast<u32>(result.m_clusters.size());
        line.top = result.m_measure.y;
        line.baseline = line.top + run.metrics.ascender;
        line.height = run.metrics.ascender + run.metrics.descender + run.metrics.lineGap;
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
                const Point position{ pen + glyph.offset.x, line.baseline + glyph.offset.y };
                result.m_glyphs.push_back({ glyph.face, glyph.glyphId,
                    line.byteBegin + glyph.byteBegin, line.byteBegin + glyph.byteEnd, position });
                __hidden_ui_text_layout::IncludeInk(result.m_inkBounds, hasInk,
                    { position.x + glyph.ink.x, position.y + glyph.ink.y, glyph.ink.width, glyph.ink.height });
                pen += glyph.advance.x;
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
            return TextLayoutStatus::InvalidParameters;
        line.advance = pen;
        line.clusterCount = static_cast<u32>(result.m_clusters.size()) - line.firstCluster;
        result.m_lines.push_back(line);
        result.m_measure.x = Max(result.m_measure.x, pen);
        result.m_measure.y += line.height;
        if(breakEnd == byteEnd)
            break;
        byteBegin = breakEnd;
    }
    output = Move(result);
    return TextLayoutStatus::Success;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

