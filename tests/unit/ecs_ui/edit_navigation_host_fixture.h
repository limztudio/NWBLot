// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box_host_fixture.h"

#include <impl/ecs_ui/toolkit/edit/vertical_navigation.h>

#include <global/simd/vector.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiEditNavigationTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace UiEditBoxTestSupport;

struct NavigationCall{
    AString<Alloc::GlobalArena> text;
    Ui::EditNavigationSnapshot navigation;
    Expected<Ui::EditNavigationResult> result = MakeUnexpected(Failure{});
    u64 selectionGeneration = 0u;
    usize caret = 0u;
    f32 viewportHeight = 0.0f;


public:
    explicit NavigationCall(Alloc::GlobalArena& arena)noexcept : text(arena){}
};

class RecordingNavigationResolver final : public Ui::IEditNavigationResolver{
public:
    RecordingNavigationResolver(Alloc::GlobalArena& arena, Ui::TextLayoutBuilder& layoutBuilder)noexcept
        : records(arena)
        , m_arena(arena)
        , m_layoutBuilder(layoutBuilder)
    {}
    virtual ~RecordingNavigationResolver()noexcept override = default;


public:
    [[nodiscard]] virtual Expected<Ui::EditNavigationResult> resolve(const Ui::EditModel& model, Ui::EditNavigationDirection::Enum direction, const Ui::EditNavigationSnapshot& navigation, f32 viewportHeight)override;


public:
    Vector<NavigationCall, Alloc::GlobalArena> records;
    Function<void()> hook;
    Expected<Ui::EditNavigationResult> forcedResult = MakeUnexpected(Failure{});
    b8 forceResult = false;


private:
    Alloc::GlobalArena& m_arena;
    Ui::TextLayoutBuilder& m_layoutBuilder;
};

struct NavigationAction{
    AString<Alloc::GlobalArena> text;
    Ui::EditAction::Enum action = Ui::EditAction::Abandon;


public:
    explicit NavigationAction(Alloc::GlobalArena& arena)noexcept : text(arena){}
};

class AcceptingNavigationActions final : public Ui::IEditActionSink{
public:
    explicit AcceptingNavigationActions(Alloc::GlobalArena& arena)noexcept
        : records(arena)
        , m_arena(arena)
    {}
    virtual ~AcceptingNavigationActions()noexcept override = default;


public:
    [[nodiscard]] virtual b8 apply(Ui::EditModel& model, Ui::EditAction::Enum action, b8 readOnly)override;
    [[nodiscard]] usize count(Ui::EditAction::Enum action)const noexcept;


public:
    Vector<NavigationAction, Alloc::GlobalArena> records;


private:
    Alloc::GlobalArena& m_arena;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiEditNavigationHostTests : public UiEditBoxHostTests{
public:
    UiEditNavigationHostTests();


protected:
    [[nodiscard]] b8 prepareNavigation(const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }, const Ui::Point previousScroll = {}){
        return prepareNavigationModel(m_navigationModel, m_navigation, options, bounds, previousScroll);
    }

    [[nodiscard]] b8 prepareNavigationModel(Ui::EditModel& model, Ui::EditNavigationState& navigation, const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }, Ui::Point previousScroll = {});

    [[nodiscard]] b8 navigationFrame(const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }, const Ui::Point previousScroll = {}){
        return prepareNavigation(options, bounds, previousScroll) && commit();
    }

    [[nodiscard]] b8 activateNavigation(const Ui::EditBoxOptions& options = {}, const Ui::Rect& bounds = { 10.0f, 20.0f, 180.0f, 48.0f }){
        return navigationFrame(options, bounds) && key(Core::Key::Tab) && navigationFrame(options, bounds) && m_context.input().focus() == m_widget.id;
    }

    [[nodiscard]] b8 commitNative(AStringView text);

    [[nodiscard]] b8 click(const Ui::Point position, const b8 shift = false){
        return dispatch({ .position = position, .type = Ui::InputEventType::PrimaryDown, .shift = shift }) && dispatch({ .position = position, .type = Ui::InputEventType::PrimaryUp, .shift = shift });
    }

    [[nodiscard]] b8 focusOther(){
        return key(Core::Key::Tab) && m_context.input().focus() == m_otherWidget.id;
    }

    [[nodiscard]] b8 focusNavigation(){
        return key(Core::Key::Tab) && m_context.input().focus() == m_widget.id;
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

