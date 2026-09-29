// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_host_fixture.h"

#include <impl/ui/edit/vertical_navigation.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditNavigationTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

struct NavigationCall{
    AString<Alloc::GlobalArena> text;
    Ui::EditNavigationSnapshot navigation;
    Ui::EditNavigationResult result;
    u64 revision = 0u;
    u64 selectionGeneration = 0u;
    usize anchor = 0u;
    usize caret = 0u;
    f32 viewportHeight = 0.0f;
    Ui::EditNavigationDirection::Enum direction = Ui::EditNavigationDirection::Up;

    explicit NavigationCall(Alloc::GlobalArena& arena) : text(arena){}
};

class RecordingNavigationResolver final : public Ui::IEditNavigationResolver{
public:
    RecordingNavigationResolver(Alloc::GlobalArena& arena, Ui::TextLayoutBuilder& layoutBuilder)
        : records(arena)
        , m_arena(arena)
        , m_layoutBuilder(layoutBuilder)
    {}
    virtual ~RecordingNavigationResolver()override = default;


public:
    [[nodiscard]] virtual Ui::EditNavigationResult resolve(const Ui::EditModel& model,
        const Ui::EditNavigationDirection::Enum direction, const Ui::EditNavigationSnapshot& navigation,
        const f32 viewportHeight)override{
        records.emplace_back(m_arena);
        NavigationCall& record = records.back();
        record.text.assign(model.text().data(), model.text().size());
        record.navigation = navigation;
        record.revision = model.revision();
        record.selectionGeneration = model.selectionGeneration();
        record.anchor = model.anchor();
        record.caret = model.caret();
        record.viewportHeight = viewportHeight;
        record.direction = direction;

        Ui::EditNavigationResult result;
        Ui::EditBoxView view(m_arena);
        Ui::TextLayout layout(m_arena);
        if(view.snapshot(model) && m_layoutBuilder.layout({ view.displayText() }, layout) == Ui::TextLayoutStatus::Success
            && view.adoptLayout(Move(layout))){
            Ui::Rect caret;
            if(view.caretGeometry().caretRect(view.displayCaret(), caret)){
                result.preferredX = navigation.valid ? navigation.preferredX : caret.x;
                if(direction == Ui::EditNavigationDirection::Up || direction == Ui::EditNavigationDirection::Down)
                    result.resolved = view.caretGeometry().verticalTarget(view.displayCaret(), direction == Ui::EditNavigationDirection::Down,
                        result.preferredX, result.committedByte);
                else{
                    const f32 distance = direction == Ui::EditNavigationDirection::PageDown ? viewportHeight : -viewportHeight;
                    result.resolved = view.caretGeometry().hitTest({ result.preferredX, caret.y + caret.height * 0.5f + distance }, result.committedByte);
                }
            }
        }
        if(forceResult)
            result = forcedResult;
        record.result = result;
        if(hook){
            Function<void()> callback = Move(hook);
            hook = {};
            callback();
        }
        return result;
    }


public:
    Vector<NavigationCall, Alloc::GlobalArena> records;
    Function<void()> hook;
    Ui::EditNavigationResult forcedResult;
    bool forceResult = false;


private:
    Alloc::GlobalArena& m_arena;
    Ui::TextLayoutBuilder& m_layoutBuilder;
};

struct NavigationAction{
    AString<Alloc::GlobalArena> text;
    Ui::EditAction::Enum action = Ui::EditAction::Abandon;
    bool readOnly = false;

    explicit NavigationAction(Alloc::GlobalArena& arena) : text(arena){}
};

class AcceptingNavigationActions final : public Ui::IEditActionSink{
public:
    explicit AcceptingNavigationActions(Alloc::GlobalArena& arena)
        : records(arena)
        , m_arena(arena)
    {}
    virtual ~AcceptingNavigationActions()override = default;


public:
    [[nodiscard]] virtual bool apply(Ui::EditModel& model, const Ui::EditAction::Enum action, const bool readOnly)override{
        records.emplace_back(m_arena);
        records.back().text.assign(model.text().data(), model.text().size());
        records.back().action = action;
        records.back().readOnly = readOnly;
        if(action == Ui::EditAction::Submit && submitHook){
            Function<void()> callback = Move(submitHook);
            submitHook = {};
            callback();
        }
        return true;
    }

