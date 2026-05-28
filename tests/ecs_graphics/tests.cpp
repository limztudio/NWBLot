// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_skinned_mesh_render/resource_names.h>
#include <impl/ecs_skinned_mesh_render/skin_payload.h>

#include <tests/capturing_logger.h>
#include <tests/ecs_test_world.h>
#include <tests/test_context.h>

#include <core/common/module.h>
#include <core/ecs/module.h>
#include <core/mesh/classification.h>
#include <impl/ecs_skinned_mesh/components.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_lighting/system.h>
#include <impl/ecs_render/components.h>
#include <impl/assets_mesh/meshlet_ref_encoding.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skinned_asset.h>
#include <impl/assets_mesh/asset.h>

#include <core/common/log.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using TestContext = NWB::Tests::TestContext;
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;


#define NWB_ECS_GRAPHICS_TEST_CHECK NWB_TEST_CHECK


static void TestRuntimeResourceNameBuilderMatchesFormattedSuffix(TestContext& context){
    NWB::Tests::TestArena<> arena;
    const Name sourceName("project/meshes/skinned_mesh_source");
    const auto suffix = NWB::Impl::BuildRuntimeResourceSuffix(arena.arena, 42u, 17u, "skinned_mesh_ranges");
    NWB_ECS_GRAPHICS_TEST_CHECK(context, AStringView(suffix.data(), suffix.size()) == AStringView(":runtime_42_revision_17_skinned_mesh_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "skinned_mesh_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_skinned_mesh_ranges"));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, builtName == formattedName);
}


using TestWorld = NWB::Tests::EcsTestWorld;

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

static void TestMeshSystemResolvesMeshComponent(TestContext& context){
    TestWorld testWorld;
    auto& meshSystem = testWorld.world.addSystem<NWB::Impl::MeshSystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& mesh = entity.addComponent<NWB::Impl::MeshComponent>();
    mesh.mesh.virtualPath = Name("project/meshes/static_mesh");

    NWB::Core::Assets::AssetRef<NWB::Impl::Mesh> resolvedMesh;
    NWB_ECS_GRAPHICS_TEST_CHECK(context, meshSystem.resolveMesh(entity.id(), resolvedMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedMesh.name() == mesh.mesh.name());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, meshSystem.findMesh(entity.id()) == &mesh);

    auto missingMeshEntity = testWorld.world.createEntity();
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !meshSystem.resolveMesh(missingMeshEntity.id(), resolvedMesh));
    NWB_ECS_GRAPHICS_TEST_CHECK(context, !resolvedMesh.valid());
}

static NWB::Impl::SkinnedMeshJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[3] = Float4(x, y, z, 1.0f);
    return joint;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeZHalfTurnJointMatrix(){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeXHalfTurnJointMatrix(){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeYHalfTurnJointMatrix(){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeZQuarterTurnJointMatrix(){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::SkinnedMeshJointMatrix joint = NWB::Impl::MakeIdentitySkinnedMeshJointMatrix();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight[0] = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::SkinnedMeshRuntimeMeshInstance& instance, const u16 joint){
    instance.meshClass = NWB::Core::Mesh::MeshClass::Skinned;
    instance.skin.assign(instance.restPositions.size(), MakeSingleJointSkin(joint));
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static void AppendRuntimeVertex(NWB::Impl::SkinnedMeshRuntimeMeshInstance& instance, const Float3U& position, const f32 u){
    instance.restPositions.push_back(position);
    instance.restNormals.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));
    instance.restTangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    instance.uv0.push_back(Float2U(u, 0.0f));
    instance.colors.push_back(MakeHalf4U(1.0f, 1.0f, 1.0f, 1.0f));
}

static NWB::Impl::SkinnedMeshRuntimeMeshInstance MakeTriangleInstance(){
    NWB::Impl::SkinnedMeshRuntimeMeshInstance instance(NWB::Tests::TestDetail::Arena());
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    AppendRuntimeVertex(instance, Float3U(-1.0f, -1.0f, 0.0f), 0.0f);
    AppendRuntimeVertex(instance, Float3U(1.0f, -1.0f, 0.0f), 0.5f);
    AppendRuntimeVertex(instance, Float3U(0.0f, 1.0f, 0.0f), 1.0f);

    instance.meshlets.push_back(NWB::Impl::MeshletDesc{
        0u,
        0u,
        0u,
        0u,
        NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u),
    });
    instance.meshlets.back().skinBase = 0u;
    instance.meshletBounds.push_back(NWB::Impl::MeshletBounds{
        Float4U(0.0f, 0.0f, 0.0f, 2.0f),
        NWB::Impl::PackMeshletCone(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), 1.0f),
        0u,
    });
    Vector<NWB::Impl::MeshletPositionStreamRef> meshletPositionStreamRefs;
    Vector<NWB::Impl::MeshletAttributeStreamRef> meshletAttributeStreamRefs;
    for(usize vertexIndex = 0u; vertexIndex < instance.restPositions.size(); ++vertexIndex){
        meshletPositionStreamRefs.push_back(NWB::Impl::MeshletPositionStreamRef{
            static_cast<u32>(vertexIndex),
            static_cast<u32>(vertexIndex),
        });
        meshletAttributeStreamRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{
            static_cast<u32>(vertexIndex),
            static_cast<u32>(vertexIndex),
            static_cast<u32>(vertexIndex),
            static_cast<u32>(vertexIndex),
        });
        instance.meshletLocalVertexRefs.push_back(NWB::Impl::MeshletLocalVertexRef{
            static_cast<u16>(vertexIndex),
            static_cast<u16>(vertexIndex),
        });
        instance.attributeSkins.push_back(static_cast<u32>(vertexIndex));
    }
    for(const u32 index : MakeTriangleIndices())
        instance.meshletPrimitiveIndices.push_back(static_cast<u8>(index));
    const bool meshletRefsEncoded = NWB::Impl::EncodeMeshletRefDeltas(
        instance.meshlets,
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        instance.meshletPositionRefDeltas,
        instance.meshletAttributeRefDeltas,
        true,
        [](const usize, const tchar*){ return false; }
    );
    NWB_ASSERT(meshletRefsEncoded);
    static_cast<void>(meshletRefsEncoded);
    instance.meshletPositionRefCount = static_cast<u32>(meshletPositionStreamRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(meshletAttributeStreamRefs.size());

    return instance;
}

static NWB::Impl::SkinnedMeshJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

static void CheckJointRotationQuaternion(
    TestContext& context,
    const NWB::Impl::SkinnedMeshJointMatrix& joint,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w){
    SIMDVector quaternion = QuaternionIdentity();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshRuntime::TryBuildJointRotationQuaternion(
            LoadFloat(joint),
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
        !NWB::Impl::SkinnedMeshRuntime::TryBuildJointRotationQuaternion(
            LoadFloat(MakeNonUniformScaleJointMatrix()),
            quaternion
        )
    );
}

static NWB::Impl::SkinnedMeshSkeletonPoseComponent MakeTwoJointSkeletonPose(
    const NWB::Impl::SkinnedMeshJointMatrix& rootJoint,
    const NWB::Impl::SkinnedMeshJointMatrix& childJoint){
    NWB::Impl::SkinnedMeshSkeletonPoseComponent pose(NWB::Tests::TestDetail::Arena());
    pose.parentJoints.push_back(NWB::Impl::s_SkinnedMeshSkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

static void TestSkeletonPoseBuildsHierarchicalPalette(TestContext& context){
    NWB::Impl::SkinnedMeshSkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::SkinnedMeshJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::SkinnedMeshSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkinnedMeshSkinningMode::LinearBlend);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, resolvedJoints.size() == 2u);
    if(resolvedJoints.size() == 2u){
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[0u].rows[3].y, 0.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].x, 1.0f));
        NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(resolvedJoints[1u].rows[3].y, 2.0f));
    }

    pose.skinningMode = NWB::Impl::SkinnedMeshSkinningMode::DualQuaternion;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinningMode == NWB::Impl::SkinnedMeshSkinningMode::DualQuaternion);

    pose.parentJoints[1u] = 1u;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedMeshRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedMeshRuntime::BuildJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode)
    );
}
static void TestSkinnedMeshSkinPayloadValidatesSkeletonAndPalette(TestContext& context){
    NWB::Impl::SkinnedMeshRuntimeMeshInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::SkinnedMeshJointPaletteComponent joints(NWB::Tests::TestDetail::Arena());
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::SkinnedMeshSkinInfluenceGpu> skinInfluences;
    Vector<NWB::Impl::SkinnedMeshJointMatrix> jointMatrices;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences.size() == instance.skin.size());
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, skinInfluences[0u].joint[0u] == 0u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(skinInfluences[0u].weight.x, 1.0f));

    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));
    joints.joints[0u] = MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices)
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(context, jointMatrices.size() == 1u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, NearlyEqual(jointMatrices[0u].rows[3].x, 0.75f));
    joints.joints[0u] = MakeIdentityJointMatrix();

    NWB::Impl::SkinnedMeshRuntimeMeshInstance dualQuaternionInstance = MakeTriangleInstance();
    AssignSingleJointSkin(dualQuaternionInstance, 0u);
    dualQuaternionInstance.handle.value = instance.handle.value;
    joints.skinningMode = NWB::Impl::SkinnedMeshSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeTranslationJointMatrix(2.0f, 4.0f, 6.0f);
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(dualQuaternionInstance, &joints, skinInfluences, jointMatrices)
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
    joints.skinningMode = NWB::Impl::SkinnedMeshSkinningMode::LinearBlend;
    joints.joints[0u] = MakeIdentityJointMatrix();

