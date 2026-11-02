// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_box.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 EditBoxView::snapshot(const EditModel& model){
    const AStringView committed = model.text();
    const EditCompositionView composition = model.composition();
    const usize replaced = composition.active ? composition.replacementEnd - composition.replacementStart : 0u;
    const usize preeditBytes = composition.active ? composition.text.size() : 0u;
    if(committed.size() > s_TextMaxBytes || preeditBytes > s_TextMaxBytes - (committed.size() - replaced))
        return false;
    AString<Core::Alloc::GlobalArena> display(*m_arena);
    display.reserve(committed.size() - replaced + preeditBytes);
    if(composition.active){
        display.append(committed.data(), composition.replacementStart);
        if(preeditBytes != 0u)
            display.append(composition.text.data(), preeditBytes);
        display.append(committed.data() + composition.replacementEnd, committed.size() - composition.replacementEnd);
    }
    else if(!committed.empty())
        display.assign(committed.data(), committed.size());
    const AStringView candidateText(display.data(), display.size());
    const b8 admitted = model.textMode() == EditTextMode::Multiline ? ValidateMultilineText(candidateText) : GraphemeSegmentation::Validate(candidateText, true);
    if(!admitted)
        return false;
    PaintVector<EditCaretMapping> stops(*m_arena);
    stops.reserve(model.graphemeBoundaries().size() + 1u);
    for(const usize boundary : model.graphemeBoundaries()){
        if(!composition.active || boundary <= composition.replacementStart)
            stops.push_back({ boundary, boundary });
        if(composition.active && boundary >= composition.replacementEnd)
            stops.push_back({ boundary, boundary - replaced + preeditBytes });
    }
    m_display = Move(display);
    m_mapping = Move(stops);
    m_composing = composition.active;
    m_preedit = composition.active ? EditBoxRange{ composition.replacementStart, composition.replacementStart + preeditBytes } : EditBoxRange{};
    m_replacement = composition.active ? EditBoxRange{ composition.replacementStart, composition.replacementEnd } : EditBoxRange{};
    m_selection = composition.active ? EditBoxRange{ m_preedit.begin + Min(composition.anchor, composition.caret), m_preedit.begin + Max(composition.anchor, composition.caret) } : EditBoxRange{ model.selectionStart(), model.selectionEnd() };
    m_caret = composition.active ? m_preedit.begin + composition.caret : model.caret();
    m_committedBytes = committed.size();
    m_revision = model.revision();
    m_textMode = model.textMode();
    m_ready = false;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

