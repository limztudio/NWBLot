// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/common/log.h>
#include <global/math/frame.h>
#include <core/graphics/module.h>
#include <global/simplemath.h>
#include <impl/assets_model/asset.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_mesh/skinning/module.h>
#include <impl/ecs_model/module.h>
#include <impl/ecs_render/kernel/module.h>
#include <impl/ecs_ui/module.h>

#include <imgui.h>


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
static constexpr f32 s_DefaultDirectionalLightColorR = 1.0f;
static constexpr f32 s_DefaultDirectionalLightColorG = 0.96f;
static constexpr f32 s_DefaultDirectionalLightColorB = 0.88f;
static constexpr f32 s_CharacterCameraTargetY = 0.85f;
// Orbit the camera to the +Z side and yaw 180 degrees so it faces back along -Z onto the model's front.
static constexpr f32 s_CameraStartYaw = s_PI;
static constexpr f32 s_PointLightPositionX = 1.5f;
static constexpr f32 s_PointLightPositionY = 1.6f;
static constexpr f32 s_PointLightPositionZ = 1.5f;
static constexpr f32 s_PointLightColorR = 0.6f;
static constexpr f32 s_PointLightColorG = 0.74f;
static constexpr f32 s_PointLightColorB = 1.0f;
static constexpr f32 s_PointLightIntensity = 2.0f;
static constexpr f32 s_PointLightRange = 16.0f; // larger range = gentler distance falloff so it stays comparable to the directional across the scene
static constexpr f32 s_UiInitialPositionX = 18.0f;
static constexpr f32 s_UiInitialPositionY = 18.0f;
static constexpr f32 s_UiInitialWidth = 360.0f;
static constexpr f32 s_UiInitialHeightAuto = 0.0f;
static constexpr AStringView s_FemaleModelPath = "project/characters/female/model";
static constexpr AStringView s_ModelMaterialPath = "project/materials/mat_skinned_uv";
static constexpr AStringView s_GroundPlaneModelPath = "project/meshes/ground_plane/model";
static constexpr AStringView s_GroundPlaneMaterialPath = "project/materials/mat_white_opaque";
static constexpr tchar s_DefaultSceneDescription[] = NWB_TEXT("45-degree directional + point light, female skinned character on a white ground plane");


[[nodiscard]] static f32 KeyAxis(const bool negative, const bool positive){
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

[[nodiscard]] static f32 ClampPitch(const f32 pitchRadians, const f32 pitchLimitRadians){
    return Min(Max(pitchRadians, -pitchLimitRadians), pitchLimitRadians);
}

[[nodiscard]] static bool ResolveKeyIndex(const i32 key, usize& outIndex){
    if(key < 0 || key > NWB::Core::Key::Menu)
        return false;

    outIndex = static_cast<usize>(key);
    return true;
}

[[nodiscard]] static bool UiWantsKeyboardCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Impl::UiSystem>();
    return uiSystem && uiSystem->wantsKeyboardCapture();
}

[[nodiscard]] static bool UiWantsMouseCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Impl::UiSystem>();
    return uiSystem && uiSystem->wantsMouseCapture();
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
    const f32 safeRightAxis = IsFinite(rightAxis) ? Min(Max(rightAxis, -1.0f), 1.0f) : 0.0f;
    const f32 safeForwardAxis = IsFinite(forwardAxis) ? Min(Max(forwardAxis, -1.0f), 1.0f) : 0.0f;
    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;

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
    const f32 moveLengthSq = VectorGetX(moveLengthSqVector);
    if(moveLengthSq > s_CameraMoveEpsilon){
        const f32 invMoveLength = VectorGetX(VectorReciprocalSqrt(moveLengthSqVector));
        const f32 speed = s_FlyCameraMoveSpeed * (boosted ? s_FlyCameraBoostMultiplier : 1.0f);
        const f32 moveScale = speed * safeDelta * invMoveLength;
        if(!IsFinite(moveScale))
            return;

        const SIMDVector localMove = VectorMultiply(
            VectorSet(safeRightAxis, 0.0f, safeForwardAxis, 0.0f),
            VectorReplicate(moveScale)
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
    if(!cameraView.valid())
        return;

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
    StoreFloat(resolvedRotation, &cameraView.transform->rotation);
    StoreFloat(resolvedPosition, &cameraView.transform->position);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateModelEntity(
    NWB::Core::ECS::World& world,
    const AStringView modelPath,
    const AStringView materialPath
){
    TestbedModelRef model;
    model.virtualPath = Name(modelPath);
    TestbedMaterialRef material;
    material.virtualPath = Name(materialPath);

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
    return CreateModelEntity(world, s_FemaleModelPath, s_ModelMaterialPath);
}

static NWB::Core::ECS::EntityID CreateStaticGroundPlaneEntity(NWB::Core::ECS::World& world){
    return CreateModelEntity(world, s_GroundPlaneModelPath, s_GroundPlaneMaterialPath);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> ProjectTestbed::createInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
#if defined(NWB_TESTBED_FORCE_RAYTRACING_EMULATION) && !defined(NWB_FINAL)
    context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, true);
    context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, true);
    context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, true);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: forced ray tracing off for software-fallback testing"));
