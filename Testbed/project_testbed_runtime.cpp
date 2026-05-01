// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project_testbed.h"

#include <core/geometry/frame_math.h>
#include <global/simplemath.h>
#include <impl/assets_geometry/deformable_geometry_asset.h>
#include <impl/assets_geometry/geometry_asset.h>
#include <core/scene/light_component.h>
#include <impl/ecs_ui/ecs_ui.h>
#include <logger/client/logger.h>

#include <imgui.h>


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
static constexpr f32 s_SurfaceEditTargetY = 0.35f;
static constexpr f32 s_StaticPrimitiveY = -0.75f;
static constexpr AStringView s_DeformableProxyPath = "project/characters/proxy_deformable";
static constexpr AStringView s_DeformableImportedPath = "project/characters/imported_deformable";
static constexpr AStringView s_DeformableCsgCubePath = "project/characters/csg_cube";
static constexpr AStringView s_DeformableCsgSpherePath = "project/characters/csg_sphere";
static constexpr AStringView s_DeformableCsgTetrahedronPath = "project/characters/csg_tetrahedron";
static constexpr AStringView s_DeformableMaterialPath = "project/materials/mat_deformable_uv";
static constexpr AStringView s_SurfaceEditOperatorMaterialPath = "project/materials/mat_csg_subtract_preview";
static constexpr f32 s_SurfaceEditOperatorSurfaceOffsetMin = 0.001f;
static constexpr f32 s_SurfaceEditOperatorSurfaceOffsetRadiusScale = 0.025f;
static constexpr AStringView s_AccessoryGeometryPath = "project/meshes/mock_earring";
static constexpr AStringView s_AccessoryMaterialPath = "project/materials/mat_accessory_gold";


struct SurfaceEditTargetDesc{
    AStringView label;
    AStringView geometryPath;
    Float4 position;
    f32 uniformScale = 1.0f;
    f32 editRadius = 0.24f;
    f32 yawRadians = 0.0f;
    bool animated = false;
};

struct SurfaceEditOperatorDesc{
    AStringView label;
    AStringView geometryPath;
};

struct SurfaceEditCameraViewDesc{
    AStringView label;
    f32 yawRadians = 0.0f;
    f32 pitchRadians = 0.0f;
    f32 distance = s_CameraStartDepth;
};

struct EditorClientSize{
    u32 width = 0u;
    u32 height = 0u;
};

static const SurfaceEditTargetDesc s_SurfaceEditTargets[] = {
    { "plane", s_DeformableImportedPath, Float4(0.0f, s_SurfaceEditTargetY, 0.0f), 0.82f, 0.24f, s_PI, true },
    { "box", s_DeformableProxyPath, Float4(0.0f, s_SurfaceEditTargetY, 0.0f), 0.62f, 0.20f, 0.0f, true },
    { "cube", s_DeformableCsgCubePath, Float4(0.0f, s_SurfaceEditTargetY, 0.0f), 0.72f, 0.20f, 0.0f, false },
    { "sphere", s_DeformableCsgSpherePath, Float4(0.0f, s_SurfaceEditTargetY, 0.0f), 0.72f, 0.24f, 0.0f, false },
    { "tetrahedron", s_DeformableCsgTetrahedronPath, Float4(0.0f, s_SurfaceEditTargetY, 0.0f), 0.74f, 0.08f, 0.0f, false },
};
static constexpr usize s_SurfaceEditTargetCount = sizeof(s_SurfaceEditTargets) / sizeof(s_SurfaceEditTargets[0]);

static const SurfaceEditOperatorDesc s_SurfaceEditOperators[] = {
    { "cylinder", "project/meshes/csg_operator_cylinder" },
    { "box", "project/meshes/csg_operator_box" },
    { "triangle", "project/meshes/csg_operator_triangle" },
    { "cone", "project/meshes/csg_operator_cone" },
};
static constexpr usize s_SurfaceEditOperatorCount = sizeof(s_SurfaceEditOperators) / sizeof(s_SurfaceEditOperators[0]);

static const SurfaceEditCameraViewDesc s_SurfaceEditCameraViews[] = {
    { "front", 0.0f, 0.0f, s_CameraStartDepth },
    { "right high", -0.65f, 0.25f, 2.35f },
    { "side", -1.18f, 0.08f, 2.45f },
    { "left high", 0.7f, 0.34f, 2.45f },
};
static constexpr usize s_SurfaceEditCameraViewCount =
    sizeof(s_SurfaceEditCameraViews) / sizeof(s_SurfaceEditCameraViews[0])
;


[[nodiscard]] static const char* SurfaceEditTargetLabel(const usize targetIndex){
    return
        targetIndex < s_SurfaceEditTargetCount
            ? s_SurfaceEditTargets[targetIndex].label.data()
            : "invalid"
    ;
}

[[nodiscard]] static AStringView SurfaceEditOperatorLabelView(const usize operatorIndex){
    return
        operatorIndex < s_SurfaceEditOperatorCount
            ? s_SurfaceEditOperators[operatorIndex].label
            : AStringView("invalid")
    ;
}

[[nodiscard]] static const char* SurfaceEditOperatorLabel(const usize operatorIndex){
    return SurfaceEditOperatorLabelView(operatorIndex).data();
}

[[nodiscard]] static bool BuildSurfaceEditOperatorShape(
    NWB::Core::Assets::AssetManager& assetManager,
    const usize operatorIndex,
    NWB::Core::ECSDeformableEdit::DeformableOperatorFootprint& outFootprint,
    NWB::Core::ECSDeformableEdit::DeformableOperatorProfile& outProfile
){
    outFootprint = NWB::Core::ECSDeformableEdit::DeformableOperatorFootprint{};
    outProfile = NWB::Core::ECSDeformableEdit::DeformableOperatorProfile{};
    if(operatorIndex >= s_SurfaceEditOperatorCount)
        return false;

    const SurfaceEditOperatorDesc& desc = s_SurfaceEditOperators[operatorIndex];
    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(
        !assetManager.loadSync(NWB::Impl::Geometry::AssetTypeName(), Name(desc.geometryPath), loadedAsset)
        || !loadedAsset
        || loadedAsset->assetType() != NWB::Impl::Geometry::AssetTypeName()
    )
        return false;

    const auto& geometry = static_cast<const NWB::Impl::Geometry&>(*loadedAsset);
    return NWB::Core::ECSDeformableEdit::BuildOperatorShapeFromGeometry(
        geometry.vertices(),
        outFootprint,
        outProfile
    );
}

[[nodiscard]] static AStringView SurfaceEditCameraViewLabelView(const usize cameraViewIndex){
    return
        cameraViewIndex < s_SurfaceEditCameraViewCount
            ? s_SurfaceEditCameraViews[cameraViewIndex].label
            : AStringView("invalid")
    ;
}

[[nodiscard]] static const char* SurfaceEditCameraViewLabel(const usize cameraViewIndex){
    return SurfaceEditCameraViewLabelView(cameraViewIndex).data();
}

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
    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Enum permission){
    switch(permission){
    case NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Restricted:
        return NWB_TEXT("restricted");
    case NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Forbidden:
        return NWB_TEXT("forbidden");
    case NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Allowed:
    default:
        return NWB_TEXT("allowed");
    }
}

[[nodiscard]] static bool FiniteSurfaceEditOperatorVector3(const SIMDVector value){
    const SIMDVector invalid = VectorOrInt(VectorIsNaN(value), VectorIsInfinite(value));
    return (VectorMoveMask(invalid) & 0x7u) == 0u;
}

[[nodiscard]] static SIMDVector BuildSurfaceEditOperatorRotation(
    const SIMDVector worldTangent,
    const SIMDVector worldBitangent,
    const SIMDVector worldNormal
){
    SIMDMatrix basisAsRows{};
    basisAsRows.v[0] = VectorAndInt(worldTangent, s_SIMDMask3);
    basisAsRows.v[1] = VectorAndInt(worldBitangent, s_SIMDMask3);
    basisAsRows.v[2] = VectorAndInt(worldNormal, s_SIMDMask3);
    basisAsRows.v[3] = s_SIMDIdentityR3;
    return QuaternionNormalize(QuaternionRotationMatrix(MatrixTranspose(basisAsRows)));
}

