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


[[nodiscard]] static bool ValidateLayout(const TextLayout& layout, const EditTextMode::Enum mode){
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
            || !IsFinite(line.top + line.height) || line.baseline < line.top || line.baseline > line.top + line.height
        )
            return false;
        for(usize byte = line.byteBegin; byte < line.byteEnd; ++byte){
            if(text[byte] == '\n')
                return false;
        }
        const bool lastLine = lineIndex + 1u == lines.size();
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
                || !GraphemeSegmentation::isScalarBoundary(text, cluster.byteBegin)
                || !GraphemeSegmentation::isScalarBoundary(text, cluster.byteEnd)
                || cluster.firstGlyph != nextGlyph || cluster.glyphCount == 0u || cluster.glyphCount > glyphEnd - nextGlyph
                || !IsFinite(cluster.leadingX) || !IsFinite(cluster.trailingX)
                || cluster.leadingX != previousX || cluster.trailingX < cluster.leadingX || cluster.trailingX > line.advance
            )
                return false;
            const usize clusterGlyphEnd = nextGlyph + cluster.glyphCount;
            for(; nextGlyph < clusterGlyphEnd; ++nextGlyph){
                const PlacedGlyph& glyph = glyphs[nextGlyph];
                if(
                    glyph.byteBegin != cluster.byteBegin || glyph.byteEnd != cluster.byteEnd
                    || !IsFinite(glyph.position.x) || !IsFinite(glyph.position.y)
                )
                    return false;
            }
            coveredByte = cluster.byteEnd;
            previousX = cluster.trailingX;
        }
        if(coveredByte != line.byteEnd || nextGlyph != glyphEnd || previousX != line.advance)
            return false;
        previousBreak = line.breakEnd;
        previousBottom = line.top + line.height;
        maximumAdvance = Max(maximumAdvance, line.advance);
    }
    return
        nextCluster == clusters.size() && nextGlyph == glyphs.size()
        && measure.x == maximumAdvance && measure.y == previousBottom
    ;
}

[[nodiscard]] static u32 LineForByte(const TextLayout& layout, const usize byte){
    const auto next = LowerBound(
        layout.lines().begin(), layout.lines().end(), byte,
        [](const TextLine& line, usize value){ return line.byteBegin <= value; }
    );
    return static_cast<u32>(next - layout.lines().begin() - 1);
}

[[nodiscard]] static f32 ScalarFraction(const StringView text, const usize begin, const usize end, const usize position){
    usize total = 0u;
    usize before = 0u;
    for(usize index = begin; index < end; ++index){
        if(!IsUtf8Continuation(static_cast<u8>(text[index]))){
            ++total;
            if(index < position)
                ++before;
        }
    }
    return total == 0u ? 0.0f : static_cast<f32>(before) / static_cast<f32>(total);
}

[[nodiscard]] static bool CaretRect(const TextLayout& layout, const EditBoundaryVector& boundaries,
    const usize byte, Rect& output){
    if(layout.lines().empty() || !GraphemeSegmentation::isScalarBoundary(layout.utf8(), byte))
        return false;
    const TextLine& line = layout.lines()[LineForByte(layout, byte)];
    if(byte < line.byteBegin || byte > line.byteEnd)
        return false;
    f32 x = 0.0f;
    if(line.clusterCount != 0u){
        const auto firstCluster = layout.clusters().begin() + line.firstCluster;
        const auto lastCluster = firstCluster + line.clusterCount;
        const auto cluster = LowerBound(
            firstCluster, lastCluster, byte,
            [](const TextCluster& item, usize value){ return item.byteEnd < value; }
        );
        if(cluster == lastCluster || byte < cluster->byteBegin)
            return false;
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
                ordinal += ScalarFraction(layout.utf8(), begin, end, byte);
            }
            x = cluster->leadingX + (cluster->trailingX - cluster->leadingX) * ordinal / static_cast<f32>(intervals);
        }
    }
    if(!IsFinite(x))
        return false;
    output = { x, line.top, 0.0f, line.height };
    return true;
}

