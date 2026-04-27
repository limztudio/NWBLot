// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <global/simplemath.h>
#include <logger/client/logger.h>


namespace __hidden_project_testbed_runtime{


using TestbedGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::Geometry>;
using TestbedDeformableGeometryRef = NWB::Core::Assets::AssetRef<NWB::Impl::DeformableGeometry>;
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
static constexpr f32 s_DeformableSkinPivotY = -0.5f;
static constexpr f32 s_DeformableSkinMaxAngle = 0.45f;
static constexpr f32 s_AccessoryNormalOffset = 0.08f;
static constexpr f32 s_AccessoryUniformScale = 0.16f;
static constexpr AStringView s_DeformableProxyPath = "project/characters/proxy_deformable";
static constexpr AStringView s_DeformableImportedPath = "project/characters/imported_deformable";
static constexpr AStringView s_DeformableMaterialPath = "project/materials/mat_deformable_uv";
static constexpr AStringView s_AccessoryGeometryPath = "project/meshes/mock_earring";
static constexpr AStringView s_AccessoryMaterialPath = "project/materials/mat_accessory_gold";


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

[[nodiscard]] static const tchar* SurfaceEditPermissionText(
    const NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Enum permission)
{
    switch(permission){
    case NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Restricted:
        return NWB_TEXT("restricted");
    case NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Forbidden:
        return NWB_TEXT("forbidden");
    case NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Allowed:
    default:
        return NWB_TEXT("allowed");
    }
}

[[nodiscard]] static bool DeformableDisplacementModeUsesTexture(const u32 mode){
    return mode == NWB::Impl::DeformableDisplacementMode::ScalarTexture
        || mode == NWB::Impl::DeformableDisplacementMode::VectorTangentTexture
        || mode == NWB::Impl::DeformableDisplacementMode::VectorObjectTexture
    ;
}

[[nodiscard]] static const NWB::Impl::DeformableDisplacementTexture* ResolveSurfaceEditDebugDisplacementTexture(
    const NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance& instance,
    NWB::Core::Assets::AssetManager& assetManager,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset)
{
    outLoadedAsset.reset();
    const NWB::Impl::DeformableDisplacement& displacement = instance.displacement;
    if(!DeformableDisplacementModeUsesTexture(displacement.mode) || !displacement.texture.valid())
        return nullptr;

    if(!assetManager.loadSync(
            NWB::Impl::DeformableDisplacementTexture::AssetTypeName(),
            displacement.texture.name(),
            outLoadedAsset
        )
        || !outLoadedAsset
        || outLoadedAsset->assetType() != NWB::Impl::DeformableDisplacementTexture::AssetTypeName()
    )
        return nullptr;

    const auto* texture = static_cast<const NWB::Impl::DeformableDisplacementTexture*>(outLoadedAsset.get());
    return texture->virtualPath() == displacement.texture.name() && texture->validatePayload()
        ? texture
        : nullptr
    ;
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

[[nodiscard]] static bool FiniteFloat3(const Float4& value){
    return FiniteVector3(LoadFloat(value));
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

[[nodiscard]] static EditorVec3 NormalizeVec3(const EditorVec3& value, const EditorVec3& fallback){
    return NormalizeVec3(LoadFloat(static_cast<const Float4&>(value)), fallback);
}

static void ResolveFlyCameraAnglesFromTransform(
    const NWB::Core::Scene::TransformComponent& transform,
    f32& outYawRadians,
    f32& outPitchRadians)
{
    const Float4 localForward(0.0f, 0.0f, 1.0f);
    const EditorVec3 forward = NormalizeVec3(
        Vector3Rotate(LoadFloat(localForward), LoadFloat(transform.rotation)),
        EditorVec3{ 0.0f, 0.0f, 1.0f }
    );
    const f32 clampedForwardY = Min(Max(forward.y, -1.0f), 1.0f);
    const f32 yawRadians = ATan2(forward.x, forward.z);
    const f32 pitchRadians = -ASin(clampedForwardY);

    outYawRadians = IsFinite(yawRadians) ? yawRadians : 0.0f;
    outPitchRadians = IsFinite(pitchRadians)
        ? ClampPitch(pitchRadians, s_FlyCameraPitchLimitRadians)
        : 0.0f
    ;
}

[[nodiscard]] static bool BuildEditorPickRay(
    NWB::Core::ECS::World& world,
    const f64 cursorX,
    const f64 cursorY,
    NWB::Core::ECSGraphics::DeformablePickingRay& outRay){
    const NWB::ProjectFrameClientSize clientSize = NWB::QueryProjectFrameClientSize();
    if(clientSize.width == 0u || clientSize.height == 0u || !IsFinite(cursorX) || !IsFinite(cursorY))
        return false;

    const f32 width = static_cast<f32>(clientSize.width);
    const f32 height = static_cast<f32>(clientSize.height);
    const f32 framebufferAspect = width / height;
    const NWB::Core::Scene::SceneCameraView cameraView =
        NWB::Core::Scene::ResolveSceneCameraView(world, framebufferAspect)
    ;
    if(!cameraView.valid())
        return false;

    const f32 cursorXF32 = static_cast<f32>(cursorX);
    const f32 cursorYF32 = static_cast<f32>(cursorY);
    if(!IsFinite(cursorXF32) || !IsFinite(cursorYF32))
        return false;

    const f32 ndcX = (2.0f * cursorXF32 / width) - 1.0f;
    const f32 ndcY = 1.0f - (2.0f * cursorYF32 / height);
    if(!IsFinite(ndcX) || !IsFinite(ndcY))
        return false;

    const NWB::Core::Scene::CameraProjectionData& projectionData = cameraView.projectionData;
    const f32 horizontalScale = projectionData.tanHalfVerticalFov * projectionData.aspectRatio;
    const f32 localX = ndcX * horizontalScale;
    const f32 localY = ndcY * projectionData.tanHalfVerticalFov;
    if(!IsFinite(horizontalScale) || !IsFinite(localX) || !IsFinite(localY))
        return false;

    EditorVec3 localDirection{
        localX,
        localY,
        1.0f
    };
    localDirection = NormalizeVec3(localDirection, EditorVec3{ 0.0f, 0.0f, 1.0f });
    const SIMDVector worldDirectionVector = Vector3Rotate(
        LoadFloat(static_cast<const Float4&>(localDirection)),
        LoadFloat(cameraView.transform->rotation)
    );
    const EditorVec3 worldDirection = NormalizeVec3(
        worldDirectionVector,
        EditorVec3{ 0.0f, 0.0f, 1.0f }
    );

    outRay.setOrigin(Float3U(
        cameraView.transform->position.x,
        cameraView.transform->position.y,
        cameraView.transform->position.z
    ));
    outRay.setDirection(Float3U(worldDirection.x, worldDirection.y, worldDirection.z));
    outRay.setMinDistance(cameraView.camera->nearPlane());
    outRay.setMaxDistance(cameraView.camera->farPlane());
    return true;
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
    if(!FiniteFloat3(transform.position))
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

        const Float4 localMove(safeRightAxis * moveScale, 0.0f, safeForwardAxis * moveScale);
        const SIMDVector worldMove = Vector3Rotate(LoadFloat(localMove), rotation);
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

[[nodiscard]] static NWB::Core::ECS::EntityID CreateMainCameraEntity(NWB::Core::ECS::World& world){
    auto cameraEntity = world.createEntity();
    auto& transform = cameraEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.position = Float4(0.0f, 0.0f, -s_CameraStartDepth);
    cameraEntity.addComponent<NWB::Core::Scene::CameraComponent>();
    return cameraEntity.id();
}

static void CreateDefaultDirectionalLightEntity(NWB::Core::ECS::World& world){
    auto lightEntity = world.createEntity();
    auto& transform = lightEntity.addComponent<NWB::Core::Scene::TransformComponent>();
    StoreFloat(
        QuaternionRotationRollPitchYaw(s_DefaultDirectionalLightPitch, s_DefaultDirectionalLightYaw, 0.0f),
        &transform.rotation
    );

    auto& light = lightEntity.addComponent<NWB::Core::Scene::LightComponent>();
    light.type = NWB::Core::Scene::LightType::Directional;
    light.setColor(Float4(1.0f, 0.96f, 0.88f));
    light.setIntensity(s_DefaultDirectionalLightIntensity);
}

static void CreateRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedGeometryRef& geometry,
    const TestbedMaterialRef& material,
    const Float4& position,
    const f32 uniformScale
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = Float4(uniformScale, uniformScale, uniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSGraphics::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
}

[[nodiscard]] static NWB::Core::ECSGraphics::DeformableJointMatrix BuildProxySkinJoint(const f32 angleRadians){
    const f32 safeAngleRadians = IsFinite(angleRadians) ? angleRadians : 0.0f;
    SIMDVector sinAngleVector;
    SIMDVector cosAngleVector;
    VectorSinCos(&sinAngleVector, &cosAngleVector, VectorReplicate(safeAngleRadians));
    f32 sinAngle = VectorGetX(sinAngleVector);
    f32 cosAngle = VectorGetX(cosAngleVector);
    if(!IsFinite(sinAngle) || !IsFinite(cosAngle)){
        sinAngle = 0.0f;
        cosAngle = 1.0f;
    }

    NWB::Core::ECSGraphics::DeformableJointMatrix joint;
    joint.column0 = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joint.column1 = Float4(0.0f, cosAngle, sinAngle, 0.0f);
    joint.column2 = Float4(0.0f, -sinAngle, cosAngle, 0.0f);
    joint.column3 = Float4(
        0.0f,
        s_DeformableSkinPivotY * (1.0f - cosAngle),
        -s_DeformableSkinPivotY * sinAngle,
        1.0f
    );
    return joint;
}

static void UpdateProxySkinPalette(
    NWB::Core::ECSGraphics::DeformableJointPaletteComponent& jointPalette,
    const f32 timeSeconds
){
    const f32 safeTimeSeconds = IsFinite(timeSeconds) ? timeSeconds : 0.0f;

    jointPalette.joints.resize(2u);
    jointPalette.joints[0] = NWB::Core::ECSGraphics::DeformableJointMatrix{};
    const f32 skinAngle = VectorGetX(VectorSin(VectorReplicate(safeTimeSeconds * 0.9f)));
    jointPalette.joints[1] = BuildProxySkinJoint(s_DeformableSkinMaxAngle * skinAngle);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateDeformableRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedDeformableGeometryRef& geometry,
    const TestbedMaterialRef& material,
    const Float4& position,
    const f32 uniformScale
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = Float4(uniformScale, uniformScale, uniformScale);

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
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.scale = Float4(s_AccessoryUniformScale, s_AccessoryUniformScale, s_AccessoryUniformScale);

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
    outInputs.transform = world.tryGetComponent<NWB::Core::Scene::TransformComponent>(entity);
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

NWB::Core::ECSGraphics::RendererSystem& ProjectTestbed::rendererSystem(){
    auto* system = m_world->getSystem<NWB::Core::ECSGraphics::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(system, NWB_TEXT("ProjectTestbed runtime invariant failed: renderer system is missing"));
    return *system;
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

    auto sceneEntity = m_world->createEntity();
    auto& scene = sceneEntity.addComponent<NWB::Core::Scene::SceneComponent>();
    scene.mainCamera = __hidden_project_testbed_runtime::CreateMainCameraEntity(*m_world);
    __hidden_project_testbed_runtime::CreateDefaultDirectionalLightEntity(*m_world);

    TestbedMaterialRef cubeWarmMaterial;
    cubeWarmMaterial.virtualPath = Name("project/materials/mat_cube_warm");
    TestbedMaterialRef cubeCoolMaterial;
    cubeCoolMaterial.virtualPath = Name("project/materials/mat_cube_cool");
    TestbedMaterialRef transparentSphereMaterial;
    transparentSphereMaterial.virtualPath = Name("project/materials/mat_transparent_sphere");
    TestbedMaterialRef transparentTetrahedronMaterial;
    transparentTetrahedronMaterial.virtualPath = Name("project/materials/mat_transparent_tetrahedron");
    TestbedMaterialRef deformableUvMaterial;
    deformableUvMaterial.virtualPath = Name(__hidden_project_testbed_runtime::s_DeformableMaterialPath);

    TestbedGeometryRef cubeGeometry;
    cubeGeometry.virtualPath = Name("project/meshes/cube");
    TestbedGeometryRef sphereGeometry;
    sphereGeometry.virtualPath = Name("project/meshes/sphere");
    TestbedGeometryRef tetrahedronGeometry;
    tetrahedronGeometry.virtualPath = Name("project/meshes/tetrahedron");
    TestbedDeformableGeometryRef deformableProxyGeometry;
    deformableProxyGeometry.virtualPath = Name(__hidden_project_testbed_runtime::s_DeformableProxyPath);
    TestbedDeformableGeometryRef importedDeformableGeometry;
    importedDeformableGeometry.virtualPath = Name(__hidden_project_testbed_runtime::s_DeformableImportedPath);

    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        cubeGeometry,
        cubeWarmMaterial,
        Float4(-0.55f, 0.0f, 0.0f),
        0.65f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        cubeGeometry,
        cubeCoolMaterial,
        Float4(0.55f, 0.0f, 0.0f),
        0.9f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        sphereGeometry,
        transparentSphereMaterial,
        Float4(1.45f, 0.0f, 0.0f),
        0.75f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        tetrahedronGeometry,
        transparentTetrahedronMaterial,
        Float4(-1.45f, 0.0f, 0.0f),
        0.8f
    );
    m_deformableMorphEntity = __hidden_project_testbed_runtime::CreateDeformableRendererEntity(
        *m_world,
        deformableProxyGeometry,
        deformableUvMaterial,
        Float4(0.0f, 0.85f, 0.0f),
        0.8f
    );
    const NWB::Core::ECS::EntityID importedDeformableEntity = __hidden_project_testbed_runtime::CreateDeformableRendererEntity(
        *m_world,
        importedDeformableGeometry,
        deformableUvMaterial,
        Float4(0.0f, -0.85f, 0.0f),
        0.7f
    );
    if(auto* morphWeights =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableMorphWeightsComponent>(importedDeformableEntity)
    ){
        if(!morphWeights->weights.empty())
            morphWeights->weights[0].weight = 0.65f;
    }
    if(auto* jointPalette =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableJointPaletteComponent>(importedDeformableEntity)
    ){
        __hidden_project_testbed_runtime::UpdateProxySkinPalette(*jointPalette, 1.0f);
    }

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("directional light, shared primitives, animated proxy, mock native .nwb deformable")
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
    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;

    updateMainCamera(safeDelta);
    updateDeformableMorph(safeDelta);
    m_world->tick(safeDelta);
    applyPendingSurfaceEditReplay();
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
    const bool boosted = keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift);

    __hidden_project_testbed_runtime::ApplyFlyCameraInputToMainCamera(
        *m_world,
        rightAxis,
        forwardAxis,
        boosted,
        mouseDeltaX,
        mouseDeltaY,
        delta
    );
}

void ProjectTestbed::updateDeformableMorph(const f32 delta){
    if(!m_deformableMorphEntity.valid())
        return;

    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
    if(!IsFinite(m_deformableMorphTime))
        m_deformableMorphTime = 0.0f;

    m_deformableMorphTime += safeDelta;
    if(!IsFinite(m_deformableMorphTime))
        m_deformableMorphTime = 0.0f;

    auto* morphWeights =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableMorphWeightsComponent>(m_deformableMorphEntity)
    ;
    if(morphWeights && !morphWeights->weights.empty()){
        const f32 morphWeight =
            0.5f + 0.5f * VectorGetX(VectorSin(VectorReplicate(m_deformableMorphTime * 1.35f)))
        ;
        morphWeights->weights[0].weight = IsFinite(morphWeight) ? morphWeight : 0.5f;
    }

    auto* jointPalette =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableJointPaletteComponent>(m_deformableMorphEntity)
    ;
    if(jointPalette)
        __hidden_project_testbed_runtime::UpdateProxySkinPalette(*jointPalette, m_deformableMorphTime);

    auto* displacement =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableDisplacementComponent>(m_deformableMorphEntity)
    ;
    if(displacement){
        if(!IsFinite(m_deformableDisplacementScale))
            m_deformableDisplacementScale = 1.0f;

        const f32 displacementAxis = __hidden_project_testbed_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::Z),
            keyPressed(NWB::Core::Key::X)
        );
        if(displacementAxis != 0.0f){
            const f32 displacementScale = m_deformableDisplacementScale + displacementAxis * safeDelta;
            if(IsFinite(displacementScale))
                m_deformableDisplacementScale = Max(0.0f, displacementScale);
        }

        displacement->enabled = !keyPressed(NWB::Core::Key::C);
        displacement->amplitudeScale = m_deformableDisplacementScale;
    }
}

void ProjectTestbed::updateSurfaceEditAccessories(){
    auto& renderSystem = rendererSystem();

    m_world->view<
        NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent,
        NWB::Core::Scene::TransformComponent,
        NWB::Core::ECSGraphics::RendererComponent
    >().each(
        [&](NWB::Core::ECS::EntityID entity,
            NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent& attachment,
            NWB::Core::Scene::TransformComponent& transform,
            NWB::Core::ECSGraphics::RendererComponent& renderer)
        {
            static_cast<void>(entity);
            const auto* instance = renderSystem.findDeformableRuntimeMesh(attachment.runtimeMesh);
            if(!instance){
                renderer.visible = false;
                return;
            }
            const auto* targetRenderer =
                m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableRendererComponent>(attachment.targetEntity)
            ;
            if(!targetRenderer || !targetRenderer->visible){
                renderer.visible = false;
                return;
            }

            NWB::Core::ECSGraphics::DeformablePickingInputs inputs;
            __hidden_project_testbed_runtime::ResolvePickingInputs(*m_world, attachment.targetEntity, inputs);
            inputs.assetManager = &m_context.assetManager;
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

    auto* instance = rendererSystem().findDeformableRuntimeMesh(m_surfaceEditSession.runtimeMesh);
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
        NWB_TEXT("Surface edit: preview radius={} ellipse={} depth={} rev={} permission={}"),
        m_surfaceEditPreview.radius,
        m_surfaceEditPreview.ellipseRatio,
        m_surfaceEditPreview.depth,
        m_surfaceEditPreview.editRevision,
        __hidden_project_testbed_runtime::SurfaceEditPermissionText(m_surfaceEditPreview.editPermission)
    );
    return true;
}

void ProjectTestbed::previewSurfaceEditAtCursor(){
    if(m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: awaiting committed mesh upload before starting another edit"));
        return;
    }

    clearSurfaceEditPreview();

    auto& renderSystem = rendererSystem();

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
    if(!NWB::Core::ECSGraphics::RaycastVisibleDeformableRenderers(
            *m_world,
            renderSystem,
            ray,
            hit,
            &m_context.assetManager
        )
    ){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: no deformable surface under cursor"));
        return;
    }

    auto* instance = renderSystem.findDeformableRuntimeMesh(hit.runtimeMesh);
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
    m_surfaceEditDebugRuntimeMesh = session.runtimeMesh;
    m_surfaceEditPreviewActive = true;
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: selected preview radius={} ellipse={} depth={} rev={} permission={}, press Enter to commit"),
        m_surfaceEditPreview.radius,
        m_surfaceEditPreview.ellipseRatio,
        m_surfaceEditPreview.depth,
        m_surfaceEditPreview.editRevision,
        __hidden_project_testbed_runtime::SurfaceEditPermissionText(m_surfaceEditPreview.editPermission)
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

    auto* instance = rendererSystem().findDeformableRuntimeMesh(m_surfaceEditSession.runtimeMesh);
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
        const NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Enum permission =
            m_surfaceEditPreview.editPermission
        ;
        clearSurfaceEditPreview();
        if(permission == NWB::Core::ECSGraphics::DeformableSurfaceEditPermission::Forbidden){
            NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: commit refused by the deformable edit mask"));
        }
        else{
            NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: commit failed for the selected deformable surface"));
        }
        return;
    }

    m_pendingSurfaceEditRuntimeMesh = m_surfaceEditSession.runtimeMesh;
    m_surfaceEditDebugRuntimeMesh = m_surfaceEditSession.runtimeMesh;
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

    const auto* instance = rendererSystem().findDeformableRuntimeMesh(m_pendingSurfaceEditRuntimeMesh);
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

    __hidden_project_testbed_runtime::TestbedGeometryRef accessoryGeometry;
    accessoryGeometry.virtualPath = Name(__hidden_project_testbed_runtime::s_AccessoryGeometryPath);
    __hidden_project_testbed_runtime::TestbedMaterialRef accessoryMaterial;
    accessoryMaterial.virtualPath = Name(__hidden_project_testbed_runtime::s_AccessoryMaterialPath);
    NWB::Core::ECSGraphics::DeformableAccessoryAttachmentRecord accessoryRecord;
    accessoryRecord.geometry = accessoryGeometry;
    accessoryRecord.material = accessoryMaterial;
    if(!accessoryRecord.geometryVirtualPathText.assign(__hidden_project_testbed_runtime::s_AccessoryGeometryPath)
        || !accessoryRecord.materialVirtualPathText.assign(__hidden_project_testbed_runtime::s_AccessoryMaterialPath)
    ){
        clearPendingSurfaceEditAccessory();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: accessory virtual paths are too long to persist"));
        return;
    }
    accessoryRecord.anchorEditId = m_pendingSurfaceEditRecord.editId;
    accessoryRecord.firstWallVertex = attachment.firstWallVertex;
    accessoryRecord.wallVertexCount = attachment.wallVertexCount;
    accessoryRecord.normalOffset = attachment.normalOffset();
    accessoryRecord.uniformScale = attachment.uniformScale();
    accessoryRecord.wallLoopParameter = attachment.wallLoopParameter();

    NWB::Core::ECSGraphics::DeformableSurfaceEditState candidateState = m_surfaceEditState;
    candidateState.edits.push_back(m_pendingSurfaceEditRecord);
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
    m_surfaceEditHistory.redoStack.clear();

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

void ProjectTestbed::queueSurfaceEditReplay(){
    if(m_pendingSurfaceEditReplay){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit replay: already waiting for a clean runtime mesh"));
        return;
    }
    if(m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit replay: awaiting committed accessory before replay"));
        return;
    }
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit replay: no saved edits to replay"));
        return;
    }

    NWB::Core::Assets::AssetBytes serializedState;
    NWB::Core::ECSGraphics::DeformableSurfaceEditState loadedState;
    if(!NWB::Core::ECSGraphics::SerializeSurfaceEditState(m_surfaceEditState, serializedState)
        || !NWB::Core::ECSGraphics::DeserializeSurfaceEditState(serializedState, loadedState)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit replay: save/load validation failed"));
        return;
    }

    Float4 replayPosition(0.0f, 0.85f, 0.0f);
    f32 replayScale = 0.8f;
    if(const auto* oldTransform = m_world->tryGetComponent<NWB::Core::Scene::TransformComponent>(m_deformableMorphEntity)){
        replayPosition = oldTransform->position;
        replayScale = oldTransform->scale.x;
    }
    if(auto* oldRenderer =
        m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableRendererComponent>(m_deformableMorphEntity)
    )
        oldRenderer->visible = false;

    __hidden_project_testbed_runtime::TestbedDeformableGeometryRef deformableProxyGeometry;
    deformableProxyGeometry.virtualPath = Name(__hidden_project_testbed_runtime::s_DeformableProxyPath);
    __hidden_project_testbed_runtime::TestbedMaterialRef deformableUvMaterial;
    deformableUvMaterial.virtualPath = Name(__hidden_project_testbed_runtime::s_DeformableMaterialPath);
    m_deformableMorphEntity = __hidden_project_testbed_runtime::CreateDeformableRendererEntity(
        *m_world,
        deformableProxyGeometry,
        deformableUvMaterial,
        replayPosition,
        replayScale
    );
    m_surfaceEditState = Move(loadedState);
    m_surfaceEditHistory.redoStack.clear();
    m_pendingSurfaceEditReplay = true;
    clearSurfaceEditPreview();
    clearPendingSurfaceEditAccessory();
    m_surfaceEditDebugRuntimeMesh.reset();
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit replay: queued {} edits and {} accessories ({} bytes)"),
        m_surfaceEditState.edits.size(),
        m_surfaceEditState.accessories.size(),
        serializedState.size()
    );
}