[[nodiscard]] static bool ResolveSurfaceEditOperatorTransform(
    const NWB::Core::Scene::TransformComponent& targetTransform,
    const NWB::Core::ECSDeformableEdit::DeformableHolePreview& preview,
    NWB::Core::Scene::TransformComponent& outTransform
){
    if(!preview.valid)
        return false;

    const SIMDVector targetRotation = QuaternionNormalize(LoadFloat(targetTransform.rotation));
    if(QuaternionIsNaN(targetRotation) || QuaternionIsInfinite(targetRotation))
        return false;

    const SIMDVector targetScale = LoadFloat(targetTransform.scale);
    const SIMDVector localCenter = LoadFloat(preview.center);
    const SIMDVector localNormal = LoadFloat(preview.normal);
    const SIMDVector localTangent = LoadFloat(preview.tangent);
    const SIMDVector localBitangent = LoadFloat(preview.bitangent);
    if(
        !FiniteSurfaceEditOperatorVector3(targetScale)
        || !FiniteSurfaceEditOperatorVector3(localCenter)
        || !FiniteSurfaceEditOperatorVector3(localNormal)
        || !FiniteSurfaceEditOperatorVector3(localTangent)
        || !FiniteSurfaceEditOperatorVector3(localBitangent)
        || !IsFinite(preview.radius)
        || !IsFinite(preview.ellipseRatio)
        || !IsFinite(preview.depth)
        || preview.radius <= 0.0f
        || preview.ellipseRatio <= 0.0f
        || preview.depth <= 0.0f
    )
        return false;

    const SIMDVector worldNormal = NWB::Core::Geometry::FrameNormalizeDirection(
        Vector3Rotate(localNormal, targetRotation),
        VectorSet(0.0f, 0.0f, 1.0f, 0.0f)
    );
    const SIMDVector worldTangent = NWB::Core::Geometry::FrameResolveTangent(
        worldNormal,
        Vector3Rotate(localTangent, targetRotation),
        VectorSet(1.0f, 0.0f, 0.0f, 0.0f)
    );
    const SIMDVector worldBitangent = NWB::Core::Geometry::FrameNormalizeDirection(
        Vector3Rotate(localBitangent, targetRotation),
        Vector3Cross(worldNormal, worldTangent)
    );
    const SIMDVector operatorRotation = BuildSurfaceEditOperatorRotation(worldTangent, worldBitangent, worldNormal);
    if(QuaternionIsNaN(operatorRotation) || QuaternionIsInfinite(operatorRotation))
        return false;

    const SIMDVector scaledLocalCenter = VectorMultiply(localCenter, targetScale);
    const SIMDVector targetPosition = LoadFloat(targetTransform.position);
    const f32 targetUniformScale = Max(Abs(targetTransform.scale.x), Max(Abs(targetTransform.scale.y), Abs(targetTransform.scale.z)));
    const f32 surfaceOffset = Max(
        s_SurfaceEditOperatorSurfaceOffsetMin,
        preview.radius * s_SurfaceEditOperatorSurfaceOffsetRadiusScale
    ) * targetUniformScale;
    const SIMDVector worldPosition = VectorMultiplyAdd(
        worldNormal,
        VectorReplicate(surfaceOffset),
        VectorAdd(Vector3Rotate(scaledLocalCenter, targetRotation), targetPosition)
    );
    if(!FiniteSurfaceEditOperatorVector3(worldPosition))
        return false;

    StoreFloat(VectorSetW(worldPosition, 0.0f), &outTransform.position);
    StoreFloat(operatorRotation, &outTransform.rotation);
    outTransform.scale = Float4(
        preview.radius * Abs(targetTransform.scale.x),
        preview.radius * preview.ellipseRatio * Abs(targetTransform.scale.y),
        preview.depth * Abs(targetTransform.scale.z)
    );
    return FiniteSurfaceEditOperatorVector3(LoadFloat(outTransform.scale));
}

[[nodiscard]] static const NWB::Impl::DeformableDisplacementTexture* ResolveSurfaceEditDebugDisplacementTexture(
    const NWB::Core::ECSDeformable::DeformableRuntimeMeshInstance& instance,
    NWB::Core::Assets::AssetManager& assetManager,
    UniquePtr<NWB::Core::Assets::IAsset>& outLoadedAsset){
    outLoadedAsset.reset();
    const NWB::Impl::DeformableDisplacement& displacement = instance.displacement;
    if(!NWB::Impl::DeformableDisplacementModeUsesTexture(displacement.mode) || !displacement.texture.valid())
        return nullptr;

    if(
        !assetManager.loadSync(
            NWB::Impl::DeformableDisplacementTexture::AssetTypeName(),
            displacement.texture.name(),
            outLoadedAsset
        )
        || !outLoadedAsset
        || outLoadedAsset->assetType() != NWB::Impl::DeformableDisplacementTexture::AssetTypeName()
    )
        return nullptr;

    const auto* texture = checked_cast<const NWB::Impl::DeformableDisplacementTexture*>(outLoadedAsset.get());
    return
        texture->virtualPath() == displacement.texture.name() && texture->validatePayload()
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

[[nodiscard]] static EditorClientSize ResolveEditorClientSize(const NWB::Core::Graphics& graphics){
    i32 windowWidth = 0;
    i32 windowHeight = 0;
    graphics.getWindowDimensions(windowWidth, windowHeight);
    if(windowWidth > 0 && windowHeight > 0){
        return {
            static_cast<u32>(windowWidth),
            static_cast<u32>(windowHeight)
        };
    }

    const NWB::ProjectFrameClientSize clientSize = NWB::QueryProjectFrameClientSize();
    return { clientSize.width, clientSize.height };
}

[[nodiscard]] static bool FiniteVector3(SIMDVector value){
    return !Vector3IsNaN(value) && !Vector3IsInfinite(value);
}

[[nodiscard]] static bool FiniteFloat3(const Float4& value){
    return FiniteVector3(LoadFloat(value));
}

[[nodiscard]] static bool UiWantsKeyboardCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Core::ECSUI::UiSystem>();
    return uiSystem && uiSystem->wantsKeyboardCapture();
}

