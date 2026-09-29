// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/common/log.h>
#include <global/math/frame.h>
#include <core/graphics/runtime/runtime.h>
#include <global/simplemath.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestbedModelRef = NWB::Core::Assets::AssetRef<NWB::Impl::Model>;
using TestbedMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

static constexpr f32 s_DegreesToRadians = s_PI / 180.0f;
static constexpr f32 s_QuarterTurnFraction = 0.25f;
static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraMoveEpsilon = 0.000001f;
static constexpr f32 s_FlyCameraMoveSpeed = 2.5f;
static constexpr f32 s_FlyCameraBoostMultiplier = 4.0f;
static constexpr f32 s_FlyCameraMouseSensitivityDegreesPerPixel = 0.12f;
static constexpr f32 s_FlyCameraPitchLimitDegrees = 89.0f;
static constexpr f32 s_FlyCameraMouseSensitivityRadiansPerPixel = s_FlyCameraMouseSensitivityDegreesPerPixel * s_DegreesToRadians;
static constexpr f32 s_FlyCameraPitchLimitRadians = s_FlyCameraPitchLimitDegrees * s_DegreesToRadians;
static constexpr f32 s_DefaultDirectionalLightPitch = s_PI * s_QuarterTurnFraction; // emission aimed 45 degrees below horizontal (sun shining down)
static constexpr f32 s_DefaultDirectionalLightYaw = s_PI * s_QuarterTurnFraction;   // 45 degrees around the up axis
static constexpr f32 s_DefaultDirectionalLightRoll = 0.0f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 1.0f;
static constexpr Float4 s_DefaultDirectionalLightColor = Float4(1.0f, 0.96f, 0.88f);
static constexpr f32 s_CharacterCameraTargetY = 0.85f;
// Orbit the camera to the +Z side and yaw 180 degrees so it faces back along -Z onto the model's front.
static constexpr f32 s_CameraStartYaw = s_PI;
static constexpr Float4 s_PointLightPosition = Float4(1.5f, 1.6f, 1.5f);
static constexpr Float4 s_PointLightColor = Float4(0.6f, 0.74f, 1.0f);
static constexpr f32 s_PointLightIntensity = 2.0f;
static constexpr f32 s_PointLightRange = 16.0f; // larger range = gentler distance falloff so it stays comparable to the directional across the scene
static constexpr TestbedModelRef s_FemaleModel{"project/characters/female/model"};
static constexpr TestbedMaterialRef s_ModelMaterial{"project/materials/mat_skinned_uv"};
static constexpr TestbedModelRef s_GroundPlaneModel{"project/meshes/ground_plane/model"};
static constexpr TestbedMaterialRef s_GroundPlaneMaterial{"project/materials/mat_white_opaque"};
static constexpr tchar s_DefaultSceneDescription[] = NWB_TEXT("45-degree directional + point light, female skinned character on a white ground plane");
static constexpr tchar s_InitWorldFailedText[] = NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false");
static constexpr tchar s_CharacterInvalidText[] = NWB_TEXT("ProjectTestbed initialization failed: character creation returned an invalid entity");
static constexpr tchar s_StartupSceneText[] = NWB_TEXT("ProjectTestbed: startup scene created ({})");
static constexpr tchar s_ShutdownText[] = NWB_TEXT("ProjectTestbed: shutdown");
static constexpr char s_InitWorldFailedNarrow[] = "ProjectTestbed initialization failed";


[[nodiscard]] static f32 KeyAxis(const bool negative, const bool positive){
    // Branchless selection on SIMD lanes: replicate the integer mask onto every lane, then pick the lane value.
    const SIMDVector positiveLane = VectorSelect(VectorZero(), s_SIMDOne, VectorReplicateInt(positive ? 0xFFFFFFFFu : 0u));
    const SIMDVector negativeLane = VectorSelect(VectorZero(), s_SIMDOne, VectorReplicateInt(negative ? 0xFFFFFFFFu : 0u));
    return VectorGetX(VectorSubtract(positiveLane, negativeLane));
}

