// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "CoolStuff/Testbed/scene_constants.adhd.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/skinning/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/layer_system.h>

#include <core/common/log.h>

#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_testbed_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_DefaultSceneDescription = NWB_TEXT("45-degree directional + point light, female skinned character on a white ground plane");
static constexpr TStringView s_InitWorldFailedText = NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false");
static constexpr TStringView s_SceneEntitiesInvalidText = NWB_TEXT("ProjectTestbed initialization failed: failed to instantiate scene entities");
static constexpr TStringView s_StartupSceneText = NWB_TEXT("ProjectTestbed: startup scene created ({})");
static constexpr TStringView s_ShutdownText = NWB_TEXT("ProjectTestbed: shutdown");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ApplyFlyCameraInputToMainCamera(NWB::Core::ECS::World& world, const SIMDVectorSingle moveAxes, const b8 boosted, const SIMDVectorSingle mouseDelta, const f32 deltaSeconds)noexcept{
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world);
    NWB_ASSERT(cameraView.valid());

    const SIMDVectorSingle resolvedRotation = ResolveFlyCameraStepRotation(LoadFloat(cameraView.transform->rotation), mouseDelta);
    const SIMDVectorSingle resolvedPosition = ResolveFlyCameraPosition(LoadFloat(cameraView.transform->position), moveAxes, resolvedRotation, deltaSeconds, boosted);
    StoreFloat(resolvedRotation, cameraView.transform->rotation);
    StoreFloat(resolvedPosition, cameraView.transform->position);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> Project::CreateInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    auto world = CreateInitialProjectWorld(context);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_testbed_runtime::s_InitWorldFailedText);
        throw RuntimeException(s_InitWorldFailedNarrow.data());
    }
    return MakeNotNullUnique(Move(*world));
}

Project::~Project()noexcept{
    unregisterInputHandler();
    destroyWorld();
}

b8 Project::onStartup(){
    if(!m_sceneSubsystems.loadProjectStartScene(m_context.assetManager))
        return false;
    if(!m_uiPreview.loadAssets(m_context.objectArena, m_context.assetManager, m_sceneSubsystems))
        return false;
    if(auto* const rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>())
        rendererSystem->setActiveSceneSubsystems(&m_sceneSubsystems);
    if(auto* const meshSkinningSystem = m_world->getSystem<NWB::Impl::MeshSkinningSystem>())
        meshSkinningSystem->setActiveSceneSubsystems(&m_sceneSubsystems);
    if(auto* const uiLayerSystem = m_world->getSystem<NWB::Impl::UiLayerSystem>())
        uiLayerSystem->setActiveSceneSubsystems(&m_sceneSubsystems);

    auto activeCameraEntity = m_world->createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    const Float4 cameraPosition(0.0f, s_CharacterCameraTargetY, s_CameraStartDepth);
    activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, cameraPosition);
    NWB_ASSERT(activeCamera.camera.valid());
    auto& cameraTransform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera));
    StoreFloat(QuaternionRotationRollPitchYaw(0.0f, s_PI, 0.0f), cameraTransform.rotation);
    const auto directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
        *m_world,
        s_DefaultDirectionalLightPitch,
        s_DefaultDirectionalLightYaw,
        s_DefaultDirectionalLightRoll,
        s_DefaultDirectionalLightColor,
        s_DefaultDirectionalLightIntensity
    );
    const auto pointLight = NWB::Impl::Scene::CreatePointLightEntity(
        *m_world,
        s_PointLightPosition,
        s_PointLightColor,
        s_PointLightIntensity,
        s_PointLightRange
    );
    if(!directionalLight.valid() || !pointLight.valid())
        return false;

    if(!createDefaultScene())
        return false;
    NWB::Impl::ModelSystem& modelSystem = *MakeNotNull(m_world->getSystem<NWB::Impl::ModelSystem>());
    modelSystem.syncModelRuntimes();
    registerInputHandler();
    return true;
}

void Project::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(__hidden_testbed_runtime::s_ShutdownText);
}

b8 Project::onUpdate(f32 delta){
    const f32 safeDelta = SanitizeFlyCameraDelta(delta);

    updateMainCamera(safeDelta);
    const auto updated = m_world->tick(safeDelta);
    if(!updated){
        NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
        return false;
    }
    return true;
}

void Project::windowFocusUpdate(const b8 focused)noexcept{
    if(!focused)
        clearInputState();
}

void Project::pointerCaptureLost()noexcept{
    // Ordinary button-up already completed the gesture; keep its accumulated camera motion.
    if(!m_mouseLookActive)
        return;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
}

::NWB::Core::KeyboardInputResult Project::keyboardUpdate(const i32 key, [[maybe_unused]] const i32 scancode, const i32 action, [[maybe_unused]] const i32 mods)noexcept{
    if(action == NWB::Core::InputAction::Release)
        setKeyState(key, false);

    if(MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsKeyboard())
        return false;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        setKeyState(key, true);

    return false;
}

