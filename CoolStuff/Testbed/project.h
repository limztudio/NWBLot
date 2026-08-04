// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include <core/ecs/module.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectTestbed final : public NWB::IProjectEntryCallbacks, public NWB::Core::IInputEventHandler{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createInitialWorldOrDie(NWB::ProjectRuntimeContext& context);

private:
    void drawUiControls();
    void createDefaultScene();
    void registerInputHandler();
    void unregisterInputHandler();
    void clearInputState();
    void setKeyState(i32 key, bool pressed);
    [[nodiscard]] bool keyPressed(i32 key)const;
    void updateMainCamera(f32 delta);
    void destroyWorld();


public:
    explicit ProjectTestbed(NWB::ProjectRuntimeContext& context);
    virtual ~ProjectTestbed()override;

public:
    virtual bool onStartup()override;
    virtual void onShutdown()override;

public:
    virtual bool onUpdate(f32 delta)override;

public:
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    virtual bool mousePosUpdate(f64 xpos, f64 ypos)override;
    virtual bool mouseButtonUpdate(i32 button, i32 action, i32 mods)override;


private:
    static constexpr usize s_KeyStateCount = static_cast<usize>(NWB::Core::Key::Menu) + 1u;

    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    f64 m_lastMouseX = 0.0;
    f64 m_lastMouseY = 0.0;
    f32 m_pendingMouseDeltaX = 0.0f;
    f32 m_pendingMouseDeltaY = 0.0f;
    NWB::Core::ECS::EntityID m_characterEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    Array<bool, s_KeyStateCount> m_keyPressed = {};
    bool m_inputRegistered = false;
    bool m_mouseLookActive = false;
    bool m_mousePositionValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