[[nodiscard]] static f32 ClampPitch(const f32 pitchRadians, const f32 pitchLimitRadians){
    return VectorGetX(VectorClamp(VectorReplicate(pitchRadians), VectorReplicate(-pitchLimitRadians), VectorReplicate(pitchLimitRadians)));
}

[[nodiscard]] static bool ResolveKeyIndex(const i32 key, usize& outIndex){
    if(key < 0 || key > NWB::Core::Key::Menu)
        return false;

    outIndex = static_cast<usize>(key);
    return true;
}

[[nodiscard]] static bool UiWantsKeyboardCapture(NWB::Core::ECS::World& world){
    const auto* ui = world.getSystem<NWB::Impl::UiLayerSystem>();
    NWB_ASSERT(ui);
    return ui->wantsKeyboard();
}

[[nodiscard]] static bool UiWantsMouseCapture(NWB::Core::ECS::World& world){
    const auto* ui = world.getSystem<NWB::Impl::UiLayerSystem>();
    NWB_ASSERT(ui);
    return ui->wantsPointer();
}

static void ResolveFlyCameraAnglesFromRotation(
    const SIMDVector rotation,
    f32& outYawRadians,
    f32& outPitchRadians){
    const SIMDVector localForward = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVector forwardVector = Vector3NormalizeOr(
        Vector3Rotate(localForward, rotation),
        localForward,
        s_CameraMoveEpsilon
    );
    const SIMDVector clampedForward = VectorClamp(forwardVector, s_SIMDNegativeOne, s_SIMDOne);
    const f32 yawRadians = VectorGetX(VectorATan2(VectorSplatX(forwardVector), VectorSplatZ(forwardVector)));
    const f32 pitchRadians = VectorGetX(VectorNegate(VectorASin(VectorSplatY(clampedForward))));

    outYawRadians = IsFinite(yawRadians) ? yawRadians : 0.0f;
    outPitchRadians = IsFinite(pitchRadians) ? ClampPitch(pitchRadians, s_FlyCameraPitchLimitRadians) : 0.0f;
}

static void ResolveFlyCameraInput(
    const SIMDVector currentPosition,
    f32& yawRadians,
    f32& pitchRadians,
    const f32 rightAxis,
    const f32 forwardAxis,
    const bool boosted,
    const f32 mouseDeltaX,
    const f32 mouseDeltaY,
    const f32 delta,
    SIMDVector& outRotation,
    SIMDVector& outPosition
){
    if(!IsFinite(yawRadians))
        yawRadians = 0.0f;
    if(!IsFinite(pitchRadians))
        pitchRadians = 0.0f;

    const f32 safeMouseDeltaX = IsFinite(mouseDeltaX) ? mouseDeltaX : 0.0f;
    const f32 safeMouseDeltaY = IsFinite(mouseDeltaY) ? mouseDeltaY : 0.0f;
    const SIMDVector sanitizedAxes = VectorSaturate(VectorSet(IsFinite(rightAxis) ? rightAxis : 0.0f, IsFinite(forwardAxis) ? forwardAxis : 0.0f, 0.0f, 0.0f));
    const f32 safeRightAxis = VectorGetX(sanitizedAxes);
    const f32 safeForwardAxis = VectorGetY(sanitizedAxes);
    const f32 safeDelta = VectorGetX(VectorMax(VectorSet(IsFinite(delta) ? delta : 0.0f, 0.0f, 0.0f, 0.0f), VectorZero()));

    yawRadians += safeMouseDeltaX * s_FlyCameraMouseSensitivityRadiansPerPixel;
    if(!IsFinite(yawRadians))
        yawRadians = 0.0f;
    pitchRadians = ClampPitch(
        pitchRadians + safeMouseDeltaY * s_FlyCameraMouseSensitivityRadiansPerPixel,
        s_FlyCameraPitchLimitRadians
    );

    outRotation = QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, 0.0f);
    outPosition = Vector3IsFinite(currentPosition) ? currentPosition : VectorZero();

    const SIMDVector moveAxis = VectorSet(safeRightAxis, safeForwardAxis, 0.0f, 0.0f);
    const SIMDVector moveLengthSqVector = Vector2LengthSq(moveAxis);
    if(Vector4Greater(moveLengthSqVector, VectorReplicate(s_CameraMoveEpsilon))){
        // Boost select and speed*delta both run on lanes; the scalar multiply is superseded by speedLanes/speedDeltaLanes.
        const SIMDVector boostLanes = VectorSelect(s_SIMDOne, VectorReplicate(s_FlyCameraBoostMultiplier), VectorReplicateInt(boosted ? 0xFFFFFFFFu : 0u));
        const SIMDVector speedLanes = VectorMultiply(VectorReplicate(s_FlyCameraMoveSpeed), boostLanes);
        const SIMDVector speedDeltaLanes = VectorMultiply(speedLanes, VectorReplicate(safeDelta));
        const SIMDVector moveScale = VectorMultiply(
            speedDeltaLanes,
            VectorReciprocalSqrt(moveLengthSqVector)
        );
        if(!VectorIsFinite(moveScale, VectorComponentMask::s_XYZW))
            return;

        const SIMDVector localMove = VectorMultiply(
            VectorSet(safeRightAxis, 0.0f, safeForwardAxis, 0.0f),
            moveScale
        );
        const SIMDVector worldMove = Vector3Rotate(localMove, outRotation);
        if(!Vector3IsFinite(worldMove))
            return;

        const SIMDVector newPosition = VectorAdd(outPosition, worldMove);
        if(Vector3IsFinite(newPosition))
            outPosition = newPosition;
    }
}

