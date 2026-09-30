// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_area.h"
#include "../context.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shape event-current text during a synchronous host loan; the resolver keeps no model or layout between keys.
class TextAreaNavigationResolver final : public IEditNavigationResolver{
public:
    TextAreaNavigationResolver(Core::Alloc::GlobalArena& arena, TextService& text, const Context& context,
        const TextAreaState& state, f32 fontSize);
    virtual ~TextAreaNavigationResolver()override = default;


public:
    [[nodiscard]] virtual EditNavigationResult resolve(const EditModel& model, EditNavigationDirection::Enum direction,
        const EditNavigationSnapshot& preferred, f32 viewportHeight)override;


private:
    [[nodiscard]] bool current()const;


private:
    Core::Alloc::GlobalArena& m_arena;
    TextService& m_text;
    const Context& m_context;
    const TextAreaState& m_state;
    const u64 m_instanceGeneration;
    const u64 m_revision;
    const f32 m_fontSize;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