[[nodiscard]] static bool NearestStop(const PaintVector<EditCaretLine>& lines, const PaintVector<EditBoxCaretStop>& stops,
    const u32 lineIndex, const Point point, usize& committedByte){
    if(stops.empty() || lineIndex >= lines.size() || !IsFinite(point.x) || !IsFinite(point.y))
        return false;
    const EditCaretLine& line = lines[lineIndex];
    if(line.firstStop > stops.size() || line.stopCount > stops.size() - line.firstStop)
        return false;
    if(line.stopCount == 0u && (line.firstStop == 0u || line.firstStop >= stops.size()))
        return false;
    const usize first = line.stopCount == 0u ? line.firstStop - 1u : line.firstStop;
    const usize end = line.stopCount == 0u ? static_cast<usize>(line.firstStop) + 1u : first + line.stopCount;
    f32 minimumX = stops[first].x;
    if(!IsFinite(minimumX) || minimumX < 0.0f)
        return false;
    f32 maximumX = minimumX;
    for(usize index = first + 1u; index < end; ++index){
        if(!IsFinite(stops[index].x) || stops[index].x < 0.0f)
            return false;
        minimumX = Min(minimumX, stops[index].x);
        maximumX = Max(maximumX, stops[index].x);
    }
    const f32 queryX = Clamp(point.x, minimumX, maximumX);
    f64 nearestY = Limit<f64>::s_Max;
    f32 nearestX = Limit<f32>::s_Max;
    usize result = 0u;
    for(usize index = first; index < end; ++index){
        const EditBoxCaretStop& stop = stops[index];
        if(stop.lineIndex >= lines.size() || (line.stopCount != 0u && stop.lineIndex != lineIndex))
            return false;
        const EditCaretLine& stopLine = lines[stop.lineIndex];
        if(
            !IsFinite(stopLine.top) || !IsFinite(stopLine.height) || stopLine.height <= 0.0f
            || !IsFinite(stopLine.top + stopLine.height)
        )
            return false;
        const f64 distanceY = line.stopCount == 0u
            ? Abs(static_cast<f64>(point.y) - (static_cast<f64>(stopLine.top) + static_cast<f64>(stopLine.height) * 0.5)) : 0.0;
        const f32 distanceX = Abs(queryX - stop.x);
        if(distanceY < nearestY || (distanceY == nearestY && distanceX < nearestX)){
            nearestY = distanceY;
            nearestX = distanceX;
            result = stop.committedByte;
        }
    }
    committedByte = result;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HitEditCaretGeometry(const PaintVector<EditCaretLine>& lines, const PaintVector<EditBoxCaretStop>& stops,
    const Point localPoint, usize& committedByte){
    if(lines.empty() || lines.size() > Limit<u32>::s_Max || stops.empty() || !IsFinite(localPoint.x) || !IsFinite(localPoint.y))
        return false;
    u32 lineIndex = static_cast<u32>(lines.size() - 1u);
    for(u32 index = 0u; index < lines.size(); ++index){
        const EditCaretLine& line = lines[index];
        if(!IsFinite(line.top) || !IsFinite(line.height) || line.height <= 0.0f || !IsFinite(line.top + line.height))
            return false;
        if(localPoint.y < line.top + line.height){
            lineIndex = index;
            break;
        }
    }
    return __hidden_ui_edit_caret_geometry::NearestStop(lines, stops, lineIndex, localPoint, committedByte);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


EditCaretGeometry::EditCaretGeometry(Core::Alloc::GlobalArena& arena)
    : m_arena(MakeNotNull(&arena))
    , m_layout(arena)
    , m_boundaries(arena)
    , m_stops(arena)
    , m_lines(arena)
{}

bool EditCaretGeometry::adoptLayout(TextLayout&& layout, const StringView expectedText,
    const PaintVector<EditCaretMapping>& mapping, const usize committedBytes, const EditTextMode::Enum mode){
    if(
        layout.utf8() != expectedText || (mode != EditTextMode::SingleLine && mode != EditTextMode::Multiline)
        || expectedText.size() > s_TextMaxBytes || committedBytes > s_TextMaxBytes
        || mapping.empty() || mapping.size() > committedBytes + 2u
        || mapping.front().committedByte != 0u || mapping.front().displayByte != 0u
        || mapping.back().committedByte != committedBytes || mapping.back().displayByte != expectedText.size()
    )
        return false;
    if(mode == EditTextMode::Multiline && !ValidateMultilineText(expectedText))
        return false;
    const ShapeRequest request{ expectedText, layout.fontSize() };
    if(
        ValidateTextRequest(request, mode == EditTextMode::Multiline) != TextLayoutStatus::Success
        || !__hidden_ui_edit_caret_geometry::ValidateLayout(layout, mode)
    )
        return false;
    EditBoundaryVector boundaries(*m_arena);
    if(!GraphemeSegmentation::build(expectedText, boundaries, mode == EditTextMode::SingleLine))
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
        if(
            item.committedByte < previousCommitted || item.committedByte > committedBytes
            || item.displayByte < previousDisplay || item.displayByte > expectedText.size()
        )
            return false;
        Rect caret;
        if(!__hidden_ui_edit_caret_geometry::CaretRect(layout, boundaries, item.displayByte, caret))
            return false;
        const u32 lineIndex = __hidden_ui_edit_caret_geometry::LineForByte(layout, item.displayByte);
        stops.push_back({ item.committedByte, item.displayByte, caret.x, lineIndex });
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
    m_boundaries = Move(boundaries);
    m_stops = Move(stops);
    m_lines = Move(lines);
    m_mode = mode;
    m_ready = true;
    return true;
}

bool EditCaretGeometry::caretRect(const usize displayByte, Rect& output)const{
    return m_ready && __hidden_ui_edit_caret_geometry::CaretRect(m_layout, m_boundaries, displayByte, output);
}

bool EditCaretGeometry::hitTest(const Point localPoint, usize& committedByte)const{
    return m_ready && HitEditCaretGeometry(m_lines, m_stops, localPoint, committedByte);
}

bool EditCaretGeometry::verticalTarget(const usize displayCaret, const bool down,
    const f32 preferredX, usize& committedByte)const{
    Rect caret;
    if(!m_ready || !IsFinite(preferredX) || !caretRect(displayCaret, caret))
        return false;
    const u32 current = __hidden_ui_edit_caret_geometry::LineForByte(m_layout, displayCaret);
    const u32 target = down ? Min(current + 1u, static_cast<u32>(m_lines.size() - 1u)) : current == 0u ? 0u : current - 1u;
    if(target == current){
        for(const EditBoxCaretStop& stop : m_stops){
            if(stop.displayByte == displayCaret){
                committedByte = stop.committedByte;
                return true;
            }
        }
        return nearestStop(current, { caret.x, caret.y + caret.height * 0.5f }, committedByte);
    }
    const EditCaretLine& line = m_lines[target];
    return nearestStop(target, { preferredX, line.top + line.height * 0.5f }, committedByte);
}

bool EditCaretGeometry::rangeOnLine(const EditBoxRange range, const u32 lineIndex,
    const f32 breakWidth, Rect& output)const{
    if(
        !m_ready || lineIndex >= m_lines.size() || range.begin > range.end || range.end > m_layout.utf8().size()
        || !IsFinite(breakWidth) || breakWidth < 0.0f
        || !GraphemeSegmentation::isScalarBoundary(m_layout.utf8(), range.begin)
        || !GraphemeSegmentation::isScalarBoundary(m_layout.utf8(), range.end)
    )
        return false;
    if(range.begin == range.end){
        output = {};
        return true;
    }
    const EditCaretLine& line = m_lines[lineIndex];
    const usize begin = Max(range.begin, line.byteBegin);
    const usize end = Min(range.end, line.byteEnd);
    const bool selectedBreak = line.breakEnd != line.byteEnd && range.begin <= line.byteEnd && range.end > line.byteEnd;
    if(begin >= end && !selectedBreak){
        output = {};
        return true;
    }
    Rect first;
    Rect last;
    if(!caretRect(begin, first) || !caretRect(end, last))
        return false;
    const f32 width = Max(0.0f, last.x - first.x) + (selectedBreak ? breakWidth : 0.0f);
    if(!IsFinite(width) || !IsFinite(first.x + width))
        return false;
    output = { first.x, line.top, width, line.height };
    return true;
}

bool EditCaretGeometry::nearestStop(const u32 lineIndex, const Point point, usize& committedByte)const{
    return __hidden_ui_edit_caret_geometry::NearestStop(m_lines, m_stops, lineIndex, point, committedByte);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