void ProjectTestbed::applyPendingSurfaceEditReplay(){
    if(!m_pendingSurfaceEditReplay)
        return;

    const NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh =
        rendererSystem().deformableRuntimeMeshHandle(m_deformableMorphEntity)
    ;
    auto* instance = rendererSystem().findDeformableRuntimeMesh(runtimeMesh);
    if(!runtimeMesh.valid() || !instance)
        return;

    NWB::Core::ECSGraphics::DeformableSurfaceEditReplayContext replayContext;
    replayContext.assetManager = &m_context.assetManager;
    replayContext.world = m_world.get();
    replayContext.targetEntity = m_deformableMorphEntity;

    NWB::Core::ECSGraphics::DeformableSurfaceEditReplayResult replayResult;
    if(!NWB::Core::ECSGraphics::ApplySurfaceEditState(
            *instance,
            m_surfaceEditState,
            replayContext,
            &replayResult
        )
    ){
        m_pendingSurfaceEditReplay = false;
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit replay: failed to apply saved state to clean runtime mesh"));
        return;
    }

    m_surfaceEditDebugRuntimeMesh = runtimeMesh;
    m_pendingSurfaceEditReplay = false;
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit replay: applied {} edits, restored {} accessories, revision={}"),
        replayResult.appliedEditCount,
        replayResult.restoredAccessoryCount,
        replayResult.finalEditRevision
    );
}

