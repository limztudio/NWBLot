// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <logger/client/logger.h>


namespace __hidden_project_testbed_runtime{


using TestbedGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>;
using TestbedDeformableGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::DeformableGeometry>;
using TestbedMaterialRef = NWB::Core::Assets::AssetRef<NWB::Impl::Material>;

struct EditorVec3{
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 z = 0.0f;
};


static constexpr f32 s_CameraStartDepth = 2.2f;
static constexpr f32 s_CameraMoveEpsilon = 0.000001f;
static constexpr f32 s_DefaultDirectionalLightPitch = -0.65f;
static constexpr f32 s_DefaultDirectionalLightYaw = 0.65f;
static constexpr f32 s_DefaultDirectionalLightIntensity = 2.0f;
static constexpr f32 s_DeformableSkinPivotY = -0.5f;
static constexpr f32 s_DeformableSkinMaxAngle = 0.45f;
static constexpr f32 s_AccessoryNormalOffset = 0.08f;
static constexpr f32 s_AccessoryUniformScale = 0.16f;


[[nodiscard]] static f32 KeyAxis(const bool negative, const bool positive){
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

[[nodiscard]] static f32 ClampPitch(const f32 pitchRadians, const f32 pitchLimitRadians){
    const f32 limit = Max(0.0f, pitchLimitRadians);
    return Min(Max(pitchRadians, -limit), limit);
}

[[nodiscard]] static f32 ClampSurfaceEditRadius(const f32 radius){
    return Min(Max(radius, 0.08f), 0.5f);
}

[[nodiscard]] static f32 ClampSurfaceEditEllipseRatio(const f32 ellipseRatio){
    return Min(Max(ellipseRatio, 0.5f), 2.0f);
}

[[nodiscard]] static f32 ClampSurfaceEditDepth(const f32 depth){
    return Min(Max(depth, 0.04f), 0.45f);
}

[[nodiscard]] static bool TanHalfFov(const f32 verticalFovRadians, f32& outTanHalfFov){
    outTanHalfFov = 0.0f;
    if(!IsFinite(verticalFovRadians))
        return false;

    const f32 halfFov = verticalFovRadians * 0.5f;
    const f32 sinHalfFov = Sin(halfFov);
    const f32 cosHalfFov = Cos(halfFov);
    if(!IsFinite(sinHalfFov)
        || !IsFinite(cosHalfFov)
        || (cosHalfFov > -0.000001f && cosHalfFov < 0.000001f)
    )
        return false;

    outTanHalfFov = sinHalfFov / cosHalfFov;
    return IsFinite(outTanHalfFov) && outTanHalfFov > 0.0f;
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

[[nodiscard]] static EditorVec3 NormalizeVec3(const EditorVec3& value, const EditorVec3& fallback){
    const f32 lengthSq = (value.x * value.x) + (value.y * value.y) + (value.z * value.z);
    if(!IsFinite(lengthSq) || lengthSq <= 0.000001f)
        return fallback;

    const f32 invLength = 1.0f / Sqrt(lengthSq);
    return EditorVec3{
        value.x * invLength,
        value.y * invLength,
        value.z * invLength
    };
}

[[nodiscard]] static EditorVec3 RotateDirectionByQuaternion(
    const EditorVec3& value,
    const AlignedFloat4Data& rotation){
    const Float3Data direction(value.x, value.y, value.z);
    Float3Data rotatedDirection;
    SourceMath::StoreFloat3(
        &rotatedDirection,
        SourceMath::Vector3Rotate(SourceMath::LoadFloat3(&direction), SourceMath::LoadFloat4A(&rotation))
    );
    return EditorVec3{ rotatedDirection.x, rotatedDirection.y, rotatedDirection.z };
}

[[nodiscard]] static bool BuildEditorPickRay(
    NWB::Core::ECS::World& world,
    const f64 cursorX,
    const f64 cursorY,
    NWB::Core::ECSGraphics::DeformablePickingRay& outRay){
    const NWB::Core::ECS::EntityID mainCamera = ResolveProjectMainCamera(world);
    NWB::Core::ECS::TransformComponent* cameraTransform = nullptr;
    NWB::Core::ECS::CameraComponent* cameraComponent = nullptr;
    bool foundCamera = false;

    world.view<NWB::Core::ECS::TransformComponent, NWB::Core::ECS::CameraComponent>().each(
        [&](
            NWB::Core::ECS::EntityID entity,
            NWB::Core::ECS::TransformComponent& transform,
            NWB::Core::ECS::CameraComponent& camera
        ){
            if(foundCamera)
                return;
            if(mainCamera.valid() && entity != mainCamera)
                return;

            cameraTransform = &transform;
            cameraComponent = &camera;
            foundCamera = true;
        }
    );
    if(!foundCamera || !cameraTransform || !cameraComponent)
        return false;

    const NWB::ProjectFrameClientSize clientSize = NWB::QueryProjectFrameClientSize();
    const f32 width = static_cast<f32>(clientSize.width != 0u ? clientSize.width : 1u);
    const f32 height = static_cast<f32>(clientSize.height != 0u ? clientSize.height : 1u);
    const f32 ndcX = (2.0f * static_cast<f32>(cursorX) / width) - 1.0f;
    const f32 ndcY = 1.0f - (2.0f * static_cast<f32>(cursorY) / height);
    const f32 aspect = width / height;
    f32 tanHalfFov = 0.0f;
    if(!TanHalfFov(cameraComponent->verticalFovRadians, tanHalfFov)
        || !IsFinite(cameraComponent->nearPlane)
        || !IsFinite(cameraComponent->farPlane)
        || cameraComponent->nearPlane < 0.0f
        || cameraComponent->nearPlane >= cameraComponent->farPlane
    )
        return false;

    EditorVec3 localDirection{
        ndcX * tanHalfFov * aspect,
        ndcY * tanHalfFov,
        1.0f
    };
    localDirection = NormalizeVec3(localDirection, EditorVec3{ 0.0f, 0.0f, 1.0f });
    const EditorVec3 worldDirection = NormalizeVec3(
        RotateDirectionByQuaternion(localDirection, cameraTransform->rotation),
        EditorVec3{ 0.0f, 0.0f, 1.0f }
    );

    outRay.origin = Float3Data(cameraTransform->position.x, cameraTransform->position.y, cameraTransform->position.z);
    outRay.direction = Float3Data(worldDirection.x, worldDirection.y, worldDirection.z);
    outRay.minDistance = cameraComponent->nearPlane;
    outRay.maxDistance = cameraComponent->farPlane;
    return true;
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
    const TestbedMaterialRef& material,
    const AlignedFloat3Data& position,
    const f32 uniformScale
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::ECS::TransformComponent>();
    transform.position = position;
    transform.scale = AlignedFloat3Data(uniformScale, uniformScale, uniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
}

[[nodiscard]] static NWB::Core::ECSGraphics::DeformableJointMatrix BuildProxySkinJoint(const f32 angleRadians){
    const f32 sinAngle = Sin(angleRadians);
    const f32 cosAngle = Cos(angleRadians);

    NWB::Core::ECSGraphics::DeformableJointMatrix joint;
    joint.column0 = Float4Data(1.0f, 0.0f, 0.0f, 0.0f);
    joint.column1 = Float4Data(0.0f, cosAngle, sinAngle, 0.0f);
    joint.column2 = Float4Data(0.0f, -sinAngle, cosAngle, 0.0f);
    joint.column3 = Float4Data(
        0.0f,
        s_DeformableSkinPivotY * (1.0f - cosAngle),
        -s_DeformableSkinPivotY * sinAngle,
        1.0f
    );
    return joint;
}

static void UpdateProxySkinPalette(
    NWB::Core::ECSGraphics::DeformableJointPaletteComponent& jointPalette,
    const f32 timeSeconds)
{
    jointPalette.joints.resize(2u);
    jointPalette.joints[0] = NWB::Core::ECSGraphics::DeformableJointMatrix{};
    jointPalette.joints[1] = BuildProxySkinJoint(s_DeformableSkinMaxAngle * Sin(timeSeconds * 0.9f));
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateDeformableRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedDeformableGeometryRef& geometry,
    const TestbedMaterialRef& material,
    const AlignedFloat3Data& position,
    const f32 uniformScale
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::ECS::TransformComponent>();
    transform.position = position;
    transform.scale = AlignedFloat3Data(uniformScale, uniformScale, uniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::DeformableRendererComponent>();
    renderer.deformableGeometry = geometry;
    renderer.material = material;

    auto& morphWeights = entity.addComponent<NWB::Core::ECSGraphics::DeformableMorphWeightsComponent>();
    morphWeights.weights.resize(1u);
    morphWeights.weights[0].morph = Name("lift");
    morphWeights.weights[0].weight = 0.0f;

    auto& jointPalette = entity.addComponent<NWB::Core::ECSGraphics::DeformableJointPaletteComponent>();
    UpdateProxySkinPalette(jointPalette, 0.0f);

    auto& displacement = entity.addComponent<NWB::Core::ECSGraphics::DeformableDisplacementComponent>();
    displacement.amplitudeScale = 1.0f;
    displacement.enabled = true;
    return entity.id();
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateAccessoryRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedGeometryRef& geometry,
    const TestbedMaterialRef& material){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::ECS::TransformComponent>();
    transform.scale = AlignedFloat3Data(s_AccessoryUniformScale, s_AccessoryUniformScale, s_AccessoryUniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
    renderer.visible = false;
    entity.addComponent<NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent>();
    return entity.id();
}

static void ResolvePickingInputs(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    NWB::Core::ECSGraphics::DeformablePickingInputs& outInputs){
    outInputs = NWB::Core::ECSGraphics::DeformablePickingInputs{};
    outInputs.morphWeights = world.tryGetComponent<NWB::Core::ECSGraphics::DeformableMorphWeightsComponent>(entity);
    outInputs.jointPalette = world.tryGetComponent<NWB::Core::ECSGraphics::DeformableJointPaletteComponent>(entity);
    outInputs.displacement = world.tryGetComponent<NWB::Core::ECSGraphics::DeformableDisplacementComponent>(entity);
    outInputs.transform = world.tryGetComponent<NWB::Core::ECS::TransformComponent>(entity);
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

void ProjectTestbed::verifyRendererSystemOrDie(NWB::Core::ECS::World& world){
    auto* rendererSystem = world.getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(
        rendererSystem,
        NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world")
    );
}


ProjectTestbed::ProjectTestbed(NWB::ProjectRuntimeContext& context)
    : m_context(context)
    , m_world(createInitialWorldOrDie(context))
{
    verifyRendererSystemOrDie(*m_world);
}

ProjectTestbed::~ProjectTestbed(){
    unregisterInputHandler();
    NWB::DestroyInitialProjectWorld(m_context, m_world.owner());
}


bool ProjectTestbed::onStartup(){
    using TestbedGeometryRef = __hidden_project_testbed_runtime::TestbedGeometryRef;
    using TestbedDeformableGeometryRef = __hidden_project_testbed_runtime::TestbedDeformableGeometryRef;
    using TestbedMaterialRef = __hidden_project_testbed_runtime::TestbedMaterialRef;

    auto projectEntity = m_world->createEntity();
    auto& project = projectEntity.addComponent<NWB::Core::ECS::ProjectComponent>();
    project.mainCamera = __hidden_project_testbed_runtime::CreateMainCameraEntity(*m_world);
    __hidden_project_testbed_runtime::CreateDefaultDirectionalLightEntity(*m_world);

    const TestbedMaterialRef cubeMaterial(Name("project/materials/mat_test"));
    const TestbedMaterialRef transparentMaterial(Name("project/materials/mat_transparent"));
    const TestbedMaterialRef deformableUvMaterial(Name("project/materials/mat_deformable_uv"));

    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/cube")),
        cubeMaterial,
        AlignedFloat3Data(-0.55f, 0.0f, 0.0f),
        0.65f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/cube")),
        cubeMaterial,
        AlignedFloat3Data(0.55f, 0.0f, 0.0f),
        0.9f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/sphere")),
        transparentMaterial,
        AlignedFloat3Data(1.45f, 0.0f, 0.0f),
        0.75f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        TestbedGeometryRef(Name("project/meshes/tetrahedron")),
        transparentMaterial,
        AlignedFloat3Data(-1.45f, 0.0f, 0.0f),
        0.8f
    );
    m_deformableMorphEntity = __hidden_project_testbed_runtime::CreateDeformableRendererEntity(
        *m_world,
        TestbedDeformableGeometryRef(Name("project/characters/proxy_deformable")),
        deformableUvMaterial,
        AlignedFloat3Data(0.0f, 0.85f, 0.0f),
        0.8f
    );

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("directional light, two shared cube instances, transparent sphere/tetrahedron, animated deformable proxy")
    );
    logSurfaceEditControls();
    registerInputHandler();
    return true;
}

void ProjectTestbed::onShutdown(){
    unregisterInputHandler();
    clearInputState();
    clearSurfaceEditPreview();
    clearPendingSurfaceEditAccessory();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: shutdown"));
}


bool ProjectTestbed::onUpdate(f32 delta){
    updateMainCamera(delta);
    updateDeformableMorph(delta);
    m_world->tick(delta);
    attachPendingSurfaceEditAccessory();
    updateSurfaceEditAccessories();

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
    m_cursorX = 0.0;
    m_cursorY = 0.0;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
    m_cursorPositionValid = false;
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

void ProjectTestbed::updateDeformableMorph(const f32 delta){
    if(!m_deformableMorphEntity.valid())
        return;

    m_deformableMorphTime += Max(delta, 0.0f);

    auto* morphWeights =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableMorphWeightsComponent>(m_deformableMorphEntity)
    ;
    if(morphWeights && !morphWeights->weights.empty())
        morphWeights->weights[0].weight = 0.5f + 0.5f * Sin(m_deformableMorphTime * 1.35f);

    auto* jointPalette =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableJointPaletteComponent>(m_deformableMorphEntity)
    ;
    if(jointPalette)
        __hidden_project_testbed_runtime::UpdateProxySkinPalette(*jointPalette, m_deformableMorphTime);

    auto* displacement =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableDisplacementComponent>(m_deformableMorphEntity)
    ;
    if(displacement){
        const f32 displacementAxis = __hidden_project_testbed_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::Z),
            keyPressed(NWB::Core::Key::X)
        );
        if(displacementAxis != 0.0f)
            m_deformableDisplacementScale = Max(0.0f, m_deformableDisplacementScale + displacementAxis * Max(delta, 0.0f));

        displacement->enabled = !keyPressed(NWB::Core::Key::C);
        displacement->amplitudeScale = m_deformableDisplacementScale;
    }
}

void ProjectTestbed::updateSurfaceEditAccessories(){
    auto* rendererSystem = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem)
        return;

    m_world->view<
        NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent,
        NWB::Core::ECS::TransformComponent,
        NWB::Core::ECSGraphics::RendererComponent
    >().each(
        [&](NWB::Core::ECS::EntityID entity,
            NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent& attachment,
            NWB::Core::ECS::TransformComponent& transform,
            NWB::Core::ECSGraphics::RendererComponent& renderer)
        {
            (void)entity;
            const auto* instance = rendererSystem->findDeformableRuntimeMesh(attachment.runtimeMesh);
            if(!instance){
                renderer.visible = false;
                return;
            }

            NWB::Core::ECSGraphics::DeformablePickingInputs inputs;
            __hidden_project_testbed_runtime::ResolvePickingInputs(*m_world, attachment.targetEntity, inputs);
            renderer.visible = NWB::Core::ECSGraphics::ResolveAccessoryAttachmentTransform(
                *instance,
                inputs,
                attachment,
                transform
            );
        }
    );
}

void ProjectTestbed::clearSurfaceEditPreview(){
    m_surfaceEditSession = NWB::Core::ECSGraphics::DeformableSurfaceEditSession{};
    m_surfaceEditPreviewParams = NWB::Core::ECSGraphics::DeformableHoleEditParams{};
    m_surfaceEditPreview = NWB::Core::ECSGraphics::DeformableHolePreview{};
    m_surfaceEditPreviewActive = false;
}

void ProjectTestbed::clearPendingSurfaceEditAccessory(){
    m_pendingSurfaceEditRuntimeMesh = NWB::Core::ECSGraphics::RuntimeMeshHandle{};
    m_pendingSurfaceEditResult = NWB::Core::ECSGraphics::DeformableHoleEditResult{};
    m_pendingSurfaceEditRecord = NWB::Core::ECSGraphics::DeformableSurfaceEditRecord{};
    m_pendingSurfaceEditAccessory = false;
}

bool ProjectTestbed::refreshSurfaceEditPreview(){
    if(!m_surfaceEditPreviewActive)
        return false;

    auto* rendererSystem = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: renderer system is unavailable"));
        return false;
    }

    auto* instance = rendererSystem->findDeformableRuntimeMesh(m_surfaceEditSession.runtimeMesh);
    if(!instance){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview runtime mesh is unavailable"));
        return false;
    }

    m_surfaceEditPreviewParams.radius = m_surfaceEditRadius;
    m_surfaceEditPreviewParams.ellipseRatio = m_surfaceEditEllipseRatio;
    m_surfaceEditPreviewParams.depth = m_surfaceEditDepth;
    if(!NWB::Core::ECSGraphics::PreviewHole(
            *instance,
            m_surfaceEditSession,
            m_surfaceEditPreviewParams,
            m_surfaceEditPreview
        )
    ){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview expired"));
        return false;
    }

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: preview radius={} ellipse={} depth={} rev={}"),
        m_surfaceEditPreview.radius,
        m_surfaceEditPreview.ellipseRatio,
        m_surfaceEditPreview.depth,
        m_surfaceEditPreview.editRevision
    );
    return true;
}

void ProjectTestbed::previewSurfaceEditAtCursor(){
    if(m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: awaiting committed mesh upload before starting another edit"));
        return;
    }

    clearSurfaceEditPreview();

    auto* rendererSystem = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: renderer system is unavailable"));
        return;
    }

