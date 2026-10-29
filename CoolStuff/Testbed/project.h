// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_skin_preview.h"

#include <loader/project_entry.h>

#include <impl/assets_scene/asset.h>

#include <core/ecs/world.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Project final : public NWB::IProjectEntryCallbacks, public NWB::Core::IInputEventHandler{
private:
    static constexpr usize s_KeyStateCount = static_cast<usize>(NWB::Core::Key::Menu) + 1u;
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateInitialWorldOrDie(NWB::ProjectRuntimeContext& context);


public:
    explicit Project(NWB::ProjectRuntimeContext& context);
    virtual ~Project()noexcept override;


public:
    virtual b8 onStartup()override;
    virtual void onShutdown()override;

public:
    virtual b8 onUpdate(f32 delta)override;

public:
    virtual void windowFocusUpdate(b8 focused)noexcept override;
    virtual void pointerCaptureLost()noexcept override;
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;
    virtual b8 mousePosUpdate(f64 xpos, f64 ypos)noexcept override;
    virtual b8 mouseButtonUpdate(i32 button, i32 action, i32 mods)noexcept override;


private:
    void drawUiControls(NWB::Impl::UiPaintContext& context);
    void drawCustomUiControls(NWB::Impl::UiPaintContext& context);
    [[nodiscard]] b8 createDefaultScene();
    void registerInputHandler();
    void unregisterInputHandler();
    void clearInputState()noexcept;
    void setKeyState(i32 key, b8 pressed)noexcept;
    [[nodiscard]] b8 keyPressed(i32 key)const noexcept;
    void updateMainCamera(f32 delta)noexcept;
    void destroyWorld();


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::ActiveSceneSubsystems m_sceneSubsystems;
    UiSkinPreview m_uiPreview;
    NWB::Impl::Ui::WindowState m_uiWindow;
    f64 m_lastMouseX = 0.0;
    f64 m_lastMouseY = 0.0;
    f32 m_pendingMouseDeltaX = 0.0f;
    f32 m_pendingMouseDeltaY = 0.0f;
    b8 m_inputRegistered = false;
    b8 m_mouseLookActive = false;
    b8 m_mousePositionValid = false;
    Array<b8, s_KeyStateCount> m_keyPressed = {};
};

Expected<UniquePtr<NWB::Core::ECS::World>, TStringView> CreateInitialProjectWorld(NWB::ProjectRuntimeContext& context);
void DestroyInitialProjectWorld(NWB::ProjectRuntimeContext& context, UniquePtr<NWB::Core::ECS::World>& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