bool ProjectTestbed::buildSurfaceEditCleanBase(
    const NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance& instance,
    NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance& outCleanBase)const
{
    outCleanBase = NWB::Core::ECSGraphics::DeformableRuntimeMeshInstance{};
    const Name sourceName = instance.source.name();
    if(!sourceName)
        return false;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(!m_context.assetManager.loadSync(
            NWB::Impl::DeformableGeometry::AssetTypeName(),
            sourceName,
            loadedAsset
        )
        || !loadedAsset
        || loadedAsset->assetType() != NWB::Impl::DeformableGeometry::AssetTypeName()
    )
        return false;

    const auto& geometry = static_cast<const NWB::Impl::DeformableGeometry&>(*loadedAsset);
    outCleanBase.entity = instance.entity;
    outCleanBase.handle = instance.handle;
    outCleanBase.source = instance.source;
    outCleanBase.restVertices = geometry.restVertices();
    outCleanBase.indices = geometry.indices();
    outCleanBase.sourceTriangleCount = static_cast<u32>(geometry.indices().size() / 3u);
    outCleanBase.skeletonJointCount = geometry.skeletonJointCount();
    outCleanBase.skin = geometry.skin();
    outCleanBase.inverseBindMatrices = geometry.inverseBindMatrices();
    outCleanBase.sourceSamples = geometry.sourceSamples();
    outCleanBase.editMaskPerTriangle = geometry.editMaskPerTriangle();
    outCleanBase.displacement = geometry.displacement();
    outCleanBase.morphs = geometry.morphs();
    outCleanBase.editRevision = 0u;
    outCleanBase.dirtyFlags = NWB::Core::ECSGraphics::RuntimeMeshDirtyFlag::All;
    return true;
}

