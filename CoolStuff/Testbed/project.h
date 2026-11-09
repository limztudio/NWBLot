// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "namespace.h"
#include "ui_skin_preview.h"

#include <CoolStuff/Testbed/fly_camera_motion.adhd.h>

#include <loader/project_entry.h>

#include <impl/assets_scene/asset.h>

#include <core/ecs/world.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#define TESTBED_WINDOW_TITLE "NWB Testbed"

inline constexpr TStringView s_WindowTitle = NWB_TEXT(TESTBED_WINDOW_TITLE);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Project final : public NWB::IProjectEntryCallbacks, public NWB::Core::IInputEventHandler{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> CreateInitialWorldOrDie(NWB::ProjectRuntimeContext& context);


public:
    explicit Project(NWB::ProjectRuntimeContext& context)
        : m_context(context)
        , m_world(CreateInitialWorldOrDie(context))
        , m_sceneSubsystems(context.objectArena)
        , m_uiPreview(context.objectArena)
    {}
    virtual ~Project()noexcept override;


public:
    virtual b8 onStartup()override;
    virtual void onShutdown()override;


public:
    virtual b8 onUpdate(f32 delta)override;


public:
    virtual void windowFocusUpdate(b8 focused)noexcept override;
    virtual void pointerCaptureLost()noexcept override;
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)noexcept override;
    virtual b8 mousePosUpdate(f64 xpos, f64 ypos)noexcept override;
    virtual b8 mouseButtonUpdate(i32 button, i32 action, i32 mods)noexcept override;


private:
    [[nodiscard]] b8 createDefaultScene();
    void registerInputHandler();
    void unregisterInputHandler()noexcept;
    void updateMainCamera(f32 delta)noexcept;
    void destroyWorld()noexcept;
    void drawUiControls(NWB::Impl::UiPaintContext& context);


private:
    NWB::ProjectRuntimeContext& m_context;
    NotNullUniquePtr<NWB::Core::ECS::World> m_world;
    NWB::Impl::ActiveSceneSubsystems m_sceneSubsystems;
    UiSkinPreview m_uiPreview;
    NWB::Impl::Ui::WindowState m_uiWindow;
    FlyCameraInput m_cameraInput;
    b8 m_inputRegistered = false;
};

Expected<UniquePtr<NWB::Core::ECS::World>, TStringView> CreateInitialProjectWorld(NWB::ProjectRuntimeContext& context);
void DestroyInitialProjectWorld(NWB::ProjectRuntimeContext& context, UniquePtr<NWB::Core::ECS::World>& world)noexcept;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