[[nodiscard]] static bool UiWantsMouseCapture(NWB::Core::ECS::World& world){
    auto* uiSystem = world.getSystem<NWB::Core::ECSUI::UiSystem>();
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

[[nodiscard]] static EditorVec3 NormalizeVec3(const EditorVec3& value, const EditorVec3& fallback){
    return NormalizeVec3(LoadFloat(static_cast<const Float4&>(value)), fallback);
}

static void ResolveFlyCameraAnglesFromTransform(
    const NWB::Core::Scene::TransformComponent& transform,
    f32& outYawRadians,
    f32& outPitchRadians){
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
    const EditorClientSize clientSize,
    const f64 cursorX,
    const f64 cursorY,
    NWB::Core::ECSDeformableEdit::DeformablePickingRay& outRay){
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

[[nodiscard]] static bool ResolveEditorViewUp(
    NWB::Core::ECS::World& world,
    Float3U& outViewUp
){
    const NWB::Core::Scene::SceneCameraView cameraView = NWB::Core::Scene::ResolveSceneCameraView(world);
    if(!cameraView.valid())
        return false;

    const SIMDVector viewUp = Vector3Rotate(
        VectorSet(0.0f, 1.0f, 0.0f, 0.0f),
        LoadFloat(cameraView.transform->rotation)
    );
    if(!FiniteVector3(viewUp))
        return false;

    const EditorVec3 normalizedViewUp = NormalizeVec3(viewUp, EditorVec3{ 0.0f, 1.0f, 0.0f });
    outViewUp = Float3U(normalizedViewUp.x, normalizedViewUp.y, normalizedViewUp.z);
    return true;
}

static void ApplySurfaceEditScalarParams(
    NWB::Core::ECSDeformableEdit::DeformableHoleEditParams& params,
    const f32 radius,
    const f32 ellipseRatio,
    const f32 depth
){
    params.radius = radius;
    params.ellipseRatio = ellipseRatio;
    params.depth = depth;
}

[[nodiscard]] static bool BuildSurfaceEditOperatorShapeForParams(
    NWB::Core::Assets::AssetManager& assetManager,
    const usize operatorIndex,
    NWB::Core::ECSDeformableEdit::DeformableHoleEditParams& params
){
    return BuildSurfaceEditOperatorShape(
        assetManager,
        operatorIndex,
        params.operatorFootprint,
        params.operatorProfile
    );
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

[[nodiscard]] static bool ApplySurfaceEditCameraView(
    NWB::Core::ECS::World& world,
    const usize cameraViewIndex
){
    if(cameraViewIndex >= s_SurfaceEditCameraViewCount)
        return false;

    const NWB::Core::Scene::SceneCameraView cameraView = NWB::Core::Scene::ResolveSceneCameraView(world);
    if(!cameraView.valid())
        return false;

    const SurfaceEditCameraViewDesc& preset = s_SurfaceEditCameraViews[cameraViewIndex];
    const SIMDVector rotation = QuaternionRotationRollPitchYaw(
        preset.pitchRadians,
        preset.yawRadians,
        0.0f
    );
    if(QuaternionIsNaN(rotation) || QuaternionIsInfinite(rotation))
        return false;

    const SIMDVector forward = Vector3Rotate(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotation);
    if(!FiniteVector3(forward))
        return false;

    const SIMDVector target = VectorSet(0.0f, s_SurfaceEditTargetY, 0.0f, 0.0f);
    const SIMDVector position = VectorSubtract(
        target,
        VectorMultiply(forward, VectorReplicate(Max(0.5f, preset.distance)))
    );
    if(!FiniteVector3(position))
        return false;

    StoreFloat(rotation, &cameraView.transform->rotation);
    StoreFloat(VectorSetW(position, 0.0f), &cameraView.transform->position);
    return true;
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

    auto& renderer = entity.addComponent<NWB::Core::ECSRender::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
}

[[nodiscard]] static NWB::Core::ECSDeformable::DeformableJointMatrix BuildProxySkinJoint(const f32 angleRadians){
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

    NWB::Core::ECSDeformable::DeformableJointMatrix joint{};
    joint.rows[0] = Float4(1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, cosAngle, sinAngle, 0.0f);
    joint.rows[2] = Float4(0.0f, -sinAngle, cosAngle, 0.0f);
    joint.rows[3] = Float4(
        0.0f,
        s_DeformableSkinPivotY * (1.0f - cosAngle),
        -s_DeformableSkinPivotY * sinAngle,
        1.0f
    );
    return joint;
}

static void UpdateProxySkeletonPose(
    NWB::Core::ECSDeformable::DeformableSkeletonPoseComponent& skeletonPose,
    const f32 timeSeconds
){
    const f32 safeTimeSeconds = IsFinite(timeSeconds) ? timeSeconds : 0.0f;

    skeletonPose.parentJoints.resize(2u);
    skeletonPose.parentJoints[0] = NWB::Core::ECSDeformable::s_DeformableSkeletonRootParent;
    skeletonPose.parentJoints[1] = 0u;
    skeletonPose.localJoints.resize(2u, NWB::Core::ECSDeformable::MakeIdentityDeformableJointMatrix());
    const f32 skinAngle = VectorGetX(VectorSin(VectorReplicate(safeTimeSeconds * 0.9f)));
    skeletonPose.localJoints[1] = BuildProxySkinJoint(s_DeformableSkinMaxAngle * skinAngle);
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateDeformableRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedDeformableGeometryRef& geometry,
    const TestbedMaterialRef& material,
    const Float4& position,
    const f32 uniformScale,
    const bool animated
){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.position = position;
    transform.scale = Float4(uniformScale, uniformScale, uniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSDeformable::DeformableRendererComponent>();
    renderer.deformableGeometry = geometry;
    renderer.material = material;

    if(!animated)
        return entity.id();

    auto& morphWeights = entity.addComponent<NWB::Core::ECSDeformable::DeformableMorphWeightsComponent>();
    morphWeights.weights.resize(1u);
    morphWeights.weights[0].morph = Name("lift");
    morphWeights.weights[0].weight = 0.0f;

    auto& skeletonPose = entity.addComponent<NWB::Core::ECSDeformable::DeformableSkeletonPoseComponent>();
    UpdateProxySkeletonPose(skeletonPose, 0.0f);

    auto& displacement = entity.addComponent<NWB::Core::ECSDeformable::DeformableDisplacementComponent>();
    displacement.amplitudeScale = 1.0f;
    displacement.enabled = true;
    return entity.id();
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSurfaceEditTargetEntity(
    NWB::Core::ECS::World& world,
    const SurfaceEditTargetDesc& target){
    TestbedDeformableGeometryRef geometry;
    geometry.virtualPath = Name(target.geometryPath);
    TestbedMaterialRef material;
    material.virtualPath = Name(s_DeformableMaterialPath);

    const NWB::Core::ECS::EntityID entity = CreateDeformableRendererEntity(
        world,
        geometry,
        material,
        target.position,
        target.uniformScale,
        target.animated
    );
    if(auto* transform = world.tryGetComponent<NWB::Core::Scene::TransformComponent>(entity))
        StoreFloat(QuaternionRotationRollPitchYaw(0.0f, target.yawRadians, 0.0f), &transform->rotation);
    return entity;
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateAccessoryRendererEntity(
    NWB::Core::ECS::World& world,
    const TestbedGeometryRef& geometry,
    const TestbedMaterialRef& material){
    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.scale = Float4(s_AccessoryUniformScale, s_AccessoryUniformScale, s_AccessoryUniformScale);

    auto& renderer = entity.addComponent<NWB::Core::ECSRender::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
    renderer.visible = false;
    entity.addComponent<NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent>();
    return entity.id();
}

[[nodiscard]] static NWB::Core::ECS::EntityID CreateSurfaceEditSubtractPreviewEntity(NWB::Core::ECS::World& world){
    TestbedGeometryRef geometry;
    geometry.virtualPath = Name(s_SurfaceEditOperators[0].geometryPath);
    TestbedMaterialRef material;
    material.virtualPath = Name(s_SurfaceEditOperatorMaterialPath);

    auto entity = world.createEntity();
    auto& transform = entity.addComponent<NWB::Core::Scene::TransformComponent>();
    transform.scale = Float4(1.0f, 1.0f, 1.0f);

    auto& renderer = entity.addComponent<NWB::Core::ECSRender::RendererComponent>();
    renderer.geometry = geometry;
    renderer.material = material;
    renderer.visible = false;
    return entity.id();
}

static void ResolvePickingInputs(
    NWB::Core::ECS::World& world,
    const NWB::Core::ECS::EntityID entity,
    NWB::Core::ECSDeformableEdit::DeformablePickingInputs& outInputs){
    outInputs = NWB::Core::ECSDeformableEdit::DeformablePickingInputs{};
    outInputs.morphWeights = world.tryGetComponent<NWB::Core::ECSDeformable::DeformableMorphWeightsComponent>(entity);
    outInputs.jointPalette = world.tryGetComponent<NWB::Core::ECSDeformable::DeformableJointPaletteComponent>(entity);
    outInputs.skeletonPose = world.tryGetComponent<NWB::Core::ECSDeformable::DeformableSkeletonPoseComponent>(entity);
    outInputs.displacement = world.tryGetComponent<NWB::Core::ECSDeformable::DeformableDisplacementComponent>(entity);
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
    auto* rendererSystem = world.getSystem<NWB::Core::ECSRender::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(
        rendererSystem,
        NWB_TEXT("ProjectTestbed initialization failed: renderer system is missing in initial world")
    );
}

NWB::Core::ECSRender::RendererSystem& ProjectTestbed::rendererSystem(){
    auto* system = m_world->getSystem<NWB::Core::ECSRender::RendererSystem>();
    NWB_FATAL_ASSERT_MSG(system, NWB_TEXT("ProjectTestbed runtime invariant failed: renderer system is missing"));
    return *system;
}

void ProjectTestbed::drawUiControls(){
    ImGui::SetNextWindowPos(ImVec2(18.0f, 18.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("NWB Testbed")){
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted("Renderer: mesh shader path with compute emulation fallback");
    ImGui::Separator();

    const char* currentTargetLabel =
        __hidden_project_testbed_runtime::SurfaceEditTargetLabel(m_surfaceEditTargetIndex)
    ;
    if(ImGui::BeginCombo("Surface target", currentTargetLabel)){
        for(usize i = 0u; i < __hidden_project_testbed_runtime::s_SurfaceEditTargetCount; ++i){
            const bool selected = i == m_surfaceEditTargetIndex;
            if(ImGui::Selectable(__hidden_project_testbed_runtime::SurfaceEditTargetLabel(i), selected)){
                m_pendingSurfaceEditTargetIndex = i;
                m_pendingSurfaceEditTargetSelection = true;
            }
            if(selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const char* currentOperatorLabel =
        __hidden_project_testbed_runtime::SurfaceEditOperatorLabel(m_surfaceEditOperatorIndex)
    ;
    if(ImGui::BeginCombo("Operator mesh", currentOperatorLabel)){
        for(usize i = 0u; i < __hidden_project_testbed_runtime::s_SurfaceEditOperatorCount; ++i){
            const bool selected = i == m_surfaceEditOperatorIndex;
            if(ImGui::Selectable(__hidden_project_testbed_runtime::SurfaceEditOperatorLabel(i), selected))
                static_cast<void>(selectSurfaceEditOperator(i));
            if(selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    const char* currentCameraViewLabel =
        __hidden_project_testbed_runtime::SurfaceEditCameraViewLabel(m_surfaceEditCameraViewIndex)
    ;
    if(ImGui::BeginCombo("Camera view", currentCameraViewLabel)){
        for(usize i = 0u; i < __hidden_project_testbed_runtime::s_SurfaceEditCameraViewCount; ++i){
            const bool selected = i == m_surfaceEditCameraViewIndex;
            if(ImGui::Selectable(__hidden_project_testbed_runtime::SurfaceEditCameraViewLabel(i), selected))
                static_cast<void>(selectSurfaceEditCameraView(i));
            if(selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    bool previewParamsChanged = false;
    f32 radius = m_surfaceEditRadius;
    if(ImGui::SliderFloat("Radius", &radius, 0.08f, 0.5f, "%.2f") && IsFinite(radius)){
        m_surfaceEditRadius = __hidden_project_testbed_runtime::ClampSurfaceEditRadius(radius);
        previewParamsChanged = true;
    }
    f32 ellipseRatio = m_surfaceEditEllipseRatio;
    if(ImGui::SliderFloat("Ellipse", &ellipseRatio, 0.5f, 2.0f, "%.2f") && IsFinite(ellipseRatio)){
        m_surfaceEditEllipseRatio =
            __hidden_project_testbed_runtime::ClampSurfaceEditEllipseRatio(ellipseRatio)
        ;
        previewParamsChanged = true;
    }
    f32 depth = m_surfaceEditDepth;
    if(ImGui::SliderFloat("Depth", &depth, 0.04f, 0.45f, "%.2f") && IsFinite(depth)){
        m_surfaceEditDepth = __hidden_project_testbed_runtime::ClampSurfaceEditDepth(depth);
        previewParamsChanged = true;
    }
    if(previewParamsChanged)
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::RefreshPreview;

    ImGui::Text(
        "Edits %zu  Accessories %zu  Redo %zu",
        m_surfaceEditState.edits.size(),
        m_surfaceEditState.accessories.size(),
        m_surfaceEditHistory.redoStack.size()
    );
    ImGui::Text("Preview: %s", m_surfaceEditPreviewActive ? "active" : "none");

    ImGui::Separator();
    ImGui::TextUnformatted("Viewport left click");
    i32 clickAction = static_cast<i32>(m_surfaceEditClickAction);
    bool clickActionChanged = ImGui::RadioButton(
        "Preview hole",
        &clickAction,
        static_cast<i32>(SurfaceEditClickAction::PreviewHole)
    );
    clickActionChanged |= ImGui::RadioButton(
        "Move latest edit",
        &clickAction,
        static_cast<i32>(SurfaceEditClickAction::MoveLatest)
    );
    clickActionChanged |= ImGui::RadioButton(
        "Patch latest edit",
        &clickAction,
        static_cast<i32>(SurfaceEditClickAction::PatchLatest)
    );
    if(clickActionChanged)
        m_surfaceEditClickAction = static_cast<SurfaceEditClickAction::Enum>(clickAction);

    ImGui::Separator();
    if(ImGui::Button("Commit Preview"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::CommitPreview;
    ImGui::SameLine();
    if(ImGui::Button("Cancel Preview"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::CancelPreview;

    if(ImGui::Button("Replay Saved State"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::ReplaySavedState;
    ImGui::SameLine();
    if(ImGui::Button("Undo"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::Undo;
    ImGui::SameLine();
    if(ImGui::Button("Redo"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::Redo;

    if(ImGui::Button("Heal Latest"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::HealLatest;
    ImGui::SameLine();
    if(ImGui::Button("Resize Latest"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::ResizeLatest;
    ImGui::SameLine();
    if(ImGui::Button("Add Loop Cut"))
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::AddLoopCut;

    bool debugEnabled = m_surfaceEditDebugEnabled;
    if(ImGui::Checkbox("Surface edit debug", &debugEnabled) && debugEnabled != m_surfaceEditDebugEnabled)
        m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::ToggleDebug;
    if(m_surfaceEditDebugEnabled){
        ImGui::SameLine();
        if(ImGui::Button("Log Debug Now"))
            m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::LogDebugSnapshot;
    }

    ImGui::Separator();
    const bool hasDisplacement =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableDisplacementComponent>(m_surfaceEditTargetEntity)
        != nullptr
    ;
    ImGui::BeginDisabled(!hasDisplacement);
    ImGui::Checkbox("Displacement enabled", &m_surfaceEditDisplacementEnabled);
    f32 displacementScale = IsFinite(m_surfaceEditDisplacementScale) ? m_surfaceEditDisplacementScale : 1.0f;
    if(ImGui::SliderFloat("Displacement scale", &displacementScale, 0.0f, 4.0f, "%.2f") && IsFinite(displacementScale))
        m_surfaceEditDisplacementScale = Max(0.0f, displacementScale);
    ImGui::EndDisabled();
    if(!hasDisplacement)
        ImGui::TextDisabled("Current target has no animated displacement component.");

    ImGui::Separator();
    ImGui::TextUnformatted("Camera: hold RMB, move with WASD, Shift boost");
    ImGui::TextUnformatted("Edit point: choose the left-click action above, then click the active deformable target.");
    ImGui::End();
}

void ProjectTestbed::processPendingUiActions(){
    u32 actions = m_pendingSurfaceEditUiActions;
    m_pendingSurfaceEditUiActions = SurfaceEditUiAction::None;

    if(m_pendingSurfaceEditTargetSelection){
        m_pendingSurfaceEditTargetSelection = false;
        if(!selectSurfaceEditTarget(m_pendingSurfaceEditTargetIndex))
            logSurfaceEditControls();
        actions &= ~SurfaceEditUiAction::RefreshPreview;
    }

    if((actions & SurfaceEditUiAction::RefreshPreview) != 0u)
        refreshSurfaceEditPreview();
    if((actions & SurfaceEditUiAction::CommitPreview) != 0u)
        commitSurfaceEditPreview();
    if((actions & SurfaceEditUiAction::CancelPreview) != 0u)
        cancelSurfaceEditPreview();
    if((actions & SurfaceEditUiAction::ReplaySavedState) != 0u)
        queueSurfaceEditReplay();
    if((actions & SurfaceEditUiAction::Undo) != 0u)
        undoSurfaceEdit();
    if((actions & SurfaceEditUiAction::Redo) != 0u)
        redoSurfaceEdit();
    if((actions & SurfaceEditUiAction::HealLatest) != 0u)
        healLatestSurfaceEdit();
    if((actions & SurfaceEditUiAction::ResizeLatest) != 0u)
        resizeLatestSurfaceEdit();
    if((actions & SurfaceEditUiAction::AddLoopCut) != 0u)
        addLoopCutToLatestSurfaceEdit();
    if((actions & SurfaceEditUiAction::ToggleDebug) != 0u)
        toggleSurfaceEditDebug();
    if((actions & SurfaceEditUiAction::LogDebugSnapshot) != 0u)
        logSurfaceEditDebugSnapshot();
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
    using TestbedGeometryRef = __hidden_project_testbed_runtime::TestbedGeometryRef;
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

    TestbedGeometryRef cubeGeometry;
    cubeGeometry.virtualPath = Name("project/meshes/cube");
    TestbedGeometryRef sphereGeometry;
    sphereGeometry.virtualPath = Name("project/meshes/sphere");
    TestbedGeometryRef tetrahedronGeometry;
    tetrahedronGeometry.virtualPath = Name("project/meshes/tetrahedron");

    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        cubeGeometry,
        cubeWarmMaterial,
        Float4(-1.05f, __hidden_project_testbed_runtime::s_StaticPrimitiveY, 0.0f),
        0.36f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        cubeGeometry,
        cubeCoolMaterial,
        Float4(-0.35f, __hidden_project_testbed_runtime::s_StaticPrimitiveY, 0.0f),
        0.42f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        sphereGeometry,
        transparentSphereMaterial,
        Float4(0.4f, __hidden_project_testbed_runtime::s_StaticPrimitiveY, 0.0f),
        0.38f
    );
    __hidden_project_testbed_runtime::CreateRendererEntity(
        *m_world,
        tetrahedronGeometry,
        transparentTetrahedronMaterial,
        Float4(1.05f, __hidden_project_testbed_runtime::s_StaticPrimitiveY, 0.0f),
        0.4f
    );
    m_surfaceEditPreviewEntity = __hidden_project_testbed_runtime::CreateSurfaceEditSubtractPreviewEntity(*m_world);

    auto uiEntity = m_world->createEntity();
    auto& ui = uiEntity.addComponent<NWB::Core::ECSUI::UiComponent>();
    ui.draw = [this](NWB::Core::ECSUI::UiDrawContext& context){
        static_cast<void>(context);
        drawUiControls();
    };

    if(!selectSurfaceEditTarget(0u))
        return false;

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("ProjectTestbed: startup scene created ({})"),
        NWB_TEXT("directional light, shared primitives, selectable deformable CSG targets")
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
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("ProjectTestbed: shutdown"));
}


bool ProjectTestbed::onUpdate(f32 delta){
    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;

    updateMainCamera(safeDelta);
    updateSurfaceEditTarget(safeDelta);
    m_world->tick(safeDelta);
    processPendingUiActions();
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

bool ProjectTestbed::selectSurfaceEditTarget(const usize targetIndex){
    if(targetIndex >= __hidden_project_testbed_runtime::s_SurfaceEditTargetCount)
        return false;

    if(m_pendingSurfaceEditReplay || m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit target: waiting for pending replay/accessory work"));
        return false;
    }

    const auto& target = __hidden_project_testbed_runtime::s_SurfaceEditTargets[targetIndex];
    if(m_surfaceEditTargetEntity.valid() && m_surfaceEditTargetIndex == targetIndex){
        NWB_LOGGER_ESSENTIAL_INFO(
            NWB_TEXT("Surface edit target: {} already active"),
            StringConvert(target.label)
        );
        return true;
    }

    clearSurfaceEditPreview();
    clearPendingSurfaceEditAccessory();
    hideSurfaceEditAccessoriesForTarget(m_surfaceEditTargetEntity);
    auto* oldRenderer =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableRendererComponent>(m_surfaceEditTargetEntity)
    ;
    if(oldRenderer)
        oldRenderer->visible = false;

    m_surfaceEditState = NWB::Core::ECSDeformableEdit::DeformableSurfaceEditState{};
    m_surfaceEditHistory = NWB::Core::ECSDeformableEdit::DeformableSurfaceEditHistory{};
    m_surfaceEditDebugRuntimeMesh.reset();
    m_surfaceEditTargetTime = 0.0f;
    m_surfaceEditDisplacementScale = 1.0f;
    m_surfaceEditDisplacementEnabled = true;
    m_surfaceEditRadius = target.editRadius;
    m_surfaceEditTargetIndex = targetIndex;
    m_surfaceEditTargetEntity = __hidden_project_testbed_runtime::CreateSurfaceEditTargetEntity(*m_world, target);

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit target: {} ({}/{})"),
        StringConvert(target.label),
        targetIndex + 1u,
        __hidden_project_testbed_runtime::s_SurfaceEditTargetCount
    );
    return true;
}

bool ProjectTestbed::selectSurfaceEditOperator(const usize operatorIndex){
    if(operatorIndex >= __hidden_project_testbed_runtime::s_SurfaceEditOperatorCount)
        return false;

    auto* renderer =
        m_world->tryGetComponent<NWB::Core::ECSRender::RendererComponent>(m_surfaceEditPreviewEntity)
    ;
    if(!renderer)
        return false;

    const auto& desc = __hidden_project_testbed_runtime::s_SurfaceEditOperators[operatorIndex];
    NWB::Core::ECSDeformableEdit::DeformableOperatorFootprint footprint;
    NWB::Core::ECSDeformableEdit::DeformableOperatorProfile profile;
    if(
        !__hidden_project_testbed_runtime::BuildSurfaceEditOperatorShape(
            m_context.assetManager,
            operatorIndex,
            footprint,
            profile
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit operator: '{}' does not expose a valid mesh shape")
            , StringConvert(__hidden_project_testbed_runtime::SurfaceEditOperatorLabelView(operatorIndex))
        );
        return false;
    }

    __hidden_project_testbed_runtime::TestbedGeometryRef geometry;
    geometry.virtualPath = Name(desc.geometryPath);
    renderer->geometry = geometry;
    m_surfaceEditOperatorIndex = operatorIndex;
    if(m_surfaceEditPreviewActive)
        static_cast<void>(refreshSurfaceEditPreview());

    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit operator: {} ({}/{}), footprint_vertices={} profile_samples={}"),
        StringConvert(__hidden_project_testbed_runtime::SurfaceEditOperatorLabelView(operatorIndex)),
        operatorIndex + 1u,
        __hidden_project_testbed_runtime::s_SurfaceEditOperatorCount,
        footprint.vertexCount,
        profile.sampleCount
    );
    return true;
}

bool ProjectTestbed::selectSurfaceEditCameraView(const usize cameraViewIndex){
    if(!__hidden_project_testbed_runtime::ApplySurfaceEditCameraView(*m_world, cameraViewIndex)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit camera: failed to apply view preset"));
        return false;
    }

    m_surfaceEditCameraViewIndex = cameraViewIndex;
    m_mouseLookActive = false;
    m_mousePositionValid = false;
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit camera: {} ({}/{})"),
        StringConvert(__hidden_project_testbed_runtime::SurfaceEditCameraViewLabelView(cameraViewIndex)),
        cameraViewIndex + 1u,
        __hidden_project_testbed_runtime::s_SurfaceEditCameraViewCount
    );
    return true;
}

void ProjectTestbed::updateMainCamera(const f32 delta){
    const f32 mouseDeltaX = m_pendingMouseDeltaX;
    const f32 mouseDeltaY = m_pendingMouseDeltaY;
    m_pendingMouseDeltaX = 0.0f;
    m_pendingMouseDeltaY = 0.0f;

    const bool keyboardCaptured = __hidden_project_testbed_runtime::UiWantsKeyboardCapture(*m_world);
    const f32 rightAxis = keyboardCaptured
        ? 0.0f
        : __hidden_project_testbed_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::A),
            keyPressed(NWB::Core::Key::D)
        )
    ;
    const f32 forwardAxis = keyboardCaptured
        ? 0.0f
        : __hidden_project_testbed_runtime::KeyAxis(
            keyPressed(NWB::Core::Key::S),
            keyPressed(NWB::Core::Key::W)
        )
    ;
    const bool boosted =
        !keyboardCaptured
        && (keyPressed(NWB::Core::Key::LeftShift) || keyPressed(NWB::Core::Key::RightShift))
    ;

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

void ProjectTestbed::updateSurfaceEditTarget(const f32 delta){
    if(!m_surfaceEditTargetEntity.valid())
        return;

    const f32 safeDelta = IsFinite(delta) ? Max(delta, 0.0f) : 0.0f;
    if(!IsFinite(m_surfaceEditTargetTime))
        m_surfaceEditTargetTime = 0.0f;

    m_surfaceEditTargetTime += safeDelta;
    if(!IsFinite(m_surfaceEditTargetTime))
        m_surfaceEditTargetTime = 0.0f;

    auto* morphWeights =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableMorphWeightsComponent>(m_surfaceEditTargetEntity)
    ;
    if(morphWeights && !morphWeights->weights.empty()){
        const f32 morphWeight =
            0.5f + 0.5f * VectorGetX(VectorSin(VectorReplicate(m_surfaceEditTargetTime * 1.35f)))
        ;
        morphWeights->weights[0].weight = IsFinite(morphWeight) ? morphWeight : 0.5f;
    }

    auto* skeletonPose =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableSkeletonPoseComponent>(m_surfaceEditTargetEntity)
    ;
    if(skeletonPose)
        __hidden_project_testbed_runtime::UpdateProxySkeletonPose(*skeletonPose, m_surfaceEditTargetTime);

    auto* displacement =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableDisplacementComponent>(m_surfaceEditTargetEntity)
    ;
    if(displacement){
        if(!IsFinite(m_surfaceEditDisplacementScale))
            m_surfaceEditDisplacementScale = 1.0f;

        m_surfaceEditDisplacementScale = Max(0.0f, m_surfaceEditDisplacementScale);
        displacement->enabled = m_surfaceEditDisplacementEnabled;
        displacement->amplitudeScale = m_surfaceEditDisplacementScale;
    }
}

void ProjectTestbed::updateSurfaceEditAccessories(){
    auto& renderSystem = rendererSystem();

    m_world->view<
        NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent,
        NWB::Core::Scene::TransformComponent,
        NWB::Core::ECSRender::RendererComponent
    >().each(
        [&](NWB::Core::ECS::EntityID entity,
            NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent& attachment,
            NWB::Core::Scene::TransformComponent& transform,
            NWB::Core::ECSRender::RendererComponent& renderer){
            static_cast<void>(entity);
            const auto* instance = renderSystem.findDeformableRuntimeMesh(attachment.runtimeMesh);
            if(!instance){
                renderer.visible = false;
                return;
            }
            const auto* targetRenderer =
                m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableRendererComponent>(attachment.targetEntity)
            ;
            if(!targetRenderer || !targetRenderer->visible){
                renderer.visible = false;
                return;
            }

            NWB::Core::ECSDeformableEdit::DeformablePickingInputs inputs;
            __hidden_project_testbed_runtime::ResolvePickingInputs(*m_world, attachment.targetEntity, inputs);
            inputs.assetManager = &m_context.assetManager;
            renderer.visible = NWB::Core::ECSDeformableEdit::ResolveAccessoryAttachmentTransform(
                *instance,
                inputs,
                attachment,
                transform
            );
        }
    );
}

void ProjectTestbed::hideSurfaceEditPreviewMesh(){
    if(!m_surfaceEditPreviewEntity.valid())
        return;

    auto* renderer =
        m_world->tryGetComponent<NWB::Core::ECSRender::RendererComponent>(m_surfaceEditPreviewEntity)
    ;
    if(renderer)
        renderer->visible = false;
}

bool ProjectTestbed::refreshSurfaceEditPreviewMesh(){
    if(!m_surfaceEditPreviewActive || !m_surfaceEditPreview.valid){
        hideSurfaceEditPreviewMesh();
        return false;
    }

    auto* previewTransform =
        m_world->tryGetComponent<NWB::Core::Scene::TransformComponent>(m_surfaceEditPreviewEntity)
    ;
    auto* previewRenderer =
        m_world->tryGetComponent<NWB::Core::ECSRender::RendererComponent>(m_surfaceEditPreviewEntity)
    ;
    const auto* targetTransform =
        m_world->tryGetComponent<NWB::Core::Scene::TransformComponent>(m_surfaceEditTargetEntity)
    ;
    if(!previewTransform || !previewRenderer || !targetTransform){
        hideSurfaceEditPreviewMesh();
        return false;
    }

    if(!__hidden_project_testbed_runtime::ResolveSurfaceEditOperatorTransform(
        *targetTransform,
        m_surfaceEditPreview,
        *previewTransform
    )){
        hideSurfaceEditPreviewMesh();
        return false;
    }

    previewRenderer->visible = true;
    return true;
}

void ProjectTestbed::clearSurfaceEditPreview(){
    hideSurfaceEditPreviewMesh();
    m_surfaceEditSession = NWB::Core::ECSDeformableEdit::DeformableSurfaceEditSession{};
    m_surfaceEditPreviewParams = NWB::Core::ECSDeformableEdit::DeformableHoleEditParams{};
    m_surfaceEditPreview = NWB::Core::ECSDeformableEdit::DeformableHolePreview{};
    m_surfaceEditPreviewActive = false;
}

void ProjectTestbed::clearPendingSurfaceEditAccessory(){
    m_pendingSurfaceEditRuntimeMesh = NWB::Core::ECSDeformable::RuntimeMeshHandle{};
    m_pendingSurfaceEditResult = NWB::Core::ECSDeformableEdit::DeformableHoleEditResult{};
    m_pendingSurfaceEditRecord = NWB::Core::ECSDeformableEdit::DeformableSurfaceEditRecord{};
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

    __hidden_project_testbed_runtime::ApplySurfaceEditScalarParams(
        m_surfaceEditPreviewParams,
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth
    );
    if(!__hidden_project_testbed_runtime::ResolveEditorViewUp(*m_world, m_surfaceEditPreviewParams.operatorUp)){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: could not resolve editor view up"));
        return false;
    }
    if(
        !__hidden_project_testbed_runtime::BuildSurfaceEditOperatorShapeForParams(
            m_context.assetManager,
            m_surfaceEditOperatorIndex,
            m_surfaceEditPreviewParams
        )
    ){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: selected operator shape is invalid"));
        return false;
    }
    if(
        !NWB::Core::ECSDeformableEdit::PreviewHole(
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

    if(!refreshSurfaceEditPreviewMesh()){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview visualization failed"));
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

    NWB::Core::ECSDeformableEdit::DeformablePickingRay ray;
    if(!buildSurfaceEditPickRay(ray)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: could not build editor pick ray"));
        return;
    }

    NWB::Core::ECSDeformableEdit::DeformablePosedHit hit;
    if(
        !NWB::Core::ECSDeformableEdit::RaycastVisibleDeformableRenderers(
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

    const NWB::Core::ECSDeformable::RuntimeMeshHandle targetRuntimeMesh =
        renderSystem.deformableRuntimeMeshHandle(m_surfaceEditTargetEntity)
    ;
    if(!targetRuntimeMesh.valid() || hit.runtimeMesh != targetRuntimeMesh){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: cursor hit is not on the active target"));
        return;
    }

    auto* instance = renderSystem.findDeformableRuntimeMesh(hit.runtimeMesh);
    if(!instance){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: hit runtime mesh is unavailable"));
        return;
    }

    NWB::Core::ECSDeformableEdit::DeformableHoleEditParams params;
    params.posedHit = hit;
    __hidden_project_testbed_runtime::ApplySurfaceEditScalarParams(
        params,
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth
    );
    if(!__hidden_project_testbed_runtime::ResolveEditorViewUp(*m_world, params.operatorUp)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: could not resolve editor view up"));
        return;
    }
    if(
        !__hidden_project_testbed_runtime::BuildSurfaceEditOperatorShapeForParams(
            m_context.assetManager,
            m_surfaceEditOperatorIndex,
            params
        )
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: selected operator shape is invalid"));
        return;
    }

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditSession session;
    NWB::Core::ECSDeformableEdit::DeformableHolePreview preview;
    if(
        !NWB::Core::ECSDeformableEdit::BeginSurfaceEdit(*instance, hit, session)
        || !NWB::Core::ECSDeformableEdit::PreviewHole(*instance, session, params, preview)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview failed for the selected deformable surface"));
        return;
    }

    m_surfaceEditSession = session;
    m_surfaceEditPreviewParams = params;
    m_surfaceEditPreview = preview;
    m_surfaceEditDebugRuntimeMesh = session.runtimeMesh;
    m_surfaceEditPreviewActive = true;
    if(!refreshSurfaceEditPreviewMesh()){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview visualization failed for the selected deformable surface"));
        return;
    }
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: selected preview radius={} ellipse={} depth={} rev={} permission={}, use the UI to commit"),
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
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit: preview a deformable surface before committing"));
        return;
    }

    auto* instance = rendererSystem().findDeformableRuntimeMesh(m_surfaceEditSession.runtimeMesh);
    if(!instance){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: preview runtime mesh is unavailable"));
        return;
    }

    __hidden_project_testbed_runtime::ApplySurfaceEditScalarParams(
        m_surfaceEditPreviewParams,
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth
    );
    if(
        !__hidden_project_testbed_runtime::BuildSurfaceEditOperatorShapeForParams(
            m_context.assetManager,
            m_surfaceEditOperatorIndex,
            m_surfaceEditPreviewParams
        )
    ){
        clearSurfaceEditPreview();
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: selected operator shape is invalid"));
        return;
    }
    NWB::Core::ECSDeformableEdit::DeformableHoleEditResult result;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditRecord record;
    if(
        !NWB::Core::ECSDeformableEdit::CommitHole(
            *instance,
            m_surfaceEditSession,
            m_surfaceEditPreviewParams,
            &result,
            &record
        )
    ){
        const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Enum permission =
            m_surfaceEditPreview.editPermission
        ;
        clearSurfaceEditPreview();
        if(permission == NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPermission::Forbidden){
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

    NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent attachment;
    if(
        !NWB::Core::ECSDeformableEdit::AttachAccessory(
            *instance,
            m_pendingSurfaceEditResult,
            __hidden_project_testbed_runtime::s_AccessoryNormalOffset,
            __hidden_project_testbed_runtime::s_AccessoryUniformScale,
            attachment
        )
    ){
        if((instance->dirtyFlags & NWB::Core::ECSDeformable::RuntimeMeshDirtyFlag::GpuUploadDirty) != 0u)
            return;

        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit: accessory attachment failed"));
        clearPendingSurfaceEditAccessory();
        return;
    }

    __hidden_project_testbed_runtime::TestbedGeometryRef accessoryGeometry;
    accessoryGeometry.virtualPath = Name(__hidden_project_testbed_runtime::s_AccessoryGeometryPath);
    __hidden_project_testbed_runtime::TestbedMaterialRef accessoryMaterial;
    accessoryMaterial.virtualPath = Name(__hidden_project_testbed_runtime::s_AccessoryMaterialPath);
    NWB::Core::ECSDeformableEdit::DeformableAccessoryAttachmentRecord accessoryRecord;
    accessoryRecord.geometry = accessoryGeometry;
    accessoryRecord.material = accessoryMaterial;
    if(
        !accessoryRecord.geometryVirtualPathText.assign(__hidden_project_testbed_runtime::s_AccessoryGeometryPath)
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

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditState candidateState = m_surfaceEditState;
    candidateState.edits.push_back(m_pendingSurfaceEditRecord);
    candidateState.accessories.push_back(accessoryRecord);

    NWB::Core::Assets::AssetBytes serializedState;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditState loadedState;
    if(
        !NWB::Core::ECSDeformableEdit::SerializeSurfaceEditState(candidateState, serializedState)
        || !NWB::Core::ECSDeformableEdit::DeserializeSurfaceEditState(serializedState, loadedState)
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
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent>(accessoryEntity)
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
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditState loadedState;
    if(
        !NWB::Core::ECSDeformableEdit::SerializeSurfaceEditState(m_surfaceEditState, serializedState)
        || !NWB::Core::ECSDeformableEdit::DeserializeSurfaceEditState(serializedState, loadedState)
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit replay: save/load validation failed"));
        return;
    }

    if(m_surfaceEditTargetIndex >= __hidden_project_testbed_runtime::s_SurfaceEditTargetCount){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit replay: active target index is invalid"));
        return;
    }

    const auto& selectedTarget =
        __hidden_project_testbed_runtime::s_SurfaceEditTargets[m_surfaceEditTargetIndex]
    ;
    Float4 replayPosition(0.0f, __hidden_project_testbed_runtime::s_SurfaceEditTargetY, 0.0f);
    f32 replayScale = 0.8f;
    const auto* oldTransform = m_world->tryGetComponent<NWB::Core::Scene::TransformComponent>(
        m_surfaceEditTargetEntity
    );
    if(oldTransform){
        replayPosition = oldTransform->position;
        replayScale = oldTransform->scale.x;
    }
    auto* oldRenderer =
        m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableRendererComponent>(m_surfaceEditTargetEntity)
    ;
    if(oldRenderer)
        oldRenderer->visible = false;

    __hidden_project_testbed_runtime::SurfaceEditTargetDesc replayTarget = selectedTarget;
    replayTarget.position = replayPosition;
    replayTarget.uniformScale = replayScale;
    m_surfaceEditTargetEntity =
        __hidden_project_testbed_runtime::CreateSurfaceEditTargetEntity(*m_world, replayTarget)
    ;
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

    const NWB::Core::ECSDeformable::RuntimeMeshHandle runtimeMesh =
        rendererSystem().deformableRuntimeMeshHandle(m_surfaceEditTargetEntity)
    ;
    auto* instance = rendererSystem().findDeformableRuntimeMesh(runtimeMesh);
    if(!runtimeMesh.valid() || !instance)
        return;

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditReplayContext replayContext;
    replayContext.assetManager = &m_context.assetManager;
    replayContext.world = m_world.get();
    replayContext.targetEntity = m_surfaceEditTargetEntity;

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditReplayResult replayResult;
    if(
        !NWB::Core::ECSDeformableEdit::ApplySurfaceEditState(
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
    const NWB::Core::ECSDeformable::DeformableRuntimeMeshInstance& instance,
    NWB::Core::ECSDeformable::DeformableRuntimeMeshInstance& outCleanBase)const
{
    outCleanBase = NWB::Core::ECSDeformable::DeformableRuntimeMeshInstance{};
    const Name sourceName = instance.source.name();
    if(!sourceName)
        return false;

    UniquePtr<NWB::Core::Assets::IAsset> loadedAsset;
    if(
        !m_context.assetManager.loadSync(
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
    outCleanBase.geometryClass = geometry.geometryClass();
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
    outCleanBase.dirtyFlags = NWB::Core::ECSDeformable::RuntimeMeshDirtyFlag::All;
    return true;
}

void ProjectTestbed::hideSurfaceEditAccessoriesForTarget(const NWB::Core::ECS::EntityID targetEntity){
    if(!targetEntity.valid())
        return;

    auto accessoryView = m_world->view<
        NWB::Core::ECSRender::RendererComponent,
        NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent
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
    const NWB::Core::ECSDeformable::RuntimeMeshHandle runtimeMesh =
        rendererSystem().deformableRuntimeMeshHandle(m_surfaceEditTargetEntity)
    ;
    if(!runtimeMesh.valid())
        return false;

    hideSurfaceEditAccessoriesForTarget(m_surfaceEditTargetEntity);
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
            m_world->tryGetComponent<NWB::Core::ECSDeformable::DeformableAccessoryAttachmentComponent>(accessoryEntity)
        ;
        if(!attachment)
            return false;

        attachment->targetEntity = m_surfaceEditTargetEntity;
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
    SurfaceEditMutationContext& outContext){
    outContext = SurfaceEditMutationContext{};
    if(m_pendingSurfaceEditReplay || m_pendingSurfaceEditAccessory){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit {}: waiting for pending replay/accessory work"), action);
        return false;
    }

    auto& renderSystem = rendererSystem();
    outContext.rendererSystem = &renderSystem;
    outContext.runtimeMesh = renderSystem.deformableRuntimeMeshHandle(m_surfaceEditTargetEntity);
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
    const NWB::Core::ECSDeformable::RuntimeMeshHandle runtimeMesh,
    const SurfaceEditRedoStackMode::Enum redoStackMode){
    clearSurfaceEditPreview();
    clearPendingSurfaceEditAccessory();
    if(redoStackMode == SurfaceEditRedoStackMode::Clear)
        m_surfaceEditHistory.redoStack.clear();
    m_surfaceEditDebugRuntimeMesh = runtimeMesh;
    if(!restoreSurfaceEditAccessoryEntities())
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: failed to restore accessory entities"), action);
}

bool ProjectTestbed::buildSurfaceEditPickRay(NWB::Core::ECSDeformableEdit::DeformablePickingRay& outRay){
    const __hidden_project_testbed_runtime::EditorClientSize clientSize =
        __hidden_project_testbed_runtime::ResolveEditorClientSize(m_context.graphics)
    ;
    const bool clientSizeValid = clientSize.width != 0u && clientSize.height != 0u;
    const f64 fallbackCursorX = static_cast<f64>(clientSizeValid ? clientSize.width : 1u) * 0.5;
    const f64 fallbackCursorY = static_cast<f64>(clientSizeValid ? clientSize.height : 1u) * 0.5;
    const f64 cursorX = clientSizeValid && m_cursorPositionValid ? m_cursorX : fallbackCursorX;
    const f64 cursorY = clientSizeValid && m_cursorPositionValid ? m_cursorY : fallbackCursorY;
    return __hidden_project_testbed_runtime::BuildEditorPickRay(*m_world, clientSize, cursorX, cursorY, outRay);
}

bool ProjectTestbed::pickSurfaceEditMutationTarget(
    const tchar* action,
    const SurfaceEditMutationContext& editContext,
    NWB::Core::ECSDeformableEdit::DeformablePosedHit& outTargetHit){
    outTargetHit = NWB::Core::ECSDeformableEdit::DeformablePosedHit{};

    NWB::Core::ECSDeformableEdit::DeformablePickingRay ray;
    if(!buildSurfaceEditPickRay(ray)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit {}: could not build editor pick ray"), action);
        return false;
    }

    if(
        !NWB::Core::ECSDeformableEdit::RaycastVisibleDeformableRenderers(
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

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditUndoResult undoResult;
    if(
        !NWB::Core::ECSDeformableEdit::UndoLastSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("undo"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Keep);

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

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditRedoResult redoResult;
    if(
        !NWB::Core::ECSDeformableEdit::RedoLastSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("redo"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Keep);

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

    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditHealResult healResult;
    if(
        !NWB::Core::ECSDeformableEdit::HealSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("heal"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Clear);

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

    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditResizeResult resizeResult;
    if(
        !NWB::Core::ECSDeformableEdit::ResizeSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("resize"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Clear);

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

    NWB::Core::ECSDeformableEdit::DeformablePosedHit targetHit;
    if(!pickSurfaceEditMutationTarget(NWB_TEXT("move"), editContext, targetHit))
        return;

    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditMoveResult moveResult;
    if(
        !NWB::Core::ECSDeformableEdit::MoveSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("move"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Clear);

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

    NWB::Core::ECSDeformableEdit::DeformablePosedHit targetHit;
    if(!pickSurfaceEditMutationTarget(NWB_TEXT("patch"), editContext, targetHit))
        return;

    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditPatchResult patchResult;
    if(
        !NWB::Core::ECSDeformableEdit::PatchSurfaceEdit(
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

    finishSurfaceEditMutation(NWB_TEXT("patch"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Clear);

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

    const NWB::Core::ECSDeformableEdit::DeformableSurfaceEditId editId =
        m_surfaceEditState.edits.back().editId
    ;
    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditLoopCutResult loopCutResult;
    if(
        !NWB::Core::ECSDeformableEdit::AddSurfaceEditLoopCut(
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

    finishSurfaceEditMutation(NWB_TEXT("loop cut"), editContext.runtimeMesh, SurfaceEditRedoStackMode::Clear);

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
        NWB_TEXT("Surface edit controls are available in the NWB Testbed UI panel")
    );
    NWB_LOGGER_ESSENTIAL_INFO(
        NWB_TEXT("Surface edit: radius={} ellipse={} depth={} operator={}, choose a left-click viewport action in UI and use UI buttons for commit/replay/history/debug"),
        m_surfaceEditRadius,
        m_surfaceEditEllipseRatio,
        m_surfaceEditDepth,
        StringConvert(__hidden_project_testbed_runtime::SurfaceEditOperatorLabelView(m_surfaceEditOperatorIndex))
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

    const NWB::Core::ECSDeformable::RuntimeMeshHandle runtimeMesh = m_surfaceEditPreviewActive
        ? m_surfaceEditSession.runtimeMesh
        : m_surfaceEditDebugRuntimeMesh
    ;
    const auto* instance = rendererSystem().findDeformableRuntimeMesh(runtimeMesh);
    if(!instance){
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Surface edit debug: no active runtime mesh"));
        return;
    }

    NWB::Core::ECSDeformableEdit::DeformableSurfaceEditDebugSnapshot snapshot;
    UniquePtr<NWB::Core::Assets::IAsset> debugDisplacementTextureAsset;
    const auto* debugDisplacementTexture =
        __hidden_project_testbed_runtime::ResolveSurfaceEditDebugDisplacementTexture(
            *instance,
            m_context.assetManager,
            debugDisplacementTextureAsset
        )
    ;
    if(
        !NWB::Core::ECSDeformableEdit::BuildDeformableSurfaceEditDebugSnapshot(
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
    if(!NWB::Core::ECSDeformableEdit::BuildDeformableSurfaceEditDebugDump(snapshot, dump)){
        NWB_LOGGER_WARNING(NWB_TEXT("Surface edit debug: failed to format snapshot"));
        return;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("{}"), StringConvert(dump));
}

bool ProjectTestbed::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);

    if(action == NWB::Core::InputAction::Release)
        setKeyState(key, false);

    if(action == NWB::Core::InputAction::Press){
        if(key >= NWB::Core::Key::Number1 && key <= NWB::Core::Key::Number9){
            const usize targetIndex = static_cast<usize>(key - NWB::Core::Key::Number1);
            if(targetIndex < __hidden_project_testbed_runtime::s_SurfaceEditTargetCount)
                static_cast<void>(selectSurfaceEditTarget(targetIndex));
        }
        else if(key >= NWB::Core::Key::F1 && key <= NWB::Core::Key::F25){
            if(key < NWB::Core::Key::F1 + static_cast<i32>(__hidden_project_testbed_runtime::s_SurfaceEditOperatorCount)){
                const usize operatorIndex = static_cast<usize>(key - NWB::Core::Key::F1);
                static_cast<void>(selectSurfaceEditOperator(operatorIndex));
            }
            else if(
                key >= NWB::Core::Key::F5
                && key < NWB::Core::Key::F5 + static_cast<i32>(__hidden_project_testbed_runtime::s_SurfaceEditCameraViewCount)
            ){
                const usize cameraViewIndex = static_cast<usize>(key - NWB::Core::Key::F5);
                static_cast<void>(selectSurfaceEditCameraView(cameraViewIndex));
            }
        }
        else if(key >= NWB::Core::Key::Keypad1 && key <= NWB::Core::Key::Keypad9){
            const usize targetIndex = static_cast<usize>(key - NWB::Core::Key::Keypad1);
            if(targetIndex < __hidden_project_testbed_runtime::s_SurfaceEditTargetCount)
                static_cast<void>(selectSurfaceEditTarget(targetIndex));
        }
        else if(key == NWB::Core::Key::C){
            m_pendingSurfaceEditUiActions |= SurfaceEditUiAction::CommitPreview;
        }
    }

    if(__hidden_project_testbed_runtime::UiWantsKeyboardCapture(*m_world))
        return false;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        setKeyState(key, true);

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

    if(__hidden_project_testbed_runtime::UiWantsMouseCapture(*m_world)){
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

    if(__hidden_project_testbed_runtime::UiWantsMouseCapture(*m_world)){
        if(button == NWB::Core::MouseButton::Right && action == NWB::Core::InputAction::Release){
            m_mouseLookActive = false;
            m_mousePositionValid = false;
        }
        return false;
    }

    if(button == NWB::Core::MouseButton::Left){
        if(action == NWB::Core::InputAction::Press){
            switch(m_surfaceEditClickAction){
            case SurfaceEditClickAction::MoveLatest:
                moveLatestSurfaceEdit();
                break;
            case SurfaceEditClickAction::PatchLatest:
                patchLatestSurfaceEdit();
                break;
            case SurfaceEditClickAction::PreviewHole:
            default:
                previewSurfaceEditAtCursor();
                break;
            }
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