void ProjectTestbed::hideSurfaceEditAccessoriesForTarget(const NWB::Core::ECS::EntityID targetEntity){
    auto accessoryView = m_world->view<
        NWB::Core::ECSGraphics::RendererComponent,
        NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent
    >();
    for(auto&& [entity, renderer, attachment] : accessoryView){
        static_cast<void>(entity);
        if(attachment.targetEntity == targetEntity){
            renderer.visible = false;
            attachment.targetEntity = NWB::Core::ECS::ENTITY_ID_INVALID;
            attachment.runtimeMesh.reset();
        }
    }
}

bool ProjectTestbed::restoreSurfaceEditAccessoryEntities(){
    const NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh =
        rendererSystem().deformableRuntimeMeshHandle(m_deformableMorphEntity)
    ;
    if(!runtimeMesh.valid())
        return false;

    hideSurfaceEditAccessoriesForTarget(m_deformableMorphEntity);
    for(const auto& accessory : m_surfaceEditState.accessories){
        if(!accessory.geometry.valid() || !accessory.material.valid())
            return false;

        const NWB::Core::ECS::EntityID accessoryEntity =
            __hidden_project_testbed_runtime::CreateAccessoryRendererEntity(
                *m_world,
                accessory.geometry,
                accessory.material
            )
        ;
        auto* attachment =
            m_world->tryGetComponent<NWB::Core::ECSGraphics::DeformableAccessoryAttachmentComponent>(accessoryEntity)
        ;
        if(!attachment)
            return false;

        attachment->targetEntity = m_deformableMorphEntity;
        attachment->runtimeMesh = runtimeMesh;
        attachment->anchorEditId = accessory.anchorEditId;
        attachment->firstWallVertex = accessory.firstWallVertex;
        attachment->wallVertexCount = accessory.wallVertexCount;
        attachment->setNormalOffset(accessory.normalOffset);
        attachment->setUniformScale(accessory.uniformScale);
        attachment->setWallLoopParameter(accessory.wallLoopParameter);
    }
    return true;
}