b8 Project::mousePosUpdate(const f64 xpos, const f64 ypos)noexcept{
    UpdateFlyCameraMousePosition(
        xpos,
        ypos,
        MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer(),
        m_mouseLookActive,
        m_mousePositionValid,
        m_lastMouseX,
        m_lastMouseY,
        m_pendingMouseDeltaX,
        m_pendingMouseDeltaY
    );
    return false;
}

b8 Project::mouseButtonUpdate(const i32 button, const i32 action, [[maybe_unused]] const i32 mods)noexcept{
    UpdateFlyCameraMouseButton(
        MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer(),
        button == NWB::Core::MouseButton::Right,
        action == NWB::Core::InputAction::Press,
        action == NWB::Core::InputAction::Release,
        m_mouseLookActive,
        m_mousePositionValid
    );
    return false;
}

b8 Project::createDefaultScene(){
    if(!m_sceneSubsystems.instantiateEntities(*m_world)){
        NWB_LOGGER_ERROR(__hidden_testbed_runtime::s_SceneEntitiesInvalidText);
        return false;
    }

    auto uiEntity = m_world->createEntity();
    auto& customUi = uiEntity.addComponent<NWB::Impl::UiPaintComponent>();
    customUi.paint = [this](NWB::Impl::UiPaintContext& context){
        drawUiControls(context);
        m_uiPreview.paint(context);
    };

    NWB_LOGGER_ESSENTIAL_INFO(__hidden_testbed_runtime::s_StartupSceneText, __hidden_testbed_runtime::s_DefaultSceneDescription);
    return true;
}

void Project::registerInputHandler(){
    if(m_inputRegistered)
        return;

    m_context.input.addHandlerToFront(*this);
    m_inputRegistered = true;
}

void Project::unregisterInputHandler()noexcept{
    if(!m_inputRegistered)
        return;

    m_context.input.removeHandler(*this);
    m_inputRegistered = false;
}

void Project::clearInputState()noexcept{
    m_keyPressed.fill(false);
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_lastMouseX = 0.0;
    m_lastMouseY = 0.0;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
}

void Project::setKeyState(const i32 key, const b8 pressed)noexcept{
    const auto keyIndex = ResolveKeyIndex(key, NWB::Core::Key::Menu);
    if(!keyIndex)
        return;

    m_keyPressed[*keyIndex] = pressed;
}

b8 Project::keyPressed(const i32 key)const noexcept{
    const auto keyIndex = ResolveKeyIndex(key, NWB::Core::Key::Menu);
    if(!keyIndex)
        return false;

    return m_keyPressed[*keyIndex];
}

void Project::updateMainCamera(const f32 delta)noexcept{
    const b8 pointerCaptured = MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer();
    // Pending deltas were admitted by scene ownership; later UI hover cannot erase a completed drag.
    const f32 mouseDeltaX = m_pendingMouseDeltaX;
    const f32 mouseDeltaY = m_pendingMouseDeltaY;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    if(pointerCaptured){
        m_mouseLookActive = false;
        m_mousePositionValid = false;
    }

    const b8 keyboardCaptured = MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsKeyboard();
    const SIMDVectorSingle moveAxes = BuildFlyCameraMoveAxes(
        keyboardCaptured,
        keyPressed(NWB::Core::Key::D),
        keyPressed(NWB::Core::Key::A),
        keyPressed(NWB::Core::Key::W),
        keyPressed(NWB::Core::Key::S)
    );
    const b8 boosted = !keyboardCaptured && (keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift));

    __hidden_testbed_runtime::ApplyFlyCameraInputToMainCamera(*m_world, moveAxes, boosted, VectorSet(mouseDeltaX, mouseDeltaY, 0.0f, 0.0f), delta);
}

void Project::destroyWorld()noexcept{
    if(!m_world.owner())
        return;

    DestroyInitialProjectWorld(m_context, m_world.owner());
}

void Project::drawUiControls(NWB::Impl::UiPaintContext& context){
    NWB::Impl::Ui::WindowOptions options;
    options.initialBounds = { s_GalleryMargin, s_GalleryMargin, s_OverviewWidth, s_OverviewHeight };
    options.contentHeightFirstUse = true;
    const auto gallery = m_uiPreview.galleryBounds(context);
    if(!gallery || !CanShowTestbedOverviewWindow(*gallery, options.initialBounds))
        return;
    NWB::Impl::Ui::SeparatorOptions separator;
    separator.thickness = 1.0f;
    if(context.ui.beginWindow("testbed_window", s_WindowTitleText, m_uiWindow, options)){
        const b8 declared = context.ui.label("renderer", s_RendererLine) && context.ui.separator("separator", separator) && context.ui.label("character", s_CharacterLine);
        NWB_FATAL_ASSERT(declared);
    }
    const b8 ended = context.ui.endWindow();
    NWB_FATAL_ASSERT(ended);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

