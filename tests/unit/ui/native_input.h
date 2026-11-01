// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/toolkit/context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiTestSupport{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl::Ui;

[[nodiscard]] const HitTarget* FindHitTarget(Span<const HitTarget> targets, WidgetId widget)noexcept;

// Native cursor adapter helpers for UI unit test fixtures.
[[nodiscard]] InputEvent NativeInput(const InputEvent& event)noexcept;
[[nodiscard]] InputEvent Pointer(InputEventType::Enum type, const Point& position = { 15.0f, 15.0f })noexcept;
[[nodiscard]] PointerInput Pointer(PointerPhase::Enum phase, const InputSource& source, const Point& position = { 20.0f, 20.0f }, b8 hoverCapable = false, PointerIntent::Enum intent = PointerIntent::Activate)noexcept;
[[nodiscard]] InputEvent Key(InputEventType::Enum type, Core::Key::Enum key, b8 repeat = false, b8 shift = false)noexcept;
[[nodiscard]] InputEvent Key(Core::Key::Enum key, b8 shift = false, b8 control = false, b8 alt = false, b8 repeat = false)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class InputTestFixtureBase : public testing::Test{
public:
    explicit InputTestFixtureBase(const Name& arenaName)
        : m_arena(arenaName)
    {}


protected:
    [[nodiscard]] virtual InputRouter& inputRouter()noexcept = 0;
    InputRoutingResult send(const InputEvent& event);
    InputRoutingResult send(const PointerInput& input);
    void click(const Point& position = { 15.0f, 15.0f });
    void key(Core::Key::Enum value, b8 shift = false);


protected:
    Core::Alloc::GlobalArena m_arena;
};

class RouterTestFixture : public InputTestFixtureBase{
public:
    explicit RouterTestFixture(const Name& arenaName)
        : InputTestFixtureBase(arenaName)
        , m_router(m_arena)
    {}


protected:
    [[nodiscard]] virtual InputRouter& inputRouter()noexcept override{ return m_router; }


protected:
    InputRouter m_router;
};

class ContextTestFixture : public InputTestFixtureBase{
public:
    explicit ContextTestFixture(const Name& arenaName)
        : InputTestFixtureBase(arenaName)
        , m_context(m_arena)
    {}


protected:
    [[nodiscard]] virtual InputRouter& inputRouter()noexcept override{ return m_context.input(); }


protected:
    Context m_context;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

