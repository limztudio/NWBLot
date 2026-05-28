// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <core/common/log.h>
#include <core/mesh/frame_math.h>
#include <core/graphics/module.h>
#include <global/simplemath.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_mesh/skinned_asset.h>
#include <core/scene/module.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_ui/module.h>

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestbedSkinnedMeshRef = NWB::Core::Assets::AssetRef<NWB::Impl::SkinnedMesh>;
using TestbedMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

struct EditorVec3 : public Float4{
    constexpr EditorVec3()noexcept
        : Float4(0.0f, 0.0f, 0.0f)
    {}
    constexpr EditorVec3(const f32 xValue, const f32 yValue, const f32 zValue)noexcept
        : Float4(xValue, yValue, zValue)
    {}
};
static_assert(IsStandardLayout_V<EditorVec3>, "EditorVec3 must stay layout-stable");
static_assert(IsTriviallyCopyable_V<EditorVec3>, "EditorVec3 must stay cheap to pass by value");
static_assert(alignof(EditorVec3) >= alignof(Float4), "EditorVec3 must stay SIMD-aligned");
static_assert(sizeof(EditorVec3) == sizeof(Float4), "EditorVec3 must stay one aligned float3 wide");


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraMoveEpsilon = 0.000001f;
static constexpr f32 s_FlyCameraMoveSpeed = 2.5f;
static constexpr f32 s_FlyCameraBoostMultiplier = 4.0f;
static constexpr f32 s_FlyCameraMouseSensitivityRadiansPerPixel = 0.12f * (s_PI / 180.0f);
static constexpr f32 s_FlyCameraPitchLimitRadians = 89.0f * (s_PI / 180.0f);
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_CharacterCameraTargetY = 0.85f;
static constexpr AStringView s_SkinnedMeshFemalePath = "project/characters/female";
static constexpr AStringView s_SkinnedMeshMaterialPath = "project/materials/mat_skinned_uv";


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

[[nodiscard]] static bool FiniteVector3(SIMDVector value){
    return !Vector3IsNaN(value) && !Vector3IsInfinite(value);
}

[[nodiscard]] static bool UiWantsKeyboardCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Impl::UiSystem>();
    return uiSystem && uiSystem->wantsKeyboardCapture();
}

[[nodiscard]] static bool UiWantsMouseCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Impl::UiSystem>();
    return uiSystem && uiSystem->wantsMouseCapture();
}

[[nodiscard]] static EditorVec3 NormalizeVec3(SIMDVector valueVector, const EditorVec3& fallback){
    const SIMDVector lengthSqVector = Vector3LengthSq(valueVector);
    const f32 lengthSq = VectorGetX(lengthSqVector);
    if(!IsFinite(lengthSq) || lengthSq <= 0.000001f)
        return fallback;

    const SIMDVector normalizedVector = VectorMultiply(valueVector, VectorReciprocalSqrt(lengthSqVector));
    if(!FiniteVector3(normalizedVector))
        return fallback;

    EditorVec3 normalized;
    StoreFloat(normalizedVector, &normalized);
    return normalized;
}

static void ResolveFlyCameraAnglesFromTransform(
    const NWB::Core::Scene::TransformComponent& transform,
    f32& outYawRadians,
    f32& outPitchRadians){
    const SIMDVector localForward = VectorSet(0.0f, 0.0f, 1.0f, 0.0f);
    const EditorVec3 forward = NormalizeVec3(
        Vector3Rotate(localForward, LoadFloat(transform.rotation)),
        EditorVec3{ 0.0f, 0.0f, 1.0f }
    );
    const f32 clampedForwardY = Min(Max(forward.y, -1.0f), 1.0f);
    const f32 yawRadians = ATan2(forward.x, forward.z);
    const f32 pitchRadians = -ASin(clampedForwardY);

    outYawRadians = IsFinite(yawRadians) ? yawRadians : 0.0f;
    outPitchRadians = IsFinite(pitchRadians) ? ClampPitch(pitchRadians, s_FlyCameraPitchLimitRadians) : 0.0f;
}