static void ApplyFlyCameraInputToMainCamera(
    NWB::Core::ECS::World& world,
    const f32 rightAxis,
    const f32 forwardAxis,
    const bool boosted,
    const f32 mouseDeltaX,
    const f32 mouseDeltaY,
    const f32 delta
){
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world);
    NWB_ASSERT(cameraView.valid());

    f32 yawRadians = 0.0f;
    f32 pitchRadians = 0.0f;
    ResolveFlyCameraAnglesFromRotation(LoadFloat(cameraView.transform->rotation), yawRadians, pitchRadians);
    SIMDVector resolvedRotation;
    SIMDVector resolvedPosition;
    ResolveFlyCameraInput(
        LoadFloat(cameraView.transform->position),
        yawRadians,
        pitchRadians,
        rightAxis,
        forwardAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta,
        resolvedRotation,
        resolvedPosition
    );
    StoreFloat(resolvedRotation, cameraView.transform->rotation);
    StoreFloat(resolvedPosition, cameraView.transform->position);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateModelEntity(
    NWB::Core::ECS::World& world,
    const TestbedModelRef& model,
    const TestbedMaterialRef& material
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Impl::Scene::TransformComponent>();
    transform.position = Float4(0.0f, 0.0f, 0.0f);
    transform.scale = Float4(1.0f, 1.0f, 1.0f);

    auto& modelComponent = entity.addComponent<NWB::Impl::ModelComponent>();
    modelComponent.model = model;

    auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
    renderer.material = material;
    return entity.id();
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSkinnedCharacterEntity(NWB::Core::ECS::World& world){
    return CreateModelEntity(world, s_FemaleModel, s_ModelMaterial);
}

static void CreateStaticGroundPlaneEntity(NWB::Core::ECS::World& world){
    const auto groundEntity = CreateModelEntity(world, s_GroundPlaneModel, s_GroundPlaneMaterial);
    NWB_FATAL_ASSERT(groundEntity.valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> ProjectTestbed::createInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    UniquePtr<NWB::Core::ECS::World> world;
    if(!NWB::CreateInitialProjectWorld(context, world)){
        NWB_LOGGER_FATAL(__hidden_runtime::s_InitWorldFailedText);
        throw RuntimeException(__hidden_runtime::s_InitWorldFailedNarrow);
    }
    return MakeNotNullUnique(Move(world));
}

ProjectTestbed::ProjectTestbed(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(createInitialWorldOrDie(context))
    , m_uiPreview(context.objectArena)
{}

ProjectTestbed::~ProjectTestbed(){
    unregisterInputHandler();
    destroyWorld();
}

void ProjectTestbed::destroyWorld(){
    if(!m_world.owner())
        return;

    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());
}

bool ProjectTestbed::onStartup(){
    auto activeCameraEntity = m_world->createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    const Float4 cameraPosition(
        0.0f,
        __hidden_runtime::s_CharacterCameraTargetY,
        __hidden_runtime::s_CameraStartDepth
    );
    activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, cameraPosition);
    auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera);
    NWB_ASSERT(activeCamera.camera.valid());
    NWB_ASSERT(cameraTransform);
    StoreFloat(QuaternionRotationRollPitchYaw(0.0f, __hidden_runtime::s_CameraStartYaw, 0.0f), cameraTransform->rotation);
    const auto directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
        *m_world,
        __hidden_runtime::s_DefaultDirectionalLightPitch,
        __hidden_runtime::s_DefaultDirectionalLightYaw,
        __hidden_runtime::s_DefaultDirectionalLightRoll,
        __hidden_runtime::s_DefaultDirectionalLightColor,
        __hidden_runtime::s_DefaultDirectionalLightIntensity
    );
    const auto pointLight = NWB::Impl::Scene::CreatePointLightEntity(
        *m_world,
        __hidden_runtime::s_PointLightPosition,
        __hidden_runtime::s_PointLightColor,
        __hidden_runtime::s_PointLightIntensity,
        __hidden_runtime::s_PointLightRange
    );
    if(!directionalLight.valid() || !pointLight.valid())
        return false;
    NWB_ASSERT(directionalLight.valid());
    NWB_ASSERT(pointLight.valid());

    if(!createDefaultScene())
        return false;
    auto* modelSystemPtr = m_world->getSystem<NWB::Impl::ModelSystem>();
    NWB_ASSERT(modelSystemPtr);
    NWB::Impl::ModelSystem& modelSystem = *modelSystemPtr;
    modelSystem.syncModelRuntimes();
    registerInputHandler();
    return true;
}