    NWB::Core::ECSGraphics::DeformablePickingRay ray;
    const NWB::ProjectFrameClientSize clientSize = NWB::QueryProjectFrameClientSize();
    const bool clientSizeValid = clientSize.width != 0u && clientSize.height != 0u;
    const f64 fallbackCursorX = static_cast<f64>(clientSizeValid ? clientSize.width : 1u) * 0.5;
    const f64 fallbackCursorY = static_cast<f64>(clientSizeValid ? clientSize.height : 1u) * 0.5;
    const f64 cursorX = clientSizeValid && m_cursorPositionValid ? m_cursorX : fallbackCursorX;
    const f64 cursorY = clientSizeValid && m_cursorPositionValid ? m_cursorY : fallbackCursorY;
    if(!__hidden_project_testbed_runtime::BuildEditorPickRay(*m_world, cursorX, cursorY, ray)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: could not build editor pick ray"));
        return;
    }

    NWB::Core::ECSGraphics::DeformablePosedHit hit;
    if(!NWB::Core::ECSGraphics::RaycastVisibleDeformableRenderers(*m_world, *rendererSystem, ray, hit)){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: no deformable surface under cursor"));
        return;
    }

    auto* instance = rendererSystem->findDeformableRuntimeMesh(hit.runtimeMesh);
    if(!instance){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: hit runtime mesh is unavailable"));
        return;
    }

