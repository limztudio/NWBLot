// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditActionTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

struct ActionRecord{
    Ui::EditAction::Enum action = Ui::EditAction::Submit;
    AString<Alloc::GlobalArena> text;
    bool readOnly = false;

    explicit ActionRecord(Alloc::GlobalArena& arena) : text(arena){}
};

class RecordingActions final : public Ui::IEditActionSink{
public:
    explicit RecordingActions(Alloc::GlobalArena& arena)
        : m_arena(arena)
        , committed(arena)
        , records(arena)
    {
        committed.assign("0");
    }
    virtual ~RecordingActions()override = default;


public:
    [[nodiscard]] virtual bool apply(Ui::EditModel& draft, const Ui::EditAction::Enum action, const bool readOnly)override{
        ActionRecord record(m_arena);
        record.action = action;
        record.text.assign(draft.text().data(), draft.text().size());
        record.readOnly = readOnly;
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

    void clear(){ records.clear(); }


private:
    Alloc::GlobalArena& m_arena;


public:
    AString<Alloc::GlobalArena> committed;
    Ui::PaintVector<ActionRecord> records;
    UiEditBoxHost* resetHost = nullptr;
    bool canonicalizeSubmit = false;
    bool failSubmit = false;
    bool rejectBlur = false;
};

class UiEditActionHostTests : public UiEditBoxHostTests{
public:
    UiEditActionHostTests() : m_actions(m_arena){ m_includeButton = true; }


protected:
    [[nodiscard]] bool actionFrame(const Ui::EditBoxOptions& options = {}){
        return prepare(m_model, options, { 10.0f, 20.0f, 180.0f, 30.0f }, &m_actions) && commit();
    }

    [[nodiscard]] bool activateActions(){
        if(!actionFrame() || !key(Ui::InputKey::Tab) || !actionFrame() || !m_textInput.activeSession().valid())
            return false;
        m_actions.clear();
        return true;
    }

    [[nodiscard]] bool commitNative(const AStringView text){
        return m_textInput.commit(text) == TextInputAdmission::Accepted;
    }

    [[nodiscard]] bool replaceNative(const AStringView text){
        return key(Ui::InputKey::A, false, true) && commitNative(text);
    }

    [[nodiscard]] bool focusOther(){
        return key(Ui::InputKey::Tab) && m_context.input().focus() == m_otherWidget.id;
    }

    [[nodiscard]] bool focusEditor(){
        return key(Ui::InputKey::Tab) && m_context.input().focus() == m_widget.id;
    }

    [[nodiscard]] usize actionCount(const Ui::EditAction::Enum action)const{
        usize count = 0u;
        for(const auto& record : m_actions.records)
            count += record.action == action ? 1u : 0u;
        return count;
    }


protected:
    RecordingActions m_actions;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

