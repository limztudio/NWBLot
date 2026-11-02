// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "label.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TextLayoutStatus::Enum Label::setText(const ShapeRequest& request){
    if(request.text == StringView(m_text.data(), m_text.size()) && request.language == StringView(m_language.data(), m_language.size()) && request.fontSize == m_fontSize && request.scriptTag == m_scriptTag && request.direction == m_direction)
        return TextLayoutStatus::Success;
    const TextLayoutStatus::Enum status = ValidateTextRequest(request, true);
    if(status != TextLayoutStatus::Success)
        return status;
    m_dirty = true;
    m_text.assign(request.text.data(), request.text.size());
    m_language.assign(request.language.data(), request.language.size());
    m_fontSize = request.fontSize;
    m_scriptTag = request.scriptTag;
    m_direction = request.direction;
    return TextLayoutStatus::Success;
}

Expected<Rect> Label::measure(TextService& text, const DisplayMetrics& display){
    if(m_dirty || m_serviceIdentity != text.identity() || m_fontGeneration != text.generation()){
        const ShapeRequest request{ StringView(m_text.data(), m_text.size()), StringView(m_language.data(), m_language.size()), m_fontSize, m_scriptTag, m_direction };
        auto layout = text.layout(request);
        if(!layout)
            return MakeUnexpected(Failure{});
        m_layout = Move(*layout);
        m_serviceIdentity = text.identity();
        m_fontGeneration = text.generation();
        m_dirty = false;
    }
    return text.measure(m_layout, display);
}

b8 Label::paint(TextService& text, PaintBuilder& paint, const Rect& bounds, const Color& color){
    const auto measured = measure(text, paint.displayMetrics());
    if(!measured)
        return false;
    paint.pushClip(bounds);
    const b8 painted = text.paint(paint, m_layout, { bounds.x - measured->x, bounds.y - measured->y }, color);
    const b8 restored = paint.popClip();
    return painted && restored;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

