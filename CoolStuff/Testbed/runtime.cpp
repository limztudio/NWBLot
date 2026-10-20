// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_ui/module.h>

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


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
// Face the model's front from +Z.
static constexpr f32 s_CameraStartYaw = s_PI;
static constexpr Float4 s_PointLightPosition = Float4(1.5f, 1.6f, 1.5f);
static constexpr Float4 s_PointLightColor = Float4(0.6f, 0.74f, 1.0f);
static constexpr f32 s_PointLightIntensity = 2.0f;
static constexpr f32 s_PointLightRange = 16.0f; // larger range = gentler distance falloff so it stays comparable to the directional across the scene
static constexpr TestbedModelRef s_FemaleModel{"project/characters/female/model"};
static constexpr TestbedMaterialRef s_ModelMaterial{"project/materials/mat_skinned_uv"};
static constexpr TestbedModelRef s_GroundPlaneModel{"project/meshes/ground_plane/model"};
static constexpr TestbedMaterialRef s_GroundPlaneMaterial{"project/materials/mat_white_opaque"};
static constexpr TStringView s_DefaultSceneDescription = NWB_TEXT("45-degree directional + point light, female skinned character on a white ground plane");
static constexpr TStringView s_InitWorldFailedText = NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false");
static constexpr TStringView s_CharacterInvalidText = NWB_TEXT("ProjectTestbed initialization failed: character creation returned an invalid entity");
static constexpr TStringView s_StartupSceneText = NWB_TEXT("ProjectTestbed: startup scene created ({})");
static constexpr TStringView s_ShutdownText = NWB_TEXT("ProjectTestbed: shutdown");
static constexpr AStringView s_InitWorldFailedNarrow = "ProjectTestbed initialization failed";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static f32 KeyAxis(const bool negative, const bool positive)noexcept{
    return VectorGetX(VectorSubtract(VectorReplicate(static_cast<f32>(positive)), VectorReplicate(static_cast<f32>(negative))));
}

[[nodiscard]] static Expected<usize> ResolveKeyIndex(const i32 key)noexcept{
    if(key < 0 || key > NWB::Core::Key::Menu)
        return MakeUnexpected(Failure{});

    return static_cast<usize>(key);
}

[[nodiscard]] static bool UiWantsKeyboardCapture(NWB::Core::ECS::World& world)noexcept{
    const auto* ui = world.getSystem<NWB::Impl::UiLayerSystem>();
    NWB_ASSERT(ui);
    return ui->wantsKeyboard();
}

[[nodiscard]] static bool UiWantsMouseCapture(NWB::Core::ECS::World& world)noexcept{
    const auto* ui = world.getSystem<NWB::Impl::UiLayerSystem>();
    NWB_ASSERT(ui);
    return ui->wantsPointer();
}

[[nodiscard]] static SIMDVectorSingle ResolveFlyCameraAnglesFromRotation(const SIMDVectorSingle rotation)noexcept{
    const SIMDVectorSingle localForward = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const SIMDVectorSingle forwardVector = Vector3NormalizeOr(Vector3Rotate(localForward, rotation), localForward, s_CameraMoveEpsilon);
    const SIMDVectorSingle clampedForward = VectorClamp(forwardVector, s_SIMDNegativeOne, s_SIMDOne);
    SIMDVectorSingle yawRadians = VectorATan2(VectorSplatX(forwardVector), VectorSplatZ(forwardVector));
    SIMDVectorSingle pitchRadians = VectorNegate(VectorASin(VectorSplatY(clampedForward)));

    if(!IsFinite(VectorGetX(yawRadians)))
        yawRadians = VectorZero();
    if(IsFinite(VectorGetX(pitchRadians)))
        pitchRadians = VectorClamp(pitchRadians, VectorReplicate(-s_FlyCameraPitchLimitRadians), VectorReplicate(s_FlyCameraPitchLimitRadians));
    else
        pitchRadians = VectorZero();
    return VectorSelect(VectorSelect(VectorZero(), yawRadians, s_SIMDMaskX), pitchRadians, s_SIMDMaskY);
}

