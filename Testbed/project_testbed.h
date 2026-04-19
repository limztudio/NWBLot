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
    static void verifyRendererSystemOrDie(NWB::Core::ECS::World& world);

private:
    void registerInputHandler();
    void unregisterInputHandler();
    void clearInputState();
    void setKeyState(i32 key, bool pressed);
    [[nodiscard]] bool keyPressed(i32 key)const;
    void updateMainCamera(f32 delta);
    void updateDeformableMorph(f32 delta);
    void updateSurfaceEditAccessories();
    void clearSurfaceEditPreview();
    void clearPendingSurfaceEditAccessory();
    bool refreshSurfaceEditPreview();
    void previewSurfaceEditAtCursor();
    void commitSurfaceEditPreview();
    void attachPendingSurfaceEditAccessory();
    void cancelSurfaceEditPreview();
    void logSurfaceEditControls()const;


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
    NWB::Core::ECS::EntityID m_deformableMorphEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECSGraphics::DeformableSurfaceEditState m_surfaceEditState;
    NWB::Core::ECSGraphics::DeformableSurfaceEditSession m_surfaceEditSession;
    NWB::Core::ECSGraphics::DeformableHoleEditParams m_surfaceEditPreviewParams;
    NWB::Core::ECSGraphics::DeformableHolePreview m_surfaceEditPreview;
    NWB::Core::ECSGraphics::RuntimeMeshHandle m_pendingSurfaceEditRuntimeMesh;
    NWB::Core::ECSGraphics::DeformableHoleEditResult m_pendingSurfaceEditResult;
    NWB::Core::ECSGraphics::DeformableSurfaceEditRecord m_pendingSurfaceEditRecord;
    f32 m_deformableMorphTime = 0.0f;
    f32 m_deformableDisplacementScale = 1.0f;
    f32 m_surfaceEditRadius = 0.24f;
    f32 m_surfaceEditEllipseRatio = 1.0f;
    f32 m_surfaceEditDepth = 0.18f;
    Array<bool, s_KeyStateCount> m_keyPressed = {};
    f32 m_pendingMouseDeltaX = 0.0f;
    f32 m_pendingMouseDeltaY = 0.0f;
    f64 m_lastMouseX = 0.0;
    f64 m_lastMouseY = 0.0;
    f64 m_cursorX = 0.0;
    f64 m_cursorY = 0.0;
    bool m_inputRegistered = false;
    bool m_mouseLookActive = false;
    bool m_mousePositionValid = false;
    bool m_cursorPositionValid = false;
    bool m_surfaceEditPreviewActive = false;
    bool m_pendingSurfaceEditAccessory = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