    [[nodiscard]] usize count(const Ui::EditAction::Enum action)const{
        usize result = 0u;
        for(const auto& record : records)
            result += record.action == action ? 1u : 0u;
        return result;
    }


public:
    Vector<NavigationAction, Alloc::GlobalArena> records;
    Function<void()> submitHook;


private:
    Alloc::GlobalArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditNavigationHostTests : public UiEditBoxHostTests{
public:
    UiEditNavigationHostTests()
        : m_navigationModel(m_arena, {}, Ui::EditTextMode::Multiline)
        , m_resolver(m_arena, m_layoutBuilder)
        , m_actions(m_arena)
    {
        m_includeButton = true;
    }


protected:
    [[nodiscard]] bool prepareNavigation(const Ui::EditBoxOptions& options = {},
        const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }, const Ui::Point previousScroll = {}){
        return prepareNavigationModel(m_navigationModel, m_navigation, options, bounds, previousScroll);
    }

    [[nodiscard]] bool prepareNavigationModel(Ui::EditModel& model, Ui::EditNavigationState& navigation,
        const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f },
        const Ui::Point previousScroll = {}){
        ++m_generation;
        if(!m_context.beginFrame(m_generation))
            return false;
        m_host.beginFrame(m_generation, { 500.0f, 200.0f, 1.0f, 1.0f });
        if(!m_context.beginRoot({ 17u, 1u }))
            return false;
        const Ui::WidgetState* widget = m_context.declare("edit", Ui::WidgetKind::EditBox);
        if(!widget)
            return false;
        m_widget = *widget;
        m_result = m_host.editNavigated(m_widget, model, options, {}, navigation, m_resolver, m_actions);
        if(!m_result.valid)
            return false;
        Ui::EditBoxView view(m_arena);
        Ui::TextLayout layout(m_arena);
        if(!view.snapshot(model) || m_layoutBuilder.layout({ view.displayText() }, layout) != Ui::TextLayoutStatus::Success)
            return false;
        if(!view.adoptLayout(Move(layout)) || !view.arrange(bounds, {}, { 0.0f, 0.0f, 500.0f, 200.0f }, previousScroll, m_placement))
            return false;
        if(!m_host.publish(m_widget, view, m_placement, options))
            return false;
        Ui::HitTarget target;
        target.rectangle = bounds;
        target.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        target.enabled = options.enabled;
        target.focusable = options.enabled;
        target.pointerGesture = options.enabled;
        target.textEditable = true;
        if(!m_context.addTarget(m_widget, target))
            return false;
        const Ui::WidgetState* button = m_context.declare("other", Ui::WidgetKind::Button);
        if(!button)
            return false;
        m_otherWidget = *button;
        Ui::HitTarget other;
        other.rectangle = { 230.0f, 20.0f, 100.0f, 30.0f };
        other.clip = { 0.0f, 0.0f, 500.0f, 200.0f };
        other.focusable = true;
        other.activatable = true;
        if(!m_context.addTarget(m_otherWidget, other) || !m_context.endRoot() || !m_context.finishFrame())
            return false;
        m_host.finishFrame();
        return true;
    }

    [[nodiscard]] bool navigationFrame(const Ui::EditBoxOptions& options = {},
        const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }, const Ui::Point previousScroll = {}){
        return prepareNavigation(options, bounds, previousScroll) && commit();
    }

    [[nodiscard]] bool activateNavigation(const Ui::EditBoxOptions& options = {},
        const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }){
        return navigationFrame(options, bounds) && key(Ui::InputKey::Tab) && navigationFrame(options, bounds)
            && m_context.input().focus() == m_widget.id;
    }

    [[nodiscard]] bool commitNative(const AStringView text){
        if(m_textInput.commit(text) != TextInputAdmission::Accepted)
            return false;
        m_host.collectNative();
        return true;
    }

    [[nodiscard]] bool click(const Ui::Point position, const bool shift = false){
        return dispatch({ .type = Ui::InputEventType::PrimaryDown, .position = position, .shift = shift })
            && dispatch({ .type = Ui::InputEventType::PrimaryUp, .position = position, .shift = shift });
    }

    [[nodiscard]] bool focusOther(){
        return key(Ui::InputKey::Tab) && m_context.input().focus() == m_otherWidget.id;
    }

    [[nodiscard]] bool focusNavigation(){
        return key(Ui::InputKey::Tab) && m_context.input().focus() == m_widget.id;
    }


protected:
    Ui::EditModel m_navigationModel;
    Ui::EditNavigationState m_navigation;
    RecordingNavigationResolver m_resolver;
    AcceptingNavigationActions m_actions;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

