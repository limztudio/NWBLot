// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_skinned_geometry_render/skinned_geometry_runtime_resource_names.h>
#include <impl/ecs_skinned_geometry_render/skinned_geometry_skin_payload.h>

#include <tests/capturing_logger.h>
#include <tests/ecs_test_world.h>
#include <tests/test_context.h>

#include <core/common/common.h>
#include <core/ecs/ecs.h>
#include <impl/ecs_skinned_geometry/components.h>
#include <impl/ecs_geometry/ecs_geometry.h>
#include <impl/ecs_lighting/lighting.h>
#include <impl/ecs_render/components.h>
#include <impl/assets_geometry/skinned_geometry_asset.h>
#include <impl/assets_geometry/geometry_asset.h>

#include <core/common/log.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeTriangleIndices;


#define NWB_ECS_GRAPHICS_TEST_CHECK NWB_TEST_CHECK


static void TestRuntimeResourceNameBuilderMatchesFormattedSuffix(TestContext& context){
    const Name sourceName("project/meshes/skinned_geometry_source");
    const AString suffix = NWB::Impl::BuildRuntimeResourceSuffix(42u, 17u, "skinned_geometry_ranges");
    NWB_ECS_GRAPHICS_TEST_CHECK(context, suffix == AStringView(":runtime_42_revision_17_skinned_geometry_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "skinned_geometry_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_skinned_geometry_ranges"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, builtName == formattedName);
}


struct ECSGraphicsTestAllocatorTag;
using ECSGraphicsTestAllocator = NWB::Tests::CountingTestAllocator<ECSGraphicsTestAllocatorTag>;
using TestWorld = NWB::Tests::EcsTestWorldWithAllocator<ECSGraphicsTestAllocator>;

static void TestLightComponents(TestContext& context){
    TestWorld testWorld;

    auto directionalEntity = testWorld.world.createEntity();
    auto& directionalTransform = directionalEntity.addComponent<NWB::Impl::TransformComponent>();
    auto& directionalLight = directionalEntity.addComponent<NWB::Impl::LightComponent>();

    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Impl::TransformComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalEntity.hasComponent<NWB::Impl::LightComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.type == NWB::Impl::LightType::Directional);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().x == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().y == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.color().z == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.intensity() > 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLight.range > 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalTransform.rotation.w == 1.0f);

    auto pointEntity = testWorld.world.createEntity();
    auto& pointTransform = pointEntity.addComponent<NWB::Impl::TransformComponent>();
    auto& pointLight = pointEntity.addComponent<NWB::Impl::LightComponent>();
    pointTransform.position = Float4(1.0f, 2.0f, 3.0f);
    pointLight.type = NWB::Impl::LightType::Point;
    pointLight.setColor(Float4(1.0f, 0.75f, 0.5f));
    pointLight.setIntensity(4.0f);
    pointLight.range = 12.0f;

    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Impl::TransformComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointEntity.hasComponent<NWB::Impl::LightComponent>());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.type == NWB::Impl::LightType::Point);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().x == 1.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().y == 0.75f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.color().z == 0.5f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.intensity() == 4.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLight.range == 12.0f);

    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        (reinterpret_cast<usize>(&directionalLight) % alignof(NWB::Impl::LightComponent)) == 0
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, (reinterpret_cast<usize>(&pointLight) % alignof(NWB::Impl::LightComponent)) == 0);

    const NWB::Core::ECS::EntityID pointEntityId = pointEntity.id();
    usize lightViewCount = 0;
    usize directionalLightCount = 0;
    usize pointLightCount = 0;
    testWorld.world.view<
        NWB::Impl::TransformComponent,
        NWB::Impl::LightComponent
    >().each(
        [&context, &lightViewCount, &directionalLightCount, &pointLightCount, pointEntityId](
            NWB::Core::ECS::EntityID entityId,
            NWB::Impl::TransformComponent& viewTransform,
            NWB::Impl::LightComponent& viewLight
        ){
            ++lightViewCount;
            if(viewLight.type == NWB::Impl::LightType::Directional){
                ++directionalLightCount;
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewLight.intensity() > 0.0f);
            }
            else if(viewLight.type == NWB::Impl::LightType::Point){
                ++pointLightCount;
                NWB_ECS_GRAPHICS_TEST_CHECK(context, entityId == pointEntityId);
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewTransform.position.x == 1.0f);
                NWB_ECS_GRAPHICS_TEST_CHECK(context, viewLight.range > 0.0f);
            }
            else{
                NWB_ECS_GRAPHICS_TEST_CHECK(context, false);
            }
        }
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, lightViewCount == 2);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, directionalLightCount == 1);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, pointLightCount == 1);
}