#if defined(NWB_FINAL)
    CapturingLogger runtimeValidationLogger;
    NWB::Core::Common::LoggerRegistrationGuard runtimeValidationLoggerRegistrationGuard(runtimeValidationLogger);

    NWB::Impl::SkinnedMeshRuntimeMeshInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, NWB::Impl::MakeIdentitySkinnedMeshJointMatrix());
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(outsidePalette, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedMeshRuntimeMeshInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[3].w = 0.0f;
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(nonAffineJoint, &joints, skinInfluences, jointMatrices)
    );

    NWB::Impl::SkinnedMeshRuntimeMeshInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkinnedMeshSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        !NWB::Impl::SkinnedMeshSkinPayload::BuildSkinPayload(
            scaledDualQuaternionJoint,
            &joints,
            skinInfluences,
            jointMatrices
        )
    );

    NWB_ECS_GRAPHICS_TEST_CHECK(context, runtimeValidationLogger.errorCount() == 3u);
    NWB_ECS_GRAPHICS_TEST_CHECK(context, runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette count")));
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix"))
    );
    NWB_ECS_GRAPHICS_TEST_CHECK(
        context,
        runtimeValidationLogger.sawErrorContaining(NWB_TEXT("failed dual-quaternion payload build"))
    );
#endif
}



#undef NWB_ECS_GRAPHICS_TEST_CHECK


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_TEST_ENTRY_POINT("ecs graphics", [](NWB::Tests::TestContext& context){
    __hidden_tests::CapturingLogger logger;
    NWB::Core::Common::LoggerRegistrationGuard loggerRegistrationGuard(logger);

    __hidden_tests::TestRuntimeResourceNameBuilderMatchesFormattedSuffix(context);
    __hidden_tests::TestLightComponents(context);
    __hidden_tests::TestMeshSystemResolvesMeshComponent(context);
    __hidden_tests::TestJointRotationQuaternionBuildsColumnVectorRotations(context);
    __hidden_tests::TestSkeletonPoseBuildsHierarchicalPalette(context);
    __hidden_tests::TestSkinnedMeshSkinPayloadValidatesSkeletonAndPalette(context);
})


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