bool ProjectTestbed::createDefaultScene(){
    if(!__hidden_runtime::CreateSkinnedCharacterEntity(*m_world).valid()){
        NWB_LOGGER_ERROR(__hidden_runtime::s_CharacterInvalidText);
        return false;
    }
    __hidden_runtime::CreateStaticGroundPlaneEntity(*m_world);

    auto uiEntity = m_world->createEntity();
    auto& customUi = uiEntity.addComponent<NWB::Impl::UiPaintComponent>();
    customUi.paint = [this](NWB::Impl::UiPaintContext& context){
        drawCustomUiControls(context);
        drawUiControls(context);
    };

    NWB_LOGGER_ESSENTIAL_INFO(
        __hidden_runtime::s_StartupSceneText,
        __hidden_runtime::s_DefaultSceneDescription
    );
    return true;
}

void ProjectTestbed::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(__hidden_runtime::s_ShutdownText);
}

bool ProjectTestbed::onUpdate(f32 delta){
    const f32 safeDelta = VectorGetX(VectorMax(VectorSet(IsFinite(delta) ? delta : 0.0f, 0.0f, 0.0f, 0.0f), VectorZero()));

    updateMainCamera(safeDelta);
    m_world->tick(safeDelta);
    return true;
}

void ProjectTestbed::registerInputHandler(){
    if(m_inputRegistered)
        return;

    m_context.input.addHandlerToFront(*this);
    m_inputRegistered = true;
}

void ProjectTestbed::unregisterInputHandler(){
    if(!m_inputRegistered)
        return;

    m_context.input.removeHandler(*this);
    m_inputRegistered = false;
}

void ProjectTestbed::clearInputState(){
    m_keyPressed.fill(false);
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    m_lastMouseX = 0.0;
    m_lastMouseY = 0.0;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
}

void ProjectTestbed::setKeyState(const i32 key, const bool pressed){
    usize keyIndex = 0;
    if(!__hidden_runtime::ResolveKeyIndex(key, keyIndex))
        return;

    m_keyPressed[keyIndex] = pressed;
}