bool ProjectTestbed::prepareSurfaceEditMutation(
    const tchar* action,
    SurfaceEditMutationContext& outContext)
{
    outContext = SurfaceEditMutationContext{};
    if(m_pendingSurfaceEditReplay || m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit {}: waiting for pending replay/accessory work"), action);
        return false;
    }

    auto& renderSystem = rendererSystem();
    outContext.rendererSystem = &renderSystem;
    outContext.runtimeMesh = renderSystem.deformableRuntimeMeshHandle(m_deformableMorphEntity);
    outContext.instance = renderSystem.findDeformableRuntimeMesh(outContext.runtimeMesh);
    if(!outContext.runtimeMesh.valid() || !outContext.instance){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: active runtime mesh is unavailable"), action);
        return false;
    }

    if(!buildSurfaceEditCleanBase(*outContext.instance, outContext.cleanBase)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: failed to load clean source mesh"), action);
        return false;
    }
    return true;
}

void ProjectTestbed::finishSurfaceEditMutation(
    const tchar* action,
    const NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh,
    const bool clearRedo)
{
    clearSurfaceEditPreview();
    clearPendingSurfaceEditAccessory();
    if(clearRedo)
        m_surfaceEditHistory.redoStack.clear();
    m_surfaceEditDebugRuntimeMesh = runtimeMesh;
    if(!restoreSurfaceEditAccessoryEntities())
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: failed to restore accessory entities"), action);
}