    NWB::Core::ECSGraphics::DeformableHoleEditParams params;
    params.posedHit = hit;
    params.radius = m_surfaceEditRadius;
    params.ellipseRatio = m_surfaceEditEllipseRatio;
    params.depth = m_surfaceEditDepth;

    NWB::Core::ECSGraphics::DeformableSurfaceEditSession session;
    NWB::Core::ECSGraphics::DeformableHolePreview preview;
    if(!NWB::Core::ECSGraphics::BeginSurfaceEdit(*instance, hit, session)
        || !NWB::Core::ECSGraphics::PreviewHole(*instance, session, params, preview)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview failed for the selected deformable surface"));
        return;
    }

    m_surfaceEditSession = session;
    m_surfaceEditPreviewParams = params;
    m_surfaceEditPreview = preview;
    m_surfaceEditPreviewActive = true;
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: selected preview radius={} ellipse={} depth={} rev={}, press Enter to commit"),
        m_surfaceEditPreview.radius,
        m_surfaceEditPreview.ellipseRatio,
        m_surfaceEditPreview.depth,
        m_surfaceEditPreview.editRevision
    );
}

void ProjectTestbed::commitSurfaceEditPreview(){
    if(m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: awaiting committed mesh upload before another commit"));
        return;
    }

    if(!m_surfaceEditPreviewActive){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: click a deformable surface before committing"));
        return;
    }

    auto* rendererSystem = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: renderer system is unavailable"));
        return;
    }

    auto* instance = rendererSystem->findDeformableRuntimeMesh(m_surfaceEditSession.runtimeMesh);
    if(!instance){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview runtime mesh is unavailable"));
        return;
    }

    m_surfaceEditPreviewParams.radius = m_surfaceEditRadius;
    m_surfaceEditPreviewParams.ellipseRatio = m_surfaceEditEllipseRatio;
    m_surfaceEditPreviewParams.depth = m_surfaceEditDepth;
    NWB::Core::ECSGraphics::DeformableHoleEditResult result;
    NWB::Core::ECSGraphics::DeformableSurfaceEditRecord record;
    if(!NWB::Core::ECSGraphics::CommitHole(
            *instance,
            m_surfaceEditSession,
            m_surfaceEditPreviewParams,
            &result,
            &record
        )
    ){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: commit failed for the selected deformable surface"));
        return;
    }

    m_pendingSurfaceEditRuntimeMesh = m_surfaceEditSession.runtimeMesh;
    m_pendingSurfaceEditResult = result;
    m_pendingSurfaceEditRecord = record;
    m_pendingSurfaceEditAccessory = true;
    clearSurfaceEditPreview();
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: hole rev={} radius={} ellipse={} depth={}, awaiting mesh upload"),
        result.editRevision,
        record.hole.radius,
        record.hole.ellipseRatio,
        record.hole.depth
    );
}