#endif

    UniquePtr<NWB::Core::ECS::World> world;
    if(!NWB::CreateInitialProjectWorld(context, world)){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false"));
        throw RuntimeException("ProjectTestbed initialization failed");
    }
    if(!world){
        NWB_LOGGER_FATAL(NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned null world"));
        throw RuntimeException("ProjectTestbed initialization failed");
    }
    return MakeNotNullUnique(Move(world));
}

void ProjectTestbed::verifyRendererSystemOrDie(NWB::Core::ECS::World& world){
    auto* rendererSystem = world.getSystem<NWB::Impl::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(
        rendererSystem,
        NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world")
    );
    auto* meshSkinningSystem = world.getSystem<NWB::Impl::MeshSkinningSystem>();
    NWB_FATAL_ASSERT_MSG(
        meshSkinningSystem,
        NWB_TEXT("ProjectTestbed initialization failed: mesh skinning system is missing in initial world")
    );
    auto* modelSystem = world.getSystem<NWB::Impl::ModelSystem>();
    NWB_FATAL_ASSERT_MSG(
        modelSystem,
        NWB_TEXT("ProjectTestbed initialization failed: model system is missing in initial world")
    );
}

void ProjectTestbed::drawUiControls(){
    ImGui::SetNextWindowPos(ImVec2(__hidden_runtime::s_UiInitialPositionX, __hidden_runtime::s_UiInitialPositionY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(__hidden_runtime::s_UiInitialWidth, __hidden_runtime::s_UiInitialHeightAuto), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("NWB Testbed")){
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Renderer: mesh shader path with compute emulation fallback");
    ImGui::Separator();
    ImGui::TextUnformatted("Character: female model");
    ImGui::End();
}

ProjectTestbed::ProjectTestbed(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(createInitialWorldOrDie(context))
{
    verifyRendererSystemOrDie(*m_world);
}

ProjectTestbed::~ProjectTestbed(){
    unregisterInputHandler();
    destroyWorld();
}

void ProjectTestbed::destroyWorld(){
    if(!m_world.owner())
        return;

    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());

#if defined(NWB_TESTBED_FORCE_RAYTRACING_EMULATION) && !defined(NWB_FINAL)
    m_context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingAccelStruct, false);
    m_context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayTracingPipeline, false);
    m_context.graphics.setFeatureSupportDisabledForTesting(NWB::Core::Feature::RayQuery, false);
#endif
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
    if(auto* cameraTransform = m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera))
        StoreFloat(QuaternionRotationRollPitchYaw(0.0f, __hidden_runtime::s_CameraStartYaw, 0.0f), &cameraTransform->rotation);
    NWB::Impl::Scene::CreateDirectionalLightEntity(
        *m_world,
        __hidden_runtime::s_DefaultDirectionalLightPitch,
        __hidden_runtime::s_DefaultDirectionalLightYaw,
        __hidden_runtime::s_DefaultDirectionalLightRoll,
        Float4(
            __hidden_runtime::s_DefaultDirectionalLightColorR,
            __hidden_runtime::s_DefaultDirectionalLightColorG,
            __hidden_runtime::s_DefaultDirectionalLightColorB
        ),
        __hidden_runtime::s_DefaultDirectionalLightIntensity
    );
    NWB::Impl::Scene::CreatePointLightEntity(
        *m_world,
        Float4(
            __hidden_runtime::s_PointLightPositionX,
            __hidden_runtime::s_PointLightPositionY,
            __hidden_runtime::s_PointLightPositionZ
        ),
        Float4(
            __hidden_runtime::s_PointLightColorR,
            __hidden_runtime::s_PointLightColorG,
            __hidden_runtime::s_PointLightColorB
        ),
        __hidden_runtime::s_PointLightIntensity,
        __hidden_runtime::s_PointLightRange
    );

    createDefaultScene();
    if(auto* modelSystem = m_world->getSystem<NWB::Impl::ModelSystem>())
        modelSystem->syncModelRuntimes();
    registerInputHandler();
    return m_characterEntity.valid();
}

void ProjectTestbed::createDefaultScene(){
    m_characterEntity = __hidden_runtime::CreateSkinnedCharacterEntity(*m_world);
    __hidden_runtime::CreateStaticGroundPlaneEntity(*m_world);

    auto uiEntity = m_world->createEntity();
    auto& ui = uiEntity.addComponent<NWB::Impl::UiComponent>();
    ui.draw = [this](NWB::Impl::UiDrawContext& context){
        static_cast<void>(context);
        drawUiControls();
    };

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        __hidden_runtime::s_DefaultSceneDescription
    );
}

void ProjectTestbed::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: shutdown"));
}

bool ProjectTestbed::onUpdate(f32 delta){
    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;

    updateMainCamera(safeDelta);
    m_world->tick(safeDelta);
    return true;
}

void ProjectTestbed::registerInputHandler(){
    if(m_inputRegistered)
        return;

    m_context.input.addHandlerToBack(*this);
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
    const f32 mouseDeltaX = m_pendingMouseDeltaX;
    const f32 mouseDeltaY = m_pendingMouseDeltaY;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;

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