bool ProjectTestbed::pickSurfaceEditMutationTarget(
    const tchar* action,
    const SurfaceEditMutationContext& editContext,
    NWB::Core::ECSGraphics::DeformablePosedHit& outTargetHit)
{
    outTargetHit = NWB::Core::ECSGraphics::DeformablePosedHit{};

    NWB::Core::ECSGraphics::DeformablePickingRay ray;
    const NWB::ProjectFrameClientSize clientSize = NWB::QueryProjectFrameClientSize();
    const bool clientSizeValid = clientSize.width != 0u && clientSize.height != 0u;
    const f64 fallbackCursorX = static_cast<f64>(clientSizeValid ? clientSize.width : 1u) * 0.5;
    const f64 fallbackCursorY = static_cast<f64>(clientSizeValid ? clientSize.height : 1u) * 0.5;
    const f64 cursorX = clientSizeValid && m_cursorPositionValid ? m_cursorX : fallbackCursorX;
    const f64 cursorY = clientSizeValid && m_cursorPositionValid ? m_cursorY : fallbackCursorY;
    if(!__hidden_project_testbed_runtime::BuildEditorPickRay(*m_world, cursorX, cursorY, ray)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: could not build editor pick ray"), action);
        return false;
    }

    if(!NWB::Core::ECSGraphics::RaycastVisibleDeformableRenderers(
            *m_world,
            *editContext.rendererSystem,
            ray,
            outTargetHit,
            &m_context.assetManager
        )
    ){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit {}: no deformable surface under cursor"), action);
        return false;
    }
    if(outTargetHit.runtimeMesh != editContext.runtimeMesh){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: cursor hit is not on the active edited mesh"), action);
        return false;
    }
    return true;
}