void ProjectTestbed::attachPendingSurfaceEditAccessory(){
    if(!m_pendingSurfaceEditAccessory)
        return;

    auto* rendererSystem = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    if(!rendererSystem){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: renderer system is unavailable"));
        clearPendingSurfaceEditAccessory();
        return;
    }

    const auto* instance = rendererSystem->findDeformableRuntimeMesh(m_pendingSurfaceEditRuntimeMesh);
    if(!instance){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: committed runtime mesh is unavailable"));
        clearPendingSurfaceEditAccessory();
        return;
    }

    NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent attachment;
    if(!NWB::Core::ECSGraphics::AttachAccessory(
            *instance,
            m_pendingSurfaceEditResult,
            __hidden_project_testbed_runtime::s_AccessoryNormalOffset,
            __hidden_project_testbed_runtime::s_AccessoryUniformScale,
            attachment
        )
    ){
        if((instance->dirtyFlags & NWB::Core::ECSGraphics::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
            return;

        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: accessory attachment failed"));
        clearPendingSurfaceEditAccessory();
        return;
    }

    const __hidden_project_testbed_runtime::TestbedGeometryRef accessoryGeometry(Name("project/meshes/mock_earring"));
    const __hidden_project_testbed_runtime::TestbedMaterialRef accessoryMaterial(Name("project/materials/mat_test"));
    NWB::Core::ECSGraphics::DeformableAccessoryAttachmentRecord accessoryRecord;
    accessoryRecord.geometry = accessoryGeometry;
    accessoryRecord.material = accessoryMaterial;
    accessoryRecord.editRevision = attachment.editRevision;
    accessoryRecord.firstWallVertex = attachment.firstWallVertex;
    accessoryRecord.wallVertexCount = attachment.wallVertexCount;
    accessoryRecord.normalOffset = attachment.normalOffset;
    accessoryRecord.uniformScale = attachment.uniformScale;

    NWB::Core::ECSGraphics::DeformableSurfaceEditState candidateState = m_surfaceEditState;
    candidateState.edits.push_back(m_pendingSurfaceEditRecord);
    candidateState.accessories.clear();
    candidateState.accessories.push_back(accessoryRecord);

    NWB::Core::Assets::AssetBytes serializedState;
    NWB::Core::ECSGraphics::DeformableSurfaceEditState loadedState;
    if(!NWB::Core::ECSGraphics::SerializeSurfaceEditState(candidateState, serializedState)
        || !NWB::Core::ECSGraphics::DeserializeSurfaceEditState(serializedState, loadedState)
    ){
        clearPendingSurfaceEditAccessory();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: committed hole but persistence validation failed"));
        return;
    }

    const NWB::Core::ECS::EntityID accessoryEntity =
        __hidden_project_testbed_runtime::CreateAccessoryRendererEntity(
            *m_world,
            accessoryGeometry,
            accessoryMaterial
        )
    ;

    auto* attachmentComponent =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent>(accessoryEntity)
    ;
    if(!attachmentComponent){
        clearPendingSurfaceEditAccessory();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: accessory entity is missing its attachment component"));
        return;
    }
    *attachmentComponent = attachment;
    m_surfaceEditState = Move(loadedState);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: committed hole rev={} radius={} ellipse={} depth={} accessory={} persisted={} bytes"),
        m_pendingSurfaceEditResult.editRevision,
        m_pendingSurfaceEditRecord.hole.radius,
        m_pendingSurfaceEditRecord.hole.ellipseRatio,
        m_pendingSurfaceEditRecord.hole.depth,
        accessoryEntity.id,
        serializedState.size()
    );
    clearPendingSurfaceEditAccessory();
}

void ProjectTestbed::cancelSurfaceEditPreview(){
    if(!m_surfaceEditPreviewActive)
        return;

    clearSurfaceEditPreview();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: preview cancelled"));
}

void ProjectTestbed::logSurfaceEditControls()const{
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: left click preview, [/] radius={}, comma/period ellipse={}, -/= depth={}, Enter commit, Esc cancel"),
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth
    );
}

