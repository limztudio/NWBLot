// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <logger/client/logger.h>


namespace __hidden_project_testbed_runtime{


using TestbedGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>;
using TestbedMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraMoveEpsilon = 0.000001f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;


[[nodiscard]] static f32 KeyAxis(const bool negative, const bool positive){
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

[[nodiscard]] static f32 ClampPitch(const f32 pitchRadians, const f32 pitchLimitRadians){
    const f32 limit = Max(0.0f, pitchLimitRadians);
    return Min(Max(pitchRadians, -limit), limit);
}

[[nodiscard]] static bool ResolveKeyIndex(const i32 key, usize& outIndex){
    if(key < 0 || key > NWB::Core::Key::Menu)
        return false;

    outIndex = static_cast<usize>(key);
    return true;
}

[[nodiscard]] static NWB::Core::ECS::EntityID ResolveProjectMainCamera(NWB::Core::ECS::World& world){
    NWB::Core::ECS::EntityID mainCamera = NWB::Core::ECS::ENTITY_ID_INVALID;
    bool foundProject = false;
    world.view<NWB::Core::ECS::ProjectComponent>().each(
        [&](NWB::Core::ECS::EntityID, NWB::Core::ECS::ProjectComponent& project){
            if(foundProject)
                return;

            foundProject = true;
            mainCamera = project.mainCamera;
        }
    );
    return mainCamera;
}

static void ApplyFpsCameraInput(
    NWB::Core::ECS::TransformComponent& transform,
    NWB::Core::ECS::FpsCameraControllerComponent& controller,
    const f32 rightAxis,
    const f32 forwardAxis,
    const f32 verticalAxis,
    const bool boosted,
    const f32 mouseDeltaX,
    const f32 mouseDeltaY,
    const f32 delta
){
    controller.yawRadians += mouseDeltaX * controller.mouseSensitivityRadiansPerPixel;
    controller.pitchRadians = ClampPitch(
        controller.pitchRadians - mouseDeltaY * controller.mouseSensitivityRadiansPerPixel,
        controller.pitchLimitRadians
    );

    const f32 sinYaw = Sin(controller.yawRadians);
    const f32 cosYaw = Cos(controller.yawRadians);
    const f32 sinPitch = Sin(controller.pitchRadians);
    const f32 cosPitch = Cos(controller.pitchRadians);
    const f32 forwardX = sinYaw * cosPitch;
    const f32 forwardY = sinPitch;
    const f32 forwardZ = cosYaw * cosPitch;
    const f32 rightX = cosYaw;
    const f32 rightZ = -sinYaw;

    f32 moveX = rightAxis * rightX + forwardAxis * forwardX;
    f32 moveY = verticalAxis + forwardAxis * forwardY;
    f32 moveZ = rightAxis * rightZ + forwardAxis * forwardZ;
    const f32 moveLengthSq = moveX * moveX + moveY * moveY + moveZ * moveZ;
    if(moveLengthSq > s_CameraMoveEpsilon){
        const f32 invMoveLength = 1.0f / Sqrt(moveLengthSq);
        const f32 speed = controller.moveSpeed * (boosted ? controller.boostMultiplier : 1.0f);
        const f32 moveScale = speed * Max(delta, 0.0f) * invMoveLength;
        moveX *= moveScale;
        moveY *= moveScale;
        moveZ *= moveScale;

        transform.position.x += moveX;
        transform.position.y += moveY;
        transform.position.z += moveZ;
    }

    SourceMath::StoreFloat4A(
        &transform.rotation,
        SourceMath::QuaternionRotationRollPitchYaw(controller.pitchRadians, controller.yawRadians, 0.0f)
    );
}

static void ApplyFpsCameraInputToControlledCamera(
    NWB::Core::ECS::World& world,
    const f32 rightAxis,
    const f32 forwardAxis,
    const f32 verticalAxis,
    const bool boosted,
    const f32 mouseDeltaX,
    const f32 mouseDeltaY,
    const f32 delta
){
    const NWB::Core::ECS::EntityID requestedCamera = ResolveProjectMainCamera(world);
    NWB::Core::ECS::TransformComponent* fallbackTransform = nullptr;
    NWB::Core::ECS::FpsCameraControllerComponent* fallbackController = nullptr;
    bool appliedRequestedCamera = false;

    world.view<
        NWB::Core::ECS::TransformComponent,
        NWB::Core::ECS::CameraComponent,
        NWB::Core::ECS::FpsCameraControllerComponent
    >().each(
        [&](
            NWB::Core::ECS::EntityID entityId,
            NWB::Core::ECS::TransformComponent& transform,
            NWB::Core::ECS::CameraComponent& camera,
            NWB::Core::ECS::FpsCameraControllerComponent& controller
        ){
            if(appliedRequestedCamera)
                return;

            (void)camera;
            if(!fallbackTransform){
                fallbackTransform = &transform;
                fallbackController = &controller;
            }
            if(requestedCamera.valid() && entityId == requestedCamera){
                ApplyFpsCameraInput(
                    transform,
                    controller,
                    rightAxis,
                    forwardAxis,
                    verticalAxis,
                    boosted,
                    mouseDeltaX,
                    mouseDeltaY,
                    delta
                );
                appliedRequestedCamera = true;
            }
        }
    );

    if(appliedRequestedCamera)
        return;
    if(!fallbackTransform || !fallbackController)
        return;

    ApplyFpsCameraInput(
        *fallbackTransform,
        *fallbackController,
        rightAxis,
        forwardAxis,
        verticalAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta
    );
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateMainCameraEntity(NWB::Core::ECS::World& world){
    auto cameraEntity = world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    transform.position = AlignedFloat3Data(0.0f, 0.0f, -s_CameraStartDepth);
    cameraEntity.addComponent<NWB::Core::ECS::CameraComponent>();
    cameraEntity.addComponent<NWB::Core::ECS::FpsCameraControllerComponent>();
    return cameraEntity.id();
}

static void CreateDefaultDirectionalLightEntity(NWB::Core::ECS::World& world){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<NWB::Core::ECS::TransformComponent>();
    SourceMath::StoreFloat4A(
        &transform.rotation,
        SourceMath::QuaternionRotationRollPitchYaw(
            s_DefaultDirectionalLightPitch,
            s_DefaultDirectionalLightYaw,
            0.0f
        )
    );

    auto& light = lightEntity.addComponent<NWB::Core::ECS::LightComponent>();
    light.type = NWB::Core::ECS::LightType::Directional;
    light.color = AlignedFloat3Data(1.0f, 0.96f, 0.88f);
    light.intensity = s_DefaultDirectionalLightIntensity;
}

static void CreateRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedGeometryRef& geometry,
    const TestbedMaterialRef& material
){
    auto entity = world.createEntity();
    entity.addComponent<NWB::Core::ECS::TransformComponent>();

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

NWB::Core::ECSGraphics::RendererSystem& ProjectTestbed::requireRendererSystemOrDie(NWB::Core::ECS::World& world){
    auto* rendererSystem = world.getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(
        rendererSystem,
        NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world")
    );
    return *rendererSystem;
}


ProjectTestbed::ProjectTestbed(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(createInitialWorldOrDie(context))
    , m_rendererSystem(requireRendererSystemOrDie(*m_world))
{}

ProjectTestbed::~ProjectTestbed(){
    unregisterInputHandler();
    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());
}


bool ProjectTestbed::onStartup(){
    (void)m_rendererSystem;

    using TestbedGeometryRef = __hidden_project_testbed_runtime::TestbedGeometryRef;
    using TestbedMaterialRef = __hidden_project_testbed_runtime::TestbedMaterialRef;

    auto projectEntity = m_world->createEntity();
    auto& project = projectEntity.addComponent<NWB::Core::ECS::ProjectComponent>();
    project.mainCamera = __hidden_project_testbed_runtime::CreateMainCameraEntity(*m_world);
    __hidden_project_testbed_runtime::CreateDefaultDirectionalLightEntity(*m_world);

    const TestbedMaterialRef cubeMaterial(Name("project/materials/mat_test"));
    const TestbedMaterialRef transparentMaterial(Name("project/materials/mat_transparent"));

    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/cube")),
        cubeMaterial
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/sphere")),
        transparentMaterial
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/tetrahedron")),
        transparentMaterial
    );

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("directional light, opaque cube, transparent sphere/tetrahedron")
    );
    registerInputHandler();
    return true;
}

void ProjectTestbed::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: shutdown"));
}