static void TestGeometrySystemResolvesGeometryComponent(TestContext& context){
    TestWorld testWorld;
    auto& geometrySystem = testWorld.world.addSystem<NWB::Impl::GeometrySystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& geometry = entity.addComponent<NWB::Impl::GeometryComponent>();
    geometry.geometry.virtualPath = Name("project/meshes/static_mesh");

    NWB::Core::Assets::AssetRef<NWB::Impl::Geometry> resolvedGeometry;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, geometrySystem.resolveGeometry(entity.id(), resolvedGeometry));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedGeometry.name() == geometry.geometry.name());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, geometrySystem.findGeometry(entity.id()) == &geometry);

    auto missingGeometryEntity = testWorld.world.createEntity();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !geometrySystem.resolveGeometry(missingGeometryEntity.id(), resolvedGeometry));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !resolvedGeometry.valid());
}

static bool NearlyEqual(const f32 lhs, const f32 rhs, const f32 epsilon = 0.00001f){
    const f32 difference = lhs > rhs ? lhs - rhs : rhs - lhs;
    return difference <= epsilon;
}

static NWB::Impl::SkinnedGeometryVertex MakeVertex(const f32 x, const f32 y, const f32 z, const f32 u = 0.0f){
    return NWB::Impl::MakeSkinnedGeometryVertex(
        Float3U(x, y, z),
        Float3U(0.0f, 0.0f, 1.0f),
        Float4U(1.0f, 0.0f, 0.0f, 1.0f),
        Float2U(u, 0.0f),
        Float4U(1.0f, 1.0f, 1.0f, 1.0f)
    );
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[3] = Float4(x, y, z, 1.0f);
    return joint;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeZHalfTurnJointMatrix(){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeXHalfTurnJointMatrix(){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeYHalfTurnJointMatrix(){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeZQuarterTurnJointMatrix(){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedGeometryJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::SkinnedGeometryJointMatrix joint = NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight[0] = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::SkinnedGeometryRuntimeMeshInstance& instance, const u16 joint){
    instance.geometryClass = NWB::Impl::GeometryClass::Skinned;
    instance.skin.resize(instance.restVertices.size());
    for(NWB::Impl::SkinInfluence4& skin : instance.skin)
        skin = MakeSingleJointSkin(joint);
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static NWB::Impl::SkinnedGeometryRuntimeMeshInstance MakeTriangleInstance(){
    NWB::Impl::SkinnedGeometryRuntimeMeshInstance instance;
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    instance.restVertices.push_back(MakeVertex(-1.0f, -1.0f, 0.0f, 0.0f));
    instance.restVertices.push_back(MakeVertex(1.0f, -1.0f, 0.0f, 0.5f));
    instance.restVertices.push_back(MakeVertex(0.0f, 1.0f, 0.0f, 1.0f));
    instance.indices = MakeTriangleIndices();
    return instance;
}
static NWB::Impl::SkinnedGeometryJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

static void CheckJointRotationQuaternion(
    TestContext& context,
    const NWB::Impl::SkinnedGeometryJointMatrix& joint,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w){
    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometryRuntime::TryBuildJointRotationQuaternion(
            NWB::Impl::SkinnedGeometryRuntime::LoadJointMatrix(joint),
            quaternion
        )
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetX(quaternion), x));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetY(quaternion), y));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetZ(quaternion), z));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(VectorGetW(quaternion), w));
}