bool ProjectTestbed::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    (void)scancode;
    (void)mods;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        setKeyState(key, true);
    else if(action == NWB::Core::InputAction::Release)
        setKeyState(key, false);

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat){
        if(key == NWB::Core::Key::LeftBracket || key == NWB::Core::Key::RightBracket){
            const f32 delta = key == NWB::Core::Key::RightBracket ? 0.02f : -0.02f;
            m_surfaceEditRadius = __hidden_project_testbed_runtime::ClampSurfaceEditRadius(
                m_surfaceEditRadius + delta
            );
            if(!refreshSurfaceEditPreview())
                logSurfaceEditControls();
        }
        else if(key == NWB::Core::Key::Comma || key == NWB::Core::Key::Period){
            const f32 delta = key == NWB::Core::Key::Period ? 0.05f : -0.05f;
            m_surfaceEditEllipseRatio = __hidden_project_testbed_runtime::ClampSurfaceEditEllipseRatio(
                m_surfaceEditEllipseRatio + delta
            );
            if(!refreshSurfaceEditPreview())
                logSurfaceEditControls();
        }
        else if(key == NWB::Core::Key::Minus || key == NWB::Core::Key::Equal){
            const f32 delta = key == NWB::Core::Key::Equal ? 0.02f : -0.02f;
            m_surfaceEditDepth = __hidden_project_testbed_runtime::ClampSurfaceEditDepth(m_surfaceEditDepth + delta);
            if(!refreshSurfaceEditPreview())
                logSurfaceEditControls();
        }
        else if(key == NWB::Core::Key::Enter || key == NWB::Core::Key::KeypadEnter){
            commitSurfaceEditPreview();
        }
        else if(key == NWB::Core::Key::Escape){
            cancelSurfaceEditPreview();
        }
    }

    return false;
}

bool ProjectTestbed::mousePosUpdate(const f64 xpos, const f64 ypos){
    m_cursorX = xpos;
    m_cursorY = ypos;
    m_cursorPositionValid = true;

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

    if(button == NWB::Core::MouseButton::Left){
        if(action == NWB::Core::InputAction::Press)
            previewSurfaceEditAtCursor();
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

