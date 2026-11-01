// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_area.h"
#include <impl/ecs_ui/toolkit/context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shape event-current text during a synchronous host loan; the resolver keeps no model or layout between keys.
class TextAreaNavigationResolver final : public IEditNavigationResolver{
public:
    TextAreaNavigationResolver(Core::Alloc::GlobalArena& arena, TextService& text, const Context& context, const TextAreaState& state, const f32 fontSize, const u32 scriptTag = s_TextDefaultScriptTag, const StringView language = s_TextDefaultLanguage)
        : m_arena(arena)
        , m_text(text)
        , m_context(context)
        , m_state(state)
        , m_instanceGeneration(state.instanceGeneration())
        , m_revision(state.revision())
        , m_fontSize(fontSize)
        , m_scriptTag(scriptTag)
        , m_language(language.data(), language.size(), arena)
    {}
    virtual ~TextAreaNavigationResolver()noexcept override = default;


public:
    [[nodiscard]] virtual Expected<EditNavigationResult> resolve(const EditModel& model, EditNavigationDirection::Enum direction, const EditNavigationSnapshot& preferred, f32 viewportHeight)override;


private:
    [[nodiscard]] b8 current()const noexcept{
        return !m_context.failed() && m_state.instanceGeneration() == m_instanceGeneration && m_state.revision() == m_revision;
    }


private:
    Core::Alloc::GlobalArena& m_arena;
    TextService& m_text;
    const Context& m_context;
    const TextAreaState& m_state;
    const u64 m_instanceGeneration;
    const u64 m_revision;
    const f32 m_fontSize;
    const u32 m_scriptTag;
    AString<Core::Alloc::GlobalArena> m_language;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

