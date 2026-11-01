// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_action_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditActionTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


RecordingActions::RecordingActions(Alloc::GlobalArena& arena)
    : committed(arena)
    , records(arena)
    , m_arena(arena)
{
    committed.assign("0");
}

b8 RecordingActions::apply(Ui::EditModel& draft, const Ui::EditAction::Enum action, const b8 readOnly){
    ActionRecord record(m_arena);
    record.action = action;
    record.text.assign(draft.text().data(), draft.text().size());
    records.push_back(Move(record));
    if(failSubmit && action == Ui::EditAction::Submit)
        return false;
    if(resetHost && action == Ui::EditAction::Submit)
        resetHost->reset();
    if(action == Ui::EditAction::Submit){
        if(readOnly)
            return true;
        committed.assign(draft.text().data(), draft.text().size());
        return !canonicalizeSubmit || draft.setText(committed);
    }
    if(action == Ui::EditAction::Blur && !readOnly && !rejectBlur)
        committed.assign(draft.text().data(), draft.text().size());
    return draft.setText(committed);
}

b8 UiEditActionHostTests::activateActions(){
    if(!actionFrame() || !key(Core::Key::Tab) || !actionFrame() || !m_textInput.activeSession().valid())
        return false;
    m_actions.clear();
    return true;
}

usize UiEditActionHostTests::actionCount(const Ui::EditAction::Enum action)const noexcept{
    usize count = 0u;
    for(const auto& record : m_actions.records)
        count += record.action == action ? 1u : 0u;
    return count;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

