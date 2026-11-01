// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_host_fixture.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditActionTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

struct ActionRecord{
    AString<Alloc::GlobalArena> text;
    Ui::EditAction::Enum action = Ui::EditAction::Submit;


public:
    explicit ActionRecord(Alloc::GlobalArena& arena)noexcept : text(arena){}
};

class RecordingActions final : public Ui::IEditActionSink{
public:
    explicit RecordingActions(Alloc::GlobalArena& arena);
    virtual ~RecordingActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(Ui::EditModel& draft, Ui::EditAction::Enum action, b8 readOnly)override;

    void clear()noexcept{ records.clear(); }


public:
    AString<Alloc::GlobalArena> committed;
    Ui::PaintVector<ActionRecord> records;
    UiEditBoxHost* resetHost = nullptr;
    b8 canonicalizeSubmit = false;
    b8 failSubmit = false;
    b8 rejectBlur = false;


private:
    Alloc::GlobalArena& m_arena;
};

class UiEditActionHostTests : public UiEditBoxHostTests{
public:
    UiEditActionHostTests() : m_actions(m_arena){ m_includeButton = true; }


protected:
    [[nodiscard]] b8 actionFrame(const Ui::EditBoxOptions& options = {}){
        return prepare(m_model, options, { 10.0f, 20.0f, 180.0f, 30.0f }, &m_actions) && commit();
    }

    [[nodiscard]] b8 activateActions();

    [[nodiscard]] b8 commitNative(const AStringView text){
        return m_textInput.commit(text) == TextInputAdmission::Accepted;
    }

    [[nodiscard]] b8 replaceNative(const AStringView text){
        return key(Core::Key::A, false, true) && commitNative(text);
    }

    [[nodiscard]] b8 focusOther(){
        return key(Core::Key::Tab) && m_context.input().focus() == m_otherWidget.id;
    }

    [[nodiscard]] b8 focusEditor(){
        return key(Core::Key::Tab) && m_context.input().focus() == m_widget.id;
    }

    [[nodiscard]] usize actionCount(Ui::EditAction::Enum action)const noexcept;


protected:
    RecordingActions m_actions;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