static void ApplyFlyCameraInput(
    NWB::Core::Scene::TransformComponent& transform,
    f32& yawRadians,
    f32& pitchRadians,
    const f32 rightAxis,
    const f32 forwardAxis,
    const bool boosted,
    const f32 mouseDeltaX,
    const f32 mouseDeltaY,
    const f32 delta
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

    const SIMDVector rotation = QuaternionRotationRollPitchYaw(pitchRadians, yawRadians, 0.0f);
    StoreFloat(rotation, &transform.rotation);
    if(!FiniteVector3(LoadFloat(transform.position)))
        transform.position = Float4(0.0f, 0.0f, 0.0f);

    const SIMDVector moveAxis = VectorSet(safeRightAxis, safeForwardAxis, 0.0f, 0.0f);
    const SIMDVector moveLengthSqVector = Vector2LengthSq(moveAxis);
    const f32 moveLengthSq = VectorGetX(moveLengthSqVector);
    if(moveLengthSq > s_CameraMoveEpsilon){
        const f32 invMoveLength = VectorGetX(VectorReciprocalSqrt(moveLengthSqVector));
        const f32 speed = s_FlyCameraMoveSpeed * (boosted ? s_FlyCameraBoostMultiplier : 1.0f);
        const f32 moveScale = speed * safeDelta * invMoveLength;
        if(!IsFinite(moveScale))
            return;

        const SIMDVector localMove = VectorSet(safeRightAxis * moveScale, 0.0f, safeForwardAxis * moveScale, 0.0f);
        const SIMDVector worldMove = Vector3Rotate(localMove, rotation);
        if(!FiniteVector3(worldMove))
            return;

        const SIMDVector newPosition = VectorAdd(LoadFloat(transform.position), worldMove);
        if(FiniteVector3(newPosition))
            StoreFloat(newPosition, &transform.position);
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
    const NWB::Core::Scene::SceneCameraView cameraView = NWB::Core::Scene::ResolveSceneCameraView(world);
    if(!cameraView.valid())
        return;

    f32 yawRadians = 0.0f;
    f32 pitchRadians = 0.0f;
    ResolveFlyCameraAnglesFromTransform(*cameraView.transform, yawRadians, pitchRadians);
    ApplyFlyCameraInput(
        *cameraView.transform,
        yawRadians,
        pitchRadians,
        rightAxis,
        forwardAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta
    );
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSkinnedCharacterEntity(NWB::Core::ECS::World& world){
    TestbedSkinnedMeshRef mesh;
    mesh.virtualPath = Name(s_SkinnedMeshFemalePath);
    TestbedMaterialRef material;
    material.virtualPath = Name(s_SkinnedMeshMaterialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.position = Float4(0.0f, 0.0f, 0.0f);
    transform.scale = Float4(1.0f, 1.0f, 1.0f);

    auto& skinnedMesh = entity.addComponent<NWB::Impl::SkinnedMeshComponent>();
    skinnedMesh.skinnedMesh = mesh;

    auto& renderer = entity.addComponent<NWB::Impl::RendererComponent>();
    renderer.material = material;
    return entity.id();
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NotNullUniquePtr<NWB::Core::ECS::World> ProjectTestbed::createInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
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
    auto* skinnedMeshSystem = world.getSystem<NWB::Impl::SkinnedMeshSystem>();
    NWB_FATAL_ASSERT_MSG(
        skinnedMeshSystem,
        NWB_TEXT("ProjectTestbed initialization failed: skinned mesh system is missing in initial world")
    );
}

void ProjectTestbed::drawUiControls(){
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 0.0f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("NWB Testbed")){
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Renderer: mesh shader path with compute emulation fallback");
    ImGui::Separator();
    ImGui::TextUnformatted("Character: female skinned mesh");
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
}

bool ProjectTestbed::onStartup(){
    auto activeCameraEntity = m_world->createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Core::Scene::ActiveCameraComponent>();
    const Float4 cameraPosition(
        0.0f,
        __hidden_runtime::s_CharacterCameraTargetY,
        -__hidden_runtime::s_CameraStartDepth
    );
    activeCamera.camera = NWB::Core::Scene::CreateSceneCameraEntity(*m_world, cameraPosition);
    NWB::Core::Scene::CreateDirectionalLightEntity(
        *m_world,
        __hidden_runtime::s_DefaultDirectionalLightPitch,
        __hidden_runtime::s_DefaultDirectionalLightYaw,
        0.0f,
        Float4(1.0f, 0.96f, 0.88f),
        __hidden_runtime::s_DefaultDirectionalLightIntensity
    );

    createDefaultScene();
    registerInputHandler();
    return m_characterEntity.valid();
}

void ProjectTestbed::createDefaultScene(){
    m_characterEntity = __hidden_runtime::CreateSkinnedCharacterEntity(*m_world);

    auto uiEntity = m_world->createEntity();
    auto& ui = uiEntity.addComponent<NWB::Impl::UiComponent>();
    ui.draw = [this](NWB::Impl::UiDrawContext& context){
        static_cast<void>(context);
        drawUiControls();
    };

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("directional light and female skinned character")
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

