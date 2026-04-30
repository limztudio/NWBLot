// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <loader/project_entry.h>

#include <core/ecs/ecs.h>
#include <core/input/input.h>
#include <core/scene/scene.h>
#include <impl/ecs_graphics/ecs_graphics.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ProjectTestbed final : public NWB::IProjectEntryCallbacks, public NWB::Core::IInputEventHandler{
private:
    static NotNullUniquePtr<NWB::Core::ECS::World> createInitialWorldOrDie(NWB::ProjectRuntimeContext& context);
    static void verifyRendererSystemOrDie(NWB::Core::ECS::World& world);

private:
    struct SurfaceEditClickAction{
        enum Enum : u8{
            PreviewHole,
            MoveLatest,
            PatchLatest
        };
    };

    struct SurfaceEditUiAction{
        enum Enum : u32{
            None = 0u,
            RefreshPreview = 1u << 0u,
            CommitPreview = 1u << 1u,
            CancelPreview = 1u << 2u,
            ReplaySavedState = 1u << 3u,
            Undo = 1u << 4u,
            Redo = 1u << 5u,
            HealLatest = 1u << 6u,
            ResizeLatest = 1u << 7u,
            AddLoopCut = 1u << 8u,
            ToggleDebug = 1u << 9u,
            LogDebugSnapshot = 1u << 10u
        };
    };

    struct SurfaceEditRedoStackMode{
        enum Enum : u8{
            Keep = 0u,
            Clear = 1u,
        };
    };

    struct SurfaceEditMutationContext{
        NWB::Core::ECSGraphics::RendererSystem* rendererSystem = nullptr;
        NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh;
        NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance* instance = nullptr;
        NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance cleanBase;
    };

    NWB::Core::ECSGraphics::RendererSystem& rendererSystem();
    void drawUiControls();
    void processPendingUiActions();
    void registerInputHandler();
    void unregisterInputHandler();
    void clearInputState();
    void setKeyState(i32 key, bool pressed);
    [[nodiscard]] bool keyPressed(i32 key)const;
    void updateMainCamera(f32 delta);
    void updateSurfaceEditTarget(f32 delta);
    void updateSurfaceEditAccessories();
    [[nodiscard]] bool selectSurfaceEditTarget(usize targetIndex);
    [[nodiscard]] bool selectSurfaceEditOperator(usize operatorIndex);
    [[nodiscard]] bool selectSurfaceEditCameraView(usize cameraViewIndex);
    void hideSurfaceEditPreviewMesh();
    [[nodiscard]] bool refreshSurfaceEditPreviewMesh();
    void clearSurfaceEditPreview();
    void clearPendingSurfaceEditAccessory();
    bool refreshSurfaceEditPreview();
    void previewSurfaceEditAtCursor();
    void commitSurfaceEditPreview();
    void attachPendingSurfaceEditAccessory();
    void cancelSurfaceEditPreview();
    void queueSurfaceEditReplay();
    void applyPendingSurfaceEditReplay();
    [[nodiscard]] bool buildSurfaceEditPickRay(NWB::Core::ECSGraphics::DeformablePickingRay& outRay);
    void undoSurfaceEdit();
    void redoSurfaceEdit();
    void healLatestSurfaceEdit();
    void resizeLatestSurfaceEdit();
    void moveLatestSurfaceEdit();
    void patchLatestSurfaceEdit();
    void addLoopCutToLatestSurfaceEdit();
    [[nodiscard]] bool buildSurfaceEditCleanBase(
        const NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance& instance,
        NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance& outCleanBase
    )const;
    void hideSurfaceEditAccessoriesForTarget(NWB::Core::ECS::EntityID targetEntity);
    [[nodiscard]] bool restoreSurfaceEditAccessoryEntities();
    [[nodiscard]] bool prepareSurfaceEditMutation(const tchar* action, SurfaceEditMutationContext& outContext);
    void finishSurfaceEditMutation(
        const tchar* action,
        NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh,
        SurfaceEditRedoStackMode::Enum redoStackMode
    );
    [[nodiscard]] bool pickSurfaceEditMutationTarget(
        const tchar* action,
        const SurfaceEditMutationContext& editContext,
        NWB::Core::ECSGraphics::DeformablePosedHit& outTargetHit
    );
    void toggleSurfaceEditDebug();
    void logSurfaceEditDebugSnapshot();
    void logSurfaceEditControls()const;
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
    NWB::Core::ECS::EntityID m_surfaceEditTargetEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECS::EntityID m_surfaceEditPreviewEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
    NWB::Core::ECSGraphics::DeformableSurfaceEditState m_surfaceEditState;
    NWB::Core::ECSGraphics::DeformableSurfaceEditHistory m_surfaceEditHistory;
    NWB::Core::ECSGraphics::DeformableSurfaceEditSession m_surfaceEditSession;
    NWB::Core::ECSGraphics::DeformableHoleEditParams m_surfaceEditPreviewParams;
    NWB::Core::ECSGraphics::DeformableHolePreview m_surfaceEditPreview;
    NWB::Core::ECSGraphics::RuntimeMeshHandle m_pendingSurfaceEditRuntimeMesh;
    NWB::Core::ECSGraphics::RuntimeMeshHandle m_surfaceEditDebugRuntimeMesh;
    NWB::Core::ECSGraphics::DeformableHoleEditResult m_pendingSurfaceEditResult;
    NWB::Core::ECSGraphics::DeformableSurfaceEditRecord m_pendingSurfaceEditRecord;
    usize m_surfaceEditTargetIndex = 0u;
    usize m_surfaceEditOperatorIndex = 0u;
    usize m_surfaceEditCameraViewIndex = 0u;
    f32 m_surfaceEditTargetTime = 0.0f;
    f32 m_surfaceEditDisplacementScale = 1.0f;
    f32 m_surfaceEditRadius = 0.24f;
    f32 m_surfaceEditEllipseRatio = 1.0f;
    f32 m_surfaceEditDepth = 0.18f;
    SurfaceEditClickAction::Enum m_surfaceEditClickAction = SurfaceEditClickAction::PreviewHole;
    u32 m_pendingSurfaceEditUiActions = SurfaceEditUiAction::None;
    usize m_pendingSurfaceEditTargetIndex = 0u;
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
    bool m_pendingSurfaceEditReplay = false;
    bool m_surfaceEditDebugEnabled = false;
    bool m_surfaceEditDisplacementEnabled = true;
    bool m_pendingSurfaceEditTargetSelection = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

