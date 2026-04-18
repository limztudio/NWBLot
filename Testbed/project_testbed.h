// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include <core/ecs/ecs.h>
#include <core/input/input.h>
#include <impl/ecs_graphics/ecs_graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectTestbed final : public NWB::IProjectEntryCallbacks, public NWB::Core::IInputEventHandler{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createInitialWorldOrDie(NWB::ProjectRuntimeContext& context);
    static NWB::Core::ECSGraphics::RendererSystem& requireRendererSystemOrDie(NWB::Core::ECS::World& world);

private:
    void registerInputHandler();
    void unregisterInputHandler();
    void clearInputState();
    void setKeyState(i32 key, bool pressed);
    [[nodiscard]] bool keyPressed(i32 key)const;
    void updateMainCamera(f32 delta);
    void updateDeformableMorph(f32 delta);


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
    NWB::Core::ECSGraphics::RendererSystem& m_rendererSystem;
    NWB::Core::ECS::EntityID m_deformableMorphEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    f32 m_deformableMorphTime = 0.0f;
    Array<bool, s_KeyStateCount> m_keyPressed = {};
    f32 m_pendingMouseDeltaX = 0.0f;
    f32 m_pendingMouseDeltaY = 0.0f;
    f64 m_lastMouseX = 0.0;
    f64 m_lastMouseY = 0.0;
    bool m_inputRegistered = false;
    bool m_mouseLookActive = false;
    bool m_mousePositionValid = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

