// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layout.h"

#include <impl/ecs_ui/toolkit/layout/validation.adhd.h>
#include <impl/ecs_ui/toolkit/text/layout_measurement.adhd.h>

#include <global/simd/result.h>
#include <global/simd/vector_arithmetic.h>
#include <global/simd/convert.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


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
        usize breakEnd = byteBegin;
        ResolveNextTextLineRange(request.text, byteBegin, byteEnd, breakEnd);
        ShapeRequest lineRequest = request;
        lineRequest.text = request.text.substr(byteBegin, byteEnd - byteBegin);
        const auto shapedRun = m_shaper.shape(lineRequest);
        if(!shapedRun)
            return MakeUnexpected(shapedRun.error());
        const ShapedRun& run = *shapedRun;
        const TextLayoutStatus::Enum lineStatus = PlaceShapedLineInto<SharedFontFace>(
            byteBegin,
            byteEnd,
            breakEnd,
            run.metrics,
            run.glyphs,
            lineRequest.text,
            request.direction,
            result.m_glyphs,
            result.m_clusters,
            result.m_lines,
            result.m_measure,
            inkBounds,
            hasInk
        );
        if(lineStatus != TextLayoutStatus::Success)
            return MakeUnexpected(lineStatus);
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