bool ProjectTestbed::keyPressed(const i32 key)const{
    usize keyIndex = 0;
    if(!__hidden_runtime::ResolveKeyIndex(key, keyIndex))
        return false;

    return m_keyPressed[keyIndex];
}

void ProjectTestbed::updateMainCamera(const f32 delta){
    const bool pointerCaptured = __hidden_runtime::UiWantsMouseCapture(*m_world);
    // Pending deltas were admitted by scene ownership; later UI hover cannot erase a completed drag.
    const f32 mouseDeltaX = m_pendingMouseDeltaX;
    const f32 mouseDeltaY = m_pendingMouseDeltaY;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
    if(pointerCaptured){
        m_mouseLookActive = false;
        m_mousePositionValid = false;
    }

    const bool keyboardCaptured = __hidden_runtime::UiWantsKeyboardCapture(*m_world);
    const f32 rightAxis = keyboardCaptured
        ? 0.0f
        : __hidden_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::A),
            keyPressed(NWB::Core::Key::D)
        )
    ;
    const f32 forwardAxis = keyboardCaptured
        ? 0.0f
        : __hidden_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::S),
            keyPressed(NWB::Core::Key::W)
        )
    ;
    const bool boosted = !keyboardCaptured && (keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift));

    __hidden_runtime::ApplyFlyCameraInputToMainCamera(
        *m_world,
        rightAxis,
        forwardAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta
    );
}

void ProjectTestbed::windowFocusUpdate(const bool focused){
    if(!focused)
        clearInputState();
}

void ProjectTestbed::pointerCaptureLost(){
    // Ordinary button-up already completed the gesture; keep its accumulated camera motion.
    if(!m_mouseLookActive)
        return;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;
}

bool ProjectTestbed::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);

    if(action == NWB::Core::InputAction::Release)
        setKeyState(key, false);

    if(__hidden_runtime::UiWantsKeyboardCapture(*m_world))
        return false;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        setKeyState(key, true);

    return false;
}

bool ProjectTestbed::mousePosUpdate(const f64 xpos, const f64 ypos){
    if(!IsFinite(xpos) || !IsFinite(ypos)){
        m_mousePositionValid = false;
        return false;
    }

    if(__hidden_runtime::UiWantsMouseCapture(*m_world)){
        m_mousePositionValid = false;
        return false;
    }

    if(!m_mouseLookActive){
        m_mousePositionValid = false;
        return false;
    }

    if(!m_mousePositionValid){
        m_lastMouseX = xpos;
        m_lastMouseY = ypos;
        m_mousePositionValid = true;
        return false;
    }

    const f32 deltaX = static_cast<f32>(xpos - m_lastMouseX);
    const f32 deltaY = static_cast<f32>(ypos - m_lastMouseY);
    const f32 pendingDeltaX = m_pendingMouseDeltaX + deltaX;
    const f32 pendingDeltaY = m_pendingMouseDeltaY + deltaY;
    if(
        IsFinite(deltaX)
        && IsFinite(deltaY)
        && IsFinite(pendingDeltaX)
        && IsFinite(pendingDeltaY)
    ){
        m_pendingMouseDeltaX = pendingDeltaX;
        m_pendingMouseDeltaY = pendingDeltaY;
    }
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;
    return false;
}

bool ProjectTestbed::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    static_cast<void>(mods);

    if(__hidden_runtime::UiWantsMouseCapture(*m_world)){
        if(button == NWB::Core::MouseButton::Right && action == NWB::Core::InputAction::Release){
            m_mouseLookActive = false;
            m_mousePositionValid = false;
        }
        return false;
    }

    if(button != NWB::Core::MouseButton::Right)
        return false;

    if(action == NWB::Core::InputAction::Press){
        m_mouseLookActive = true;
        m_mousePositionValid = false;
    }
    else if(action == NWB::Core::InputAction::Release){
        m_mouseLookActive = false;
        m_mousePositionValid = false;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