void ProjectTestbed::undoSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit undo: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("undo"), editContext))
        return;

    NWB::Core::ECSGraphics::DeformableSurfaceEditUndoResult undoResult;
    if(!NWB::Core::ECSGraphics::UndoLastSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            &undoResult,
            &m_surfaceEditHistory
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit undo: failed to replay state without the latest edit"));
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("undo"), editContext.runtimeMesh, false);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit undo: edit={} removed_accessories={} remaining_edits={} revision={}"),
        undoResult.undoneEditId,
        undoResult.removedAccessoryCount,
        m_surfaceEditState.edits.size(),
        undoResult.replay.finalEditRevision
    );
}

void ProjectTestbed::redoSurfaceEdit(){
    if(m_surfaceEditHistory.redoStack.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit redo: no edit history"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("redo"), editContext))
        return;

    NWB::Core::ECSGraphics::DeformableSurfaceEditRedoResult redoResult;
    if(!NWB::Core::ECSGraphics::RedoLastSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            m_surfaceEditHistory,
            &redoResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit redo: failed to replay redo state"));
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("redo"), editContext.runtimeMesh, false);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit redo: edit={} restored_accessories={} edits={} revision={}"),
        redoResult.redoneEditId,
        redoResult.restoredAccessoryCount,
        m_surfaceEditState.edits.size(),
        redoResult.replay.finalEditRevision
    );
}

void ProjectTestbed::healLatestSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit heal: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("heal"), editContext))
        return;

    const NWB::Core::ECSGraphics::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSGraphics::DeformableSurfaceEditHealResult healResult;
    if(!NWB::Core::ECSGraphics::HealSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            editId,
            &healResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit heal: failed to replay state without edit {}"), editId);
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("heal"), editContext.runtimeMesh, true);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit heal: edit={} removed_accessories={} remaining_edits={} revision={}"),
        healResult.healedEditId,
        healResult.removedAccessoryCount,
        m_surfaceEditState.edits.size(),
        healResult.replay.finalEditRevision
    );
}

void ProjectTestbed::resizeLatestSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit resize: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("resize"), editContext))
        return;

    const NWB::Core::ECSGraphics::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSGraphics::DeformableSurfaceEditResizeResult resizeResult;
    if(!NWB::Core::ECSGraphics::ResizeSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            editId,
            m_surfaceEditRadius,
            m_surfaceEditEllipseRatio,
            m_surfaceEditDepth,
            &resizeResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit resize: failed to replay resized edit {}"), editId);
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("resize"), editContext.runtimeMesh, true);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit resize: edit={} radius {}->{} ellipse {}->{} depth {}->{} revision={}"),
        resizeResult.resizedEditId,
        resizeResult.oldRadius,
        resizeResult.newRadius,
        resizeResult.oldEllipseRatio,
        resizeResult.newEllipseRatio,
        resizeResult.oldDepth,
        resizeResult.newDepth,
        resizeResult.replay.finalEditRevision
    );
}

void ProjectTestbed::moveLatestSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit move: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("move"), editContext))
        return;

    NWB::Core::ECSGraphics::DeformablePosedHit targetHit;
    if(!pickSurfaceEditMutationTarget(NWB_TEXT("move"), editContext, targetHit))
        return;

    const NWB::Core::ECSGraphics::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSGraphics::DeformableSurfaceEditMoveResult moveResult;
    if(!NWB::Core::ECSGraphics::MoveSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            editId,
            targetHit,
            &moveResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit move: failed to replay moved edit {}"), editId);
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("move"), editContext.runtimeMesh, true);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit move: edit={} position ({},{},{}) -> ({},{},{}) revision={}"),
        moveResult.movedEditId,
        moveResult.oldRestPosition.x,
        moveResult.oldRestPosition.y,
        moveResult.oldRestPosition.z,
        moveResult.newRestPosition.x,
        moveResult.newRestPosition.y,
        moveResult.newRestPosition.z,
        moveResult.replay.finalEditRevision
    );
}

void ProjectTestbed::patchLatestSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit patch: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("patch"), editContext))
        return;

    NWB::Core::ECSGraphics::DeformablePosedHit targetHit;
    if(!pickSurfaceEditMutationTarget(NWB_TEXT("patch"), editContext, targetHit))
        return;

    const NWB::Core::ECSGraphics::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSGraphics::DeformableSurfaceEditPatchResult patchResult;
    if(!NWB::Core::ECSGraphics::PatchSurfaceEdit(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            editId,
            targetHit,
            m_surfaceEditRadius,
            m_surfaceEditEllipseRatio,
            m_surfaceEditDepth,
            &patchResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit patch: failed to replay patched edit {}"), editId);
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("patch"), editContext.runtimeMesh, true);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit patch: edit={} position ({},{},{}) -> ({},{},{}) radius {}->{} ellipse {}->{} depth {}->{} revision={}"),
        patchResult.patchedEditId,
        patchResult.oldRestPosition.x,
        patchResult.oldRestPosition.y,
        patchResult.oldRestPosition.z,
        patchResult.newRestPosition.x,
        patchResult.newRestPosition.y,
        patchResult.newRestPosition.z,
        patchResult.oldRadius,
        patchResult.newRadius,
        patchResult.oldEllipseRatio,
        patchResult.newEllipseRatio,
        patchResult.oldDepth,
        patchResult.newDepth,
        patchResult.replay.finalEditRevision
    );
}