bool ProjectTestbed::onUpdate(f32 delta){
    updateMainCamera(delta);
    m_world->tick(delta);

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
    if(!__hidden_project_testbed_runtime::ResolveKeyIndex(key, keyIndex))
        return;

    m_keyPressed[keyIndex] = pressed;
}

bool ProjectTestbed::keyPressed(const i32 key)const{
    usize keyIndex = 0;
    if(!__hidden_project_testbed_runtime::ResolveKeyIndex(key, keyIndex))
        return false;

    return m_keyPressed[keyIndex];
}

void ProjectTestbed::updateMainCamera(const f32 delta){
    const f32 mouseDeltaX = m_pendingMouseDeltaX;
    const f32 mouseDeltaY = m_pendingMouseDeltaY;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;

    const f32 rightAxis = __hidden_project_testbed_runtime::KeyAxis(
        keyPressed(NWB::Core::Key::A),
        keyPressed(NWB::Core::Key::D)
    );
    const f32 forwardAxis = __hidden_project_testbed_runtime::KeyAxis(
        keyPressed(NWB::Core::Key::S),
        keyPressed(NWB::Core::Key::W)
    );
    const f32 verticalAxis = __hidden_project_testbed_runtime::KeyAxis(
        keyPressed(NWB::Core::Key::Q) || keyPressed(NWB::Core::Key::LeftControl) || keyPressed(NWB::Core::Key::RightControl),
        keyPressed(NWB::Core::Key::E) || keyPressed(NWB::Core::Key::Space)
    );
    const bool boosted = keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift);

    __hidden_project_testbed_runtime::ApplyFpsCameraInputToControlledCamera(
        *m_world,
        rightAxis,
        forwardAxis,
        verticalAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta
    );
}

bool ProjectTestbed::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    (void)scancode;
    (void)mods;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        setKeyState(key, true);
    else if(action == NWB::Core::InputAction::Release)
        setKeyState(key, false);

    return false;
}

bool ProjectTestbed::mousePosUpdate(const f64 xpos, const f64 ypos){
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

    m_pendingMouseDeltaX += static_cast<f32>(xpos - m_lastMouseX);
    m_pendingMouseDeltaY += static_cast<f32>(ypos - m_lastMouseY);
    m_lastMouseX = xpos;
    m_lastMouseY = ypos;
    return false;
}

bool ProjectTestbed::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    (void)mods;

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

