// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "label.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Label::Label(Core::Alloc::GlobalArena& arena)
    : m_text(arena)
    , m_language("en", arena)
    , m_layout(arena)
{}

TextLayoutStatus::Enum Label::setText(const ShapeRequest& request){
    if(
        request.text == StringView(m_text.data(), m_text.size()) && request.language == StringView(m_language.data(), m_language.size())
        && request.fontSize == m_fontSize && request.scriptTag == m_scriptTag && request.direction == m_direction
    )
        return TextLayoutStatus::Success;
    const TextLayoutStatus::Enum status = ValidateTextRequest(request, true);
    if(status != TextLayoutStatus::Success)
        return status;
    m_text.assign(request.text.data(), request.text.size());
    m_language.assign(request.language.data(), request.language.size());
    m_fontSize = request.fontSize;
    m_scriptTag = request.scriptTag;
    m_direction = request.direction;
    m_dirty = true;
    return TextLayoutStatus::Success;
}

bool Label::paint(TextService& text, PaintBuilder& paint, const Point topLeft, const Color& color){
    if(m_dirty || m_serviceIdentity != text.identity() || m_fontGeneration != text.generation()){
        const ShapeRequest request{
            StringView(m_text.data(), m_text.size()), m_fontSize, m_direction, m_scriptTag, StringView(m_language.data(), m_language.size())
        };
        if(text.layout(request, m_layout) != TextLayoutStatus::Success)
            return false;
        m_serviceIdentity = text.identity();
        m_fontGeneration = text.generation();
        m_dirty = false;
    }
    return text.paint(paint, m_layout, topLeft, color);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