static void ResolveFlyCameraInput(
    const SIMDVectorSingle currentPosition,
    const SIMDVectorSingle currentAngles,
    const SIMDVectorSingle moveAxes,
    const SIMDVectorSingle mouseDelta,
    const SIMDVectorSingle delta,
    const bool boosted,
    SIMDVectorSingle& outRotation,
    SIMDVectorSingle& outPosition
)noexcept{
    const SIMDVectorSingle angleInvalid = VectorSelectControl(!IsFinite(VectorGetX(currentAngles)), !IsFinite(VectorGetY(currentAngles)), 0u, 0u);
    const SIMDVectorSingle angles = VectorSelect(currentAngles, VectorZero(), angleInvalid);
    const SIMDVectorSingle mouseInvalid = VectorSelectControl(!IsFinite(VectorGetX(mouseDelta)), !IsFinite(VectorGetY(mouseDelta)), 0u, 0u);
    const SIMDVectorSingle sanitizedMouseDelta = VectorSelect(mouseDelta, VectorZero(), mouseInvalid);
    const SIMDVectorSingle axesInvalid = VectorSelectControl(!IsFinite(VectorGetX(moveAxes)), !IsFinite(VectorGetY(moveAxes)), 0u, 0u);
    const SIMDVectorSingle sanitizedAxes = VectorClamp(VectorSelect(moveAxes, VectorZero(), axesInvalid), s_SIMDNegativeOne, s_SIMDOne);
    const SIMDVectorSingle safeDelta = VectorMax(IsFinite(VectorGetX(delta)) ? delta : VectorZero(), VectorZero());

    const SIMDVectorSingle updatedAngles = VectorMultiplyAddExpression(sanitizedMouseDelta, VectorReplicate(s_FlyCameraMouseSensitivityRadiansPerPixel), angles);
    SIMDVectorSingle yawRadians = VectorSplatX(updatedAngles);
    if(!IsFinite(VectorGetX(yawRadians)))
        yawRadians = VectorZero();
    const SIMDVectorSingle pitchRadians = VectorClamp(VectorSplatY(updatedAngles), VectorReplicate(-s_FlyCameraPitchLimitRadians), VectorReplicate(s_FlyCameraPitchLimitRadians));
    const SIMDVectorSingle rotationAngles = VectorSelect(VectorSelect(VectorZero(), pitchRadians, s_SIMDMaskX), yawRadians, s_SIMDMaskY);

    outRotation = QuaternionRotationRollPitchYawFromVector(rotationAngles);
    outPosition = Vector3IsFinite(currentPosition) ? currentPosition : VectorZero();

    const SIMDVectorSingle moveLengthSqVector = Vector2LengthSq(sanitizedAxes);
    if(Vector4Greater(moveLengthSqVector, VectorReplicate(s_CameraMoveEpsilon))){
        const SIMDVectorSingle boostLanes = VectorSelect(s_SIMDOne, VectorReplicate(s_FlyCameraBoostMultiplier), VectorReplicateInt(boosted ? s_SELECT_1 : s_SELECT_0));
        const SIMDVectorSingle speedLanes = VectorMultiply(VectorReplicate(s_FlyCameraMoveSpeed), boostLanes);
        const SIMDVectorSingle speedDeltaLanes = VectorMultiply(speedLanes, VectorSplatX(safeDelta));
        const SIMDVectorSingle moveScale = VectorMultiply(speedDeltaLanes, VectorReciprocalSqrt(moveLengthSqVector));
        if(!VectorIsFinite(moveScale, VectorComponentMask::s_XYZW))
            return;

        const SIMDVectorSingle localMove = VectorMultiply(VectorPermute<0, 4, 1, 4>(sanitizedAxes, VectorZero()), moveScale);
        const SIMDVectorSingle worldMove = Vector3Rotate(localMove, outRotation);
        if(!Vector3IsFinite(worldMove))
            return;

        const SIMDVectorSingle newPosition = VectorAdd(outPosition, worldMove);
        if(Vector3IsFinite(newPosition))
            outPosition = newPosition;
    }
}

static void ApplyFlyCameraInputToMainCamera(NWB::Core::ECS::World& world, const f32 rightAxis, const f32 forwardAxis, const bool boosted, const f32 mouseDeltaX, const f32 mouseDeltaY, const f32 delta)noexcept{
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world);
    NWB_ASSERT(cameraView.valid());

    const SIMDVectorSingle angles = ResolveFlyCameraAnglesFromRotation(LoadFloat(cameraView.transform->rotation));
    const SIMDVectorSingle moveAxes = VectorSet(rightAxis, forwardAxis, 0.0f, 0.0f);
    const SIMDVectorSingle mouseDelta = VectorSet(mouseDeltaX, mouseDeltaY, 0.0f, 0.0f);
    SIMDVectorSingle resolvedRotation;
    SIMDVectorSingle resolvedPosition;
    ResolveFlyCameraInput(LoadFloat(cameraView.transform->position), angles, moveAxes, mouseDelta, VectorSet(delta, 0.0f, 0.0f, 0.0f), boosted, resolvedRotation, resolvedPosition);
    StoreFloat(resolvedRotation, cameraView.transform->rotation);
    StoreFloat(resolvedPosition, cameraView.transform->position);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateModelEntity(NWB::Core::ECS::World& world, const TestbedModelRef& model, const TestbedMaterialRef& material){
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


NotNullUniquePtr<NWB::Core::ECS::World> Project::CreateInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    auto world = NWB::CreateInitialProjectWorld(context);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_runtime::s_InitWorldFailedText);
        throw RuntimeException(__hidden_runtime::s_InitWorldFailedNarrow.data());
    }
    return MakeNotNullUnique(Move(*world));
}

