// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_caret_geometry.h"

#include <impl/ecs_ui/toolkit/edit/multiline_text.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_caret_geometry{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 ValidateLayout(const TextLayout& layout, const EditTextMode::Enum mode)noexcept{
    const StringView text = layout.utf8();
    const auto& lines = layout.lines();
    const auto& clusters = layout.clusters();
    const auto& glyphs = layout.glyphs();
    const Point measure = layout.measure();
    const Rect ink = layout.inkBounds();
    if(
        lines.empty() || lines.size() > s_TextMaxBytes + 1u || clusters.size() > s_TextMaxBytes
        || glyphs.size() > s_TextMaxBytes || (mode == EditTextMode::SingleLine && lines.size() != 1u)
        || !IsFinite(measure.x) || !IsFinite(measure.y) || measure.x < 0.0f || measure.y <= 0.0f
        || !IsFinite(ink.x) || !IsFinite(ink.y) || !IsFinite(ink.width) || !IsFinite(ink.height)
        || ink.width < 0.0f || ink.height < 0.0f
    )
        return false;
    usize previousBreak = 0u;
    usize nextCluster = 0u;
    usize nextGlyph = 0u;
    f32 previousBottom = 0.0f;
    f32 maximumAdvance = 0.0f;
    for(usize lineIndex = 0u; lineIndex < lines.size(); ++lineIndex){
        const TextLine& line = lines[lineIndex];
        if(
            line.byteBegin != previousBreak || line.byteEnd < line.byteBegin || line.breakEnd < line.byteEnd
            || line.breakEnd > text.size() || line.firstCluster != nextCluster || line.firstGlyph != nextGlyph
            || line.clusterCount > clusters.size() - nextCluster || line.glyphCount > glyphs.size() - nextGlyph
            || !IsFinite(line.top) || !IsFinite(line.height) || !IsFinite(line.advance) || !IsFinite(line.baseline)
            || line.top != previousBottom || line.height <= 0.0f || line.advance < 0.0f
        )
            return false;
        const f32 bottom = line.top + line.height;
        if(!IsFinite(bottom) || line.baseline < line.top || line.baseline > bottom)
            return false;
        for(usize byte = line.byteBegin; byte < line.byteEnd; ++byte){
            if(text[byte] == '\n')
                return false;
        }
        const b8 lastLine = lineIndex + 1u == lines.size();
        if(lastLine){
            if(line.byteEnd != text.size() || line.breakEnd != line.byteEnd)
                return false;
        }
        else if(line.byteEnd == text.size() || text[line.byteEnd] != '\n' || line.breakEnd != line.byteEnd + 1u)
            return false;
        usize coveredByte = line.byteBegin;
        const usize glyphEnd = nextGlyph + line.glyphCount;
        const usize clusterEnd = nextCluster + line.clusterCount;
        f32 previousX = 0.0f;
        for(; nextCluster < clusterEnd; ++nextCluster){
            const TextCluster& cluster = clusters[nextCluster];
            if(
                cluster.lineIndex != lineIndex || cluster.byteBegin != coveredByte
                || cluster.byteEnd <= cluster.byteBegin || cluster.byteEnd > line.byteEnd
                || !IsUtf8ScalarBoundary(text, cluster.byteBegin)
                || !IsUtf8ScalarBoundary(text, cluster.byteEnd)
                || cluster.firstGlyph != nextGlyph || cluster.glyphCount == 0u || cluster.glyphCount > glyphEnd - nextGlyph
                || !IsFinite(cluster.leadingX) || !IsFinite(cluster.trailingX)
                || cluster.leadingX != previousX || cluster.trailingX < cluster.leadingX || cluster.trailingX > line.advance
            )
                return false;
            const usize clusterGlyphEnd = nextGlyph + cluster.glyphCount;
            for(; nextGlyph < clusterGlyphEnd; ++nextGlyph){
                const PlacedGlyph& glyph = glyphs[nextGlyph];
                if(glyph.byteBegin != cluster.byteBegin || glyph.byteEnd != cluster.byteEnd || !IsFinite(glyph.position.x) || !IsFinite(glyph.position.y))
                    return false;
            }
            coveredByte = cluster.byteEnd;
            previousX = cluster.trailingX;
        }
        if(coveredByte != line.byteEnd || nextGlyph != glyphEnd || previousX != line.advance)
            return false;
        previousBreak = line.breakEnd;
        previousBottom = bottom;
        maximumAdvance = Max(maximumAdvance, line.advance);
    }
    return nextCluster == clusters.size() && nextGlyph == glyphs.size() && measure.x == maximumAdvance && measure.y == previousBottom;
}

