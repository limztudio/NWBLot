// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/text/service.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A passive label caches logical layout; paint confines its ink to the supplied region and inherited clip at the current display scale.
class Label final : NoCopy{
public:
    explicit Label(Core::Alloc::GlobalArena& arena);


public:
    [[nodiscard]] TextLayoutStatus::Enum setText(const ShapeRequest& request);
    [[nodiscard]] Expected<Rect> measure(TextService& text, const DisplayMetrics& display);
    [[nodiscard]] bool paint(TextService& text, PaintBuilder& paint, const Rect& bounds, const Color& color = {});
    [[nodiscard]] const TextLayout& layout()const noexcept{ return m_layout; }


private:
    AString<Core::Alloc::GlobalArena> m_text;
    AString<Core::Alloc::GlobalArena> m_language;
    TextLayout m_layout;
    u64 m_serviceIdentity = 0u;
    u64 m_fontGeneration = 0u;
    f32 m_fontSize = s_TextDefaultFontSize;
    u32 m_scriptTag = s_TextDefaultScriptTag;
    TextDirection::Enum m_direction = TextDirection::LeftToRight;
    bool m_dirty = true;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