Project::Project(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(CreateInitialWorldOrDie(context))
    , m_uiPreview(context.objectArena, context.assetManager)
{}

Project::~Project()noexcept{
    unregisterInputHandler();
    destroyWorld();
}

bool Project::onStartup(){
    auto activeCameraEntity = m_world->createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    const Float4 cameraPosition(0.0f, __hidden_runtime::s_CharacterCameraTargetY, __hidden_runtime::s_CameraStartDepth);
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
    const auto pointLight = NWB::Impl::Scene::CreatePointLightEntity(*m_world, __hidden_runtime::s_PointLightPosition, __hidden_runtime::s_PointLightColor, __hidden_runtime::s_PointLightIntensity, __hidden_runtime::s_PointLightRange);
    if(!directionalLight.valid() || !pointLight.valid())
        return false;

    if(!createDefaultScene())
        return false;
    auto* modelSystemPtr = m_world->getSystem<NWB::Impl::ModelSystem>();
    NWB_ASSERT(modelSystemPtr);
    NWB::Impl::ModelSystem& modelSystem = *modelSystemPtr;
    modelSystem.syncModelRuntimes();
    registerInputHandler();
    return true;
}

void Project::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(__hidden_runtime::s_ShutdownText);
}

bool Project::onUpdate(f32 delta){
    const f32 safeDelta = VectorGetX(VectorMax(VectorSet(IsFinite(delta) ? delta : 0.0f, 0.0f, 0.0f, 0.0f), VectorZero()));

    updateMainCamera(safeDelta);
    const auto updated = m_world->tick(safeDelta);
    if(!updated){
        NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
        return false;
    }
    return true;
}

void Project::windowFocusUpdate(const bool focused)noexcept{
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

bool Project::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods)noexcept{
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

bool Project::mousePosUpdate(const f64 xpos, const f64 ypos)noexcept{
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

    const SIMDVectorDouble mousePosition = VectorDoubleSet(xpos, ypos, 0.0, 0.0);
    const SIMDVectorDouble previousMousePosition = VectorDoubleSet(m_lastMouseX, m_lastMouseY, 0.0, 0.0);
    const SIMDVectorDouble deltaPosition = VectorDoubleSubtract(mousePosition, previousMousePosition);
    const f32 deltaX = static_cast<f32>(VectorDoubleGetX(deltaPosition));
    const f32 deltaY = static_cast<f32>(VectorDoubleGetY(deltaPosition));
    const SIMDVectorSingle mouseDelta = VectorSet(deltaX, deltaY, 0.0f, 0.0f);
    const SIMDVectorSingle pendingDelta = VectorAdd(VectorSet(m_pendingMouseDeltaX, m_pendingMouseDeltaY, 0.0f, 0.0f), mouseDelta);
    if(VectorIsFinite(mouseDelta, VectorComponentMask::s_XY) && VectorIsFinite(pendingDelta, VectorComponentMask::s_XY)){
        m_pendingMouseDeltaX = VectorGetX(pendingDelta);
        m_pendingMouseDeltaY = VectorGetY(pendingDelta);
    }
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;
    return false;
}

bool Project::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods)noexcept{
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

bool Project::createDefaultScene(){
    if(!__hidden_runtime::CreateSkinnedCharacterEntity(*m_world).valid()){
        NWB_LOGGER_ERROR(__hidden_runtime::s_CharacterInvalidText);
        return false;
    }
    __hidden_runtime::CreateStaticGroundPlaneEntity(*m_world);

    auto uiEntity = m_world->createEntity();
    auto& customUi = uiEntity.addComponent<NWB::Impl::UiPaintComponent>();
    customUi.paint = [this](NWB::Impl::UiPaintContext& context){
        drawUiControls(context);
        drawCustomUiControls(context);
    };

    NWB_LOGGER_ESSENTIAL_INFO(__hidden_runtime::s_StartupSceneText, __hidden_runtime::s_DefaultSceneDescription);
    return true;
}

void Project::registerInputHandler(){
    if(m_inputRegistered)
        return;

    m_context.input.addHandlerToFront(*this);
    m_inputRegistered = true;
}

void Project::unregisterInputHandler(){
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

void Project::setKeyState(const i32 key, const bool pressed)noexcept{
    const auto keyIndex = __hidden_runtime::ResolveKeyIndex(key);
    if(!keyIndex)
        return;

    m_keyPressed[*keyIndex] = pressed;
}

bool Project::keyPressed(const i32 key)const noexcept{
    const auto keyIndex = __hidden_runtime::ResolveKeyIndex(key);
    if(!keyIndex)
        return false;

    return m_keyPressed[*keyIndex];
}

void Project::updateMainCamera(const f32 delta)noexcept{
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
    const f32 rightAxis = keyboardCaptured ? 0.0f : __hidden_runtime::KeyAxis(keyPressed(NWB::Core::Key::A), keyPressed(NWB::Core::Key::D));
    const f32 forwardAxis = keyboardCaptured ? 0.0f : __hidden_runtime::KeyAxis(keyPressed(NWB::Core::Key::S), keyPressed(NWB::Core::Key::W));
    const bool boosted = !keyboardCaptured && (keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift));

    __hidden_runtime::ApplyFlyCameraInputToMainCamera(*m_world, rightAxis, forwardAxis, boosted, mouseDeltaX, mouseDeltaY, delta);
}

void Project::destroyWorld(){
    if(!m_world.owner())
        return;

    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());
}


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