static void TestJointRotationQuaternionBuildsColumnVectorRotations(TestContext& context){
    constexpr f32 s_HalfSqrtTwo = 0.70710678118f;

    CheckJointRotationQuaternion(context, MakeIdentityJointMatrix(), 0.0f, 0.0f, 0.0f, 1.0f);
    CheckJointRotationQuaternion(context, MakeZQuarterTurnJointMatrix(), 0.0f, 0.0f, s_HalfSqrtTwo, s_HalfSqrtTwo);
    CheckJointRotationQuaternion(context, MakeXHalfTurnJointMatrix(), 1.0f, 0.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(context, MakeYHalfTurnJointMatrix(), 0.0f, 1.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(context, MakeZHalfTurnJointMatrix(), 0.0f, 0.0f, 1.0f, 0.0f);

    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometryRuntime::TryBuildJointRotationQuaternion(
            NWB::Impl::SkinnedGeometryRuntime::LoadJointMatrix(MakeNonUniformScaleJointMatrix()),
            quaternion
        )
    );
}

static NWB::Impl::SkinnedGeometrySkeletonPoseComponent MakeTwoJointSkeletonPose(
    const NWB::Impl::SkinnedGeometryJointMatrix& rootJoint,
    const NWB::Impl::SkinnedGeometryJointMatrix& childJoint){
    NWB::Impl::SkinnedGeometrySkeletonPoseComponent pose;
    pose.parentJoints.push_back(NWB::Impl::s_SkinnedGeometrySkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

static void TestSkeletonPoseBuildsHierarchicalPalette(TestContext& context){
    NWB::Impl::SkinnedGeometrySkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::SkinnedGeometryJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::SkinnedGeometrySkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometryRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkinnedGeometrySkinningMode::LinearBlend);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedJoints.size() == 2u);
    if(resolvedJoints.size() == 2u){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].y, 2.0f));
    }

    pose.skinningMode = NWB::Impl::SkinnedGeometrySkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometryRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkinnedGeometrySkinningMode::DualQuaternion);

    pose.parentJoints[1u] = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometryRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometryRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
}
static void TestSkinnedGeometrySkinPayloadValidatesSkeletonAndPalette(TestContext& context){
    NWB::Impl::SkinnedGeometryRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::SkinnedGeometryJointPaletteComponent joints;
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::SkinnedGeometrySkinInfluenceGpu> skinInfluences;
    Vector<NWB::Impl::SkinnedGeometryJointMatrix> jointMatrices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.size() == instance.restVertices.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences[0u].joint[0u] == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(skinInfluences[0u].weight.x, 1.0f));

    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));
    joints.joints[0u] = MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[3].x, 0.75f));
    joints.joints[0u] = MakeIdentityJointMatrix();

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance dualQuaternionInstance = MakeTriangleInstance();
    AssignSingleJointSkin(dualQuaternionInstance, 0u);
    dualQuaternionInstance.handle.value = instance.handle.value;
    joints.skinningMode = NWB::Impl::SkinnedGeometrySkinningMode::DualQuaternion;
    joints.joints[0u] = MakeTranslationJointMatrix(2.0f, 4.0f, 6.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(dualQuaternionInstance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].x, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].y, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].z, 0.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[0].w, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].x, 1.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].y, 2.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].z, 3.0f));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[1].w, 0.0f));
    joints.skinningMode = NWB::Impl::SkinnedGeometrySkinningMode::LinearBlend;
    joints.joints[0u] = MakeIdentityJointMatrix();

#if defined(NWB_FINAL)
    CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance missingSkeleton = instance;
    missingSkeleton.skeletonJointCount = 0u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(missingSkeleton, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.empty());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.empty());

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance outsideSkeleton = instance;
    outsideSkeleton.skin[0u] = MakeSingleJointSkin(1u);
    outsideSkeleton.skeletonJointCount = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(outsideSkeleton, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, NWB::Impl::MakeIdentitySkinnedGeometryJointMatrix());
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(outsidePalette, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[3].w = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(nonAffineJoint, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance invalidInverseBind = instance;
    invalidInverseBind.inverseBindMatrices[0u].rows[3].w = 0.0f;
    joints.joints[0u] = MakeIdentityJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(invalidInverseBind, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedGeometryRuntimeMeshInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkinnedGeometrySkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedGeometrySkinPayload::BuildSkinPayload(
            scaledDualQuaternionJoint,
            &joints,
            skinInfluences,
            jointMatrices
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.errorCount() == 6u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("has skin but no skeleton joint count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("outside skeleton joint count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("outside palette size")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix"))
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, logger.sawErrorContaining(NWB_TEXT("inverse bind matrices are invalid")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        logger.sawErrorContaining(NWB_TEXT("not rigid for dual-quaternion skinning"))
    );
#endif
}



#undef NWB_ECS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("ecs graphics", [](NWB::Tests::TestContext& context){
    __hidden_ecs_graphics_tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    __hidden_ecs_graphics_tests::TestRuntimeResourceNameBuilderMatchesFormattedSuffix(context);
    __hidden_ecs_graphics_tests::TestLightComponents(context);
    __hidden_ecs_graphics_tests::TestGeometrySystemResolvesGeometryComponent(context);
    __hidden_ecs_graphics_tests::TestJointRotationQuaternionBuildsColumnVectorRotations(context);
    __hidden_ecs_graphics_tests::TestSkeletonPoseBuildsHierarchicalPalette(context);
    __hidden_ecs_graphics_tests::TestSkinnedGeometrySkinPayloadValidatesSkeletonAndPalette(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