void ProjectTestbed::addLoopCutToLatestSurfaceEdit(){
    if(m_surfaceEditState.edits.empty()){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit loop cut: no saved edits"));
        return;
    }

    SurfaceEditMutationContext editContext;
    if(!prepareSurfaceEditMutation(NWB_TEXT("loop cut"), editContext))
        return;

    const NWB::Core::ECSGraphics::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSGraphics::DeformableSurfaceEditLoopCutResult loopCutResult;
    if(!NWB::Core::ECSGraphics::AddSurfaceEditLoopCut(
            *editContext.instance,
            editContext.cleanBase,
            m_surfaceEditState,
            editId,
            &loopCutResult
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit loop cut: failed to replay loop cut edit {}"), editId);
        return;
    }

    finishSurfaceEditMutation(NWB_TEXT("loop cut"), editContext.runtimeMesh, true);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit loop cut: edit={} wall_loop_cuts {}->{} revision={}"),
        loopCutResult.loopCutEditId,
        loopCutResult.oldLoopCutCount,
        loopCutResult.newLoopCutCount,
        loopCutResult.replay.finalEditRevision
    );
}

void ProjectTestbed::logSurfaceEditControls()const{
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: left click preview, [/] radius={}, comma/period ellipse={}, -/= depth={}, Enter commit, F2 debug, F3 replay, F4 undo, F5 redo, F6 heal latest, F7 resize latest, F8 move latest, F9 patch latest, F10 loop cut latest, Esc cancel"),
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth
    );
}

void ProjectTestbed::toggleSurfaceEditDebug(){
    m_surfaceEditDebugEnabled = !m_surfaceEditDebugEnabled;
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit debug: {}"),
        m_surfaceEditDebugEnabled ? NWB_TEXT("enabled") : NWB_TEXT("disabled")
    );
    if(m_surfaceEditDebugEnabled)
        logSurfaceEditDebugSnapshot();
}

void ProjectTestbed::logSurfaceEditDebugSnapshot(){
    if(!m_surfaceEditDebugEnabled)
        return;

    const NWB::Core::ECSGraphics::RuntimeMeshHandle runtimeMesh = m_surfaceEditPreviewActive
        ? m_surfaceEditSession.runtimeMesh
        : m_surfaceEditDebugRuntimeMesh
    ;
    const auto* instance = rendererSystem().findDeformableRuntimeMesh(runtimeMesh);
    if(!instance){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit debug: no active runtime mesh"));
        return;
    }

    NWB::Core::ECSGraphics::DeformableSurfaceEditDebugSnapshot snapshot;
    UniquePtr<NWB::Core::Assets::IAsset> debugDisplacementTextureAsset;
    const auto* debugDisplacementTexture =
        __hidden_project_testbed_runtime::ResolveSurfaceEditDebugDisplacementTexture(
            *instance,
            m_context.assetManager,
            debugDisplacementTextureAsset
        )
    ;
    if(!NWB::Core::ECSGraphics::BuildDeformableSurfaceEditDebugSnapshot(
            *instance,
            m_surfaceEditPreviewActive ? &m_surfaceEditSession : nullptr,
            m_surfaceEditPreviewActive ? &m_surfaceEditPreview : nullptr,
            &m_surfaceEditState,
            debugDisplacementTexture,
            snapshot
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit debug: failed to build snapshot"));
        return;
    }

    AString dump;
    if(!NWB::Core::ECSGraphics::BuildDeformableSurfaceEditDebugDump(snapshot, dump)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit debug: failed to format snapshot"));
        return;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}"), StringConvert(dump));
}

bool ProjectTestbed::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);

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
        else if(key == NWB::Core::Key::F2){
            toggleSurfaceEditDebug();
        }
        else if(key == NWB::Core::Key::F3){
            queueSurfaceEditReplay();
        }
        else if(key == NWB::Core::Key::F4){
            undoSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F5){
            redoSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F6){
            healLatestSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F7){
            resizeLatestSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F8){
            moveLatestSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F9){
            patchLatestSurfaceEdit();
        }
        else if(key == NWB::Core::Key::F10){
            addLoopCutToLatestSurfaceEdit();
        }
        else if(key == NWB::Core::Key::Escape){
            cancelSurfaceEditPreview();
        }
    }

    return false;
}

bool ProjectTestbed::mousePosUpdate(const f64 xpos, const f64 ypos){
    if(!IsFinite(xpos) || !IsFinite(ypos)){
        m_cursorPositionValid = false;
        m_mousePositionValid = false;
        return false;
    }

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

    const f32 deltaX = static_cast<f32>(xpos - m_lastMouseX);
    const f32 deltaY = static_cast<f32>(ypos - m_lastMouseY);
    const f32 pendingDeltaX = m_pendingMouseDeltaX + deltaX;
    const f32 pendingDeltaY = m_pendingMouseDeltaY + deltaY;
    if(IsFinite(deltaX)
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