[[nodiscard]] static u32 LineForByte(const TextLayout& layout, const usize byte)noexcept{
    const auto next = LowerBound(layout.lines().begin(), layout.lines().end(), byte, [](const TextLine& line, usize value)noexcept{ return line.byteBegin <= value; });
    return static_cast<u32>(next - layout.lines().begin() - 1);
}

[[nodiscard]] static Expected<Rect> ResolveCaretRect(const TextLayout& layout, const Span<const usize> boundaries, const usize byte)noexcept{
    if(layout.lines().empty() || !IsUtf8ScalarBoundary(layout.utf8(), byte))
        return MakeUnexpected(Failure{});
    const TextLine& line = layout.lines()[LineForByte(layout, byte)];
    if(byte < line.byteBegin || byte > line.byteEnd)
        return MakeUnexpected(Failure{});
    f32 x = 0.0f;
    if(line.clusterCount != 0u){
        const auto firstCluster = layout.clusters().begin() + line.firstCluster;
        const auto lastCluster = firstCluster + line.clusterCount;
        const auto cluster = LowerBound(firstCluster, lastCluster, byte, [](const TextCluster& item, usize value)noexcept{ return item.byteEnd < value; });
        if(cluster == lastCluster || byte < cluster->byteBegin)
            return MakeUnexpected(Failure{});
        if(byte == cluster->byteBegin || byte == cluster->byteEnd)
            x = byte == cluster->byteBegin ? cluster->leadingX : cluster->trailingX;
        else{
            auto first = LowerBound(boundaries.begin(), boundaries.end(), static_cast<usize>(cluster->byteBegin));
            if(first != boundaries.end() && *first == cluster->byteBegin)
                ++first;
            const auto last = LowerBound(first, boundaries.end(), static_cast<usize>(cluster->byteEnd));
            const auto next = LowerBound(first, last, byte);
            const usize intervals = static_cast<usize>(last - first) + 1u;
            f32 ordinal = static_cast<f32>(next - first);
            if(next != last && *next == byte)
                ordinal += 1.0f;
            else{
                const usize begin = next == first ? cluster->byteBegin : *(next - 1);
                const usize end = next == last ? cluster->byteEnd : *next;
                ordinal += ComputeEditScalarFraction(layout.utf8(), begin, end, byte);
            }
            const f32 span = cluster->trailingX - cluster->leadingX;
            x = cluster->leadingX + ((span * ordinal) / static_cast<f32>(intervals));
        }
    }
    if(!IsFinite(x))
        return MakeUnexpected(Failure{});
    return Rect{ x, line.top, 0.0f, line.height };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EditCaretGeometry::adoptLayout(TextLayout&& layout, const StringView expectedText, const Span<const EditCaretMapping> mapping, const usize committedBytes, const EditTextMode::Enum mode){
    if(
        layout.utf8() != expectedText || (mode != EditTextMode::SingleLine && mode != EditTextMode::Multiline)
        || expectedText.size() > s_TextMaxBytes || committedBytes > s_TextMaxBytes
        || mapping.empty() || mapping.size() > committedBytes + s_EditCaretMaxExtraMappingStops
        || mapping.front().committedByte != 0u || mapping.front().displayByte != 0u
        || mapping.back().committedByte != committedBytes || mapping.back().displayByte != expectedText.size()
    )
        return false;
    if(mode == EditTextMode::Multiline && !ValidateMultilineText(expectedText))
        return false;
    const ShapeRequest request{ .text = expectedText, .fontSize = layout.fontSize() };
    if(ValidateTextRequest(request, mode == EditTextMode::Multiline) != TextLayoutStatus::Success || !__hidden_ui_edit_caret_geometry::ValidateLayout(layout, mode))
        return false;
    auto boundaries = GraphemeSegmentation::Build(*m_arena, expectedText, mode == EditTextMode::SingleLine);
    if(!boundaries)
        return false;
    PaintVector<EditBoxCaretStop> stops(*m_arena);
    PaintVector<EditCaretLine> lines(*m_arena);
    stops.reserve(mapping.size());
    lines.reserve(layout.lines().size());
    for(const TextLine& line : layout.lines())
        lines.push_back({ line.byteBegin, line.byteEnd, line.breakEnd, 0u, 0u, line.top, line.height, line.advance });
    usize previousCommitted = 0u;
    usize previousDisplay = 0u;
    for(const EditCaretMapping& item : mapping){
        if(item.committedByte < previousCommitted || item.committedByte > committedBytes || item.displayByte < previousDisplay || item.displayByte > expectedText.size())
            return false;
        const auto caret = __hidden_ui_edit_caret_geometry::ResolveCaretRect(layout, *boundaries, item.displayByte);
        if(!caret)
            return false;
        const u32 lineIndex = __hidden_ui_edit_caret_geometry::LineForByte(layout, item.displayByte);
        stops.push_back({ item.committedByte, item.displayByte, caret->x, lineIndex });
        ++lines[lineIndex].stopCount;
        previousCommitted = item.committedByte;
        previousDisplay = item.displayByte;
    }
    u32 firstStop = 0u;
    for(EditCaretLine& line : lines){
        line.firstStop = firstStop;
        firstStop += line.stopCount;
    }
    m_layout = Move(layout);
    m_boundaries = Move(*boundaries);
    m_stops = Move(stops);
    m_lines = Move(lines);
    m_mode = mode;
    m_ready = true;
    return true;
}

Expected<Rect> EditCaretGeometry::caretRect(const usize displayByte)const noexcept{
    if(!m_ready)
        return MakeUnexpected(Failure{});
    return __hidden_ui_edit_caret_geometry::ResolveCaretRect(m_layout, m_boundaries, displayByte);
}

Expected<usize> EditCaretGeometry::hitTest(const Point localPoint)const noexcept{
    if(!m_ready)
        return MakeUnexpected(Failure{});
    return HitEditCaretGeometry(m_lines, m_stops, localPoint);
}

Expected<usize> EditCaretGeometry::verticalTarget(const usize displayCaret, const b8 down, const f32 preferredX)const noexcept{
    if(!m_ready || !IsFinite(preferredX))
        return MakeUnexpected(Failure{});
    const auto caret = __hidden_ui_edit_caret_geometry::ResolveCaretRect(m_layout, m_boundaries, displayCaret);
    if(!caret)
        return MakeUnexpected(caret.error());
    const u32 current = __hidden_ui_edit_caret_geometry::LineForByte(m_layout, displayCaret);
    const u32 target = down ? Min(current + 1u, static_cast<u32>(m_lines.size() - 1u)) : current == 0u ? 0u : current - 1u;
    usize committedByte = 0u;
    if(target == current){
        for(const EditBoxCaretStop& stop : m_stops){
            if(stop.displayByte == displayCaret){
                return stop.committedByte;
            }
        }
        const f32 center = caret->y + (caret->height * s_RectCenterFraction);
        if(!TryFindNearestEditCaretStop(m_lines, m_stops, current, { caret->x, center }, committedByte))
            return MakeUnexpected(Failure{});
        return committedByte;
    }
    const EditCaretLine& line = m_lines[target];
    const f32 center = line.top + (line.height * s_RectCenterFraction);
    if(!TryFindNearestEditCaretStop(m_lines, m_stops, target, { preferredX, center }, committedByte))
        return MakeUnexpected(Failure{});
    return committedByte;
}

Expected<Rect> EditCaretGeometry::rangeOnLine(const EditBoxRange range, const u32 lineIndex, const f32 breakWidth)const noexcept{
    if(
        !m_ready || lineIndex >= m_lines.size() || range.begin > range.end || range.end > m_layout.utf8().size()
        || !IsFinite(breakWidth) || breakWidth < 0.0f
        || !IsUtf8ScalarBoundary(m_layout.utf8(), range.begin)
        || !IsUtf8ScalarBoundary(m_layout.utf8(), range.end)
    )
        return MakeUnexpected(Failure{});
    if(range.begin == range.end){
        return Rect{};
    }
    const EditCaretLine& line = m_lines[lineIndex];
    const usize begin = Max(range.begin, line.byteBegin);
    const usize end = Min(range.end, line.byteEnd);
    const b8 selectedBreak = line.breakEnd != line.byteEnd && range.begin <= line.byteEnd && range.end > line.byteEnd;
    if(begin >= end && !selectedBreak){
        return Rect{};
    }
    const auto first = __hidden_ui_edit_caret_geometry::ResolveCaretRect(m_layout, m_boundaries, begin);
    if(!first)
        return MakeUnexpected(first.error());
    const auto last = __hidden_ui_edit_caret_geometry::ResolveCaretRect(m_layout, m_boundaries, end);
    if(!last)
        return MakeUnexpected(last.error());
    const f32 firstX = first->x;
    const f32 width = Max(0.0f, last->x - firstX) + (selectedBreak ? breakWidth : 0.0f);
    if(!IsFinite(width) || !IsFinite(firstX + width))
        return MakeUnexpected(Failure{});
    return Rect{ firstX, line.top, width, line.height };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

