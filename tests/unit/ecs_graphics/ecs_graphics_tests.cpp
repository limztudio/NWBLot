// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/common/test_context.h>

#include <impl/ecs_mesh/skinning/skin_payload.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/material/renderer_pipeline_types.h>
#include <impl/assets_mesh/meshlet_ref_encode.h>

#include <core/ecs/entity.h>

#include <global/compile.h>
#include <global/limit.h>
#include <global/span.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;
using TestWorld = NWB::Tests::EcsTestWorld;
using NWB::Tests::s_TriangleIndices;

inline constexpr Name s_ScratchArena("tests/ecs_graphics/scratch");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, MissingOpticalAttachmentsDisableAuxiliarySamplingInBothPresentationModes){
    NWB::Impl::AvboitFrameTargets targets;
    targets.fullWidth = 1920u;
    targets.fullHeight = 1080u;
    targets.lowWidth = 480u;
    targets.lowHeight = 270u;
    targets.virtualSliceCount = 128u;
    targets.physicalSliceCount = 64u;
    targets.deferredSlotsBufferDescriptor = NWB::Core::GpuDescriptorHandle::Make(NWB::Core::GpuDescriptorClass::UniformBuffer, 23u);

    const auto sdr = NWB::Impl::ResolveRendererAvboitPushConstants(
        targets.fullWidth,
        targets.fullHeight,
        targets.lowWidth,
        targets.lowHeight,
        targets.virtualSliceCount,
        targets.physicalSliceCount,
        false,
        targets.refractionFramebuffer != nullptr,
        targets.deferredSlotsBufferDescriptor.slot()
    );
    const auto hdr10 = NWB::Impl::ResolveRendererAvboitPushConstants(
        targets.fullWidth,
        targets.fullHeight,
        targets.lowWidth,
        targets.lowHeight,
        targets.virtualSliceCount,
        targets.physicalSliceCount,
        true,
        targets.refractionFramebuffer != nullptr,
        targets.deferredSlotsBufferDescriptor.slot()
    );
    EXPECT_FLOAT_EQ(sdr.params.raw[NWB_AVBOIT_PUSH_PARAMS_REFRACTION_CAPTURE], NWB_AVBOIT_REFRACTION_DISABLED);
    EXPECT_FLOAT_EQ(hdr10.params.raw[NWB_AVBOIT_PUSH_PARAMS_REFRACTION_CAPTURE], NWB_AVBOIT_REFRACTION_DISABLED);
}

TEST(EcsGraphics, MeshSkinningSubmissionCommitRejectKeepsPoseAndSelectorPending){
    NWB::Impl::RuntimeMeshDirtyFlag::Flag dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty | NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty;
    b8 bindlessResourceSlotsUploaded = false;
    NWB::Impl::MeshSkinningSubmissionCommit commit;
    commit.editRevision = 17u;
    commit.handledDirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty;
    commit.bindlessResourceSlotsUploadRecorded = true;

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(false, 17u, commit, dirtyFlags, bindlessResourceSlotsUploaded);

    EXPECT_EQ(dirtyFlags, NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty | NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty);
    EXPECT_FALSE(bindlessResourceSlotsUploaded);

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(true, 17u, commit, dirtyFlags, bindlessResourceSlotsUploaded);

    EXPECT_EQ(dirtyFlags, NWB::Impl::RuntimeMeshDirtyFlag::GpuUploadDirty);
    EXPECT_TRUE(bindlessResourceSlotsUploaded);
}

TEST(EcsGraphics, MeshSkinningSubmissionCommitPreservesNewerEditRevision){
    NWB::Impl::RuntimeMeshDirtyFlag::Flag dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty;
    b8 bindlessResourceSlotsUploaded = false;
    NWB::Impl::MeshSkinningSubmissionCommit commit;
    commit.editRevision = 17u;
    commit.handledDirtyFlags = dirtyFlags;
    commit.bindlessResourceSlotsUploadRecorded = true;

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(true, 18u, commit, dirtyFlags, bindlessResourceSlotsUploaded);

    EXPECT_EQ(dirtyFlags, NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty);
    EXPECT_FALSE(bindlessResourceSlotsUploaded);
}


TEST(EcsGraphics, MissingMeshDoesNotReplacePreviousResolution){
    TestWorld testWorld;
    auto& meshSystem = testWorld.world.addSystem<NWB::Impl::MeshSystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& mesh = entity.addComponent<NWB::Impl::MeshComponent>();
    mesh.mesh.virtualPath = Name("project/meshes/static_mesh");

    const auto resolvedMesh = meshSystem.resolveMesh(entity.id());
    ASSERT_TRUE(resolvedMesh);

    auto missingMeshEntity = testWorld.world.createEntity();
    EXPECT_FALSE(meshSystem.resolveMesh(missingMeshEntity.id()));
    EXPECT_TRUE(resolvedMesh->valid());
}


TEST(EcsGraphics, MaterialPipelineCacheRejectsReplacedCsgEvaluatorInclude){
    using PipelineKey = NWB::Impl::MaterialPipelineKey;
    using PipelineMap = HashMap<PipelineKey, u32, NWB::Core::Alloc::GlobalArena, NWB::Impl::MaterialPipelineKeyHasher, NWB::Impl::MaterialPipelineKeyEqualTo>;

    NWB::Tests::TestArena<> arena;
    NWB::Impl::CsgShapeRegistry registry(arena.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    auto shape = registry.findShapeType(NWB::Impl::s_CsgBoxShapeName);
    ASSERT_TRUE(shape);
    shape->desc.shaderModule = Name("tests/csg/pipeline_evaluator");
    shape->desc.shaderModuleInclude = "tests/csg/pipeline_evaluator_a.slangi";
    const auto registered = registry.registerShapeType(shape->desc, true);
    ASSERT_TRUE(registered);

    PipelineKey original;
    original.material = Name("tests/csg/pipeline_material");
    original.csgMode = NWB::Impl::MaterialPipelineCsgMode::ClipOnly;
    original.csgEvaluatorVariant = shape->desc.shaderModule;
    original.csgShapeRegistryRevision = registry.revision();
    PipelineMap pipelines(0u, PipelineMap::hasher(), PipelineMap::key_equal(), arena.arena);
    ASSERT_TRUE(pipelines.emplace(original, 17u).second);

    shape->desc.shaderModuleInclude = "tests/csg/pipeline_evaluator_b.slangi";
    const auto replaced = registry.registerShapeType(shape->desc, true);
    ASSERT_TRUE(replaced);
    EXPECT_EQ(*registered, *replaced);
    const auto include = registry.findShaderModuleInclude(original.csgEvaluatorVariant, arena.arena);
    ASSERT_TRUE(include);
    EXPECT_EQ(AStringView(*include), "tests/csg/pipeline_evaluator_b.slangi");

    PipelineKey updated = original;
    updated.csgShapeRegistryRevision = registry.revision();
    EXPECT_NE(original.csgShapeRegistryRevision, updated.csgShapeRegistryRevision);
    EXPECT_EQ(original.material, updated.material);
    EXPECT_EQ(original.csgEvaluatorVariant, updated.csgEvaluatorVariant);
    EXPECT_FALSE(PipelineMap::key_equal()(original, updated));
    EXPECT_EQ(pipelines.find(updated), pipelines.end());
    const auto retained = pipelines.find(original);
    ASSERT_NE(retained, pipelines.end());
    EXPECT_EQ(retained.value(), 17u);
}





////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Impl::SkeletonJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z)noexcept{
    NWB::Impl::SkeletonJointMatrix joint = ::MatrixIdentity<Float34>();
    joint.rows[0].w = x;
    joint.rows[1].w = y;
    joint.rows[2].w = z;
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeNonUniformScaleJointMatrix()noexcept{
    NWB::Impl::SkeletonJointMatrix joint = ::MatrixIdentity<Float34>();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

#if defined(NWB_FINAL)
static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint)noexcept{
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight.x = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(const u16 joint, NWB::Impl::MeshSkinningRuntimeInstance& instance){
    instance.skin.assign(instance.restPositions.size(), MakeSingleJointSkin(joint));
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static void AppendRuntimeVertex(const Float3U& position, const f32 u, NWB::Impl::MeshSkinningRuntimeInstance& instance){
    instance.restPositions.push_back(position);
    instance.restNormals.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));
    instance.restTangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    instance.uv0.push_back(Float2U(u, 0.0f));
    instance.colors.push_back(MakeHalf4U(1.0f, 1.0f, 1.0f, 1.0f));
}

static NWB::Impl::MeshSkinningRuntimeInstance MakeTriangleInstance(NWB::Core::Alloc::ScratchArena& scratchArena){
    NWB::Impl::MeshSkinningRuntimeInstance instance(NWB::Tests::TestDetail::Arena());
    instance.entity = NWB::Core::ECS::EntityID(1u, 0u);
    instance.handle.value = 42u;
    instance.editRevision = 7u;
    instance.dirtyFlags = NWB::Impl::RuntimeMeshDirtyFlag::None;
    AppendRuntimeVertex(Float3U(-1.0f, -1.0f, 0.0f), 0.0f, instance);
    AppendRuntimeVertex(Float3U(1.0f, -1.0f, 0.0f), 0.5f, instance);
    AppendRuntimeVertex(Float3U(0.0f, 1.0f, 0.0f), 1.0f, instance);

    instance.meshlets.push_back(NWB::Impl::MeshletDesc{ 0u, 0u, 0u, 0u, NWB::Impl::PackMeshletCounts(3u, 1u, 3u, 3u) });
    instance.meshlets.back().skinBase = 0u;
    instance.meshletBounds.push_back(NWB::Impl::MeshletBounds{ Float4U(0.0f, 0.0f, 0.0f, 2.0f), NWB::Impl::PackMeshletCone(VectorSet(0.0f, 0.0f, 1.0f, 0.0f), s_SIMDOne) });
    ::Vector<NWB::Impl::MeshletPositionStreamRef, NWB::Core::Alloc::ScratchArena> meshletPositionStreamRefs(scratchArena);
    meshletPositionStreamRefs.reserve(instance.restPositions.size());
    ::Vector<NWB::Impl::MeshletAttributeStreamRef, NWB::Core::Alloc::ScratchArena> meshletAttributeStreamRefs(scratchArena);
    meshletAttributeStreamRefs.reserve(instance.restPositions.size());
    for(usize vertexIndex = 0u; vertexIndex < instance.restPositions.size(); ++vertexIndex){
        meshletPositionStreamRefs.push_back(NWB::Impl::MeshletPositionStreamRef{ static_cast<u32>(vertexIndex), static_cast<u32>(vertexIndex) });
        meshletAttributeStreamRefs.push_back(NWB::Impl::MeshletAttributeStreamRef{ static_cast<u32>(vertexIndex), static_cast<u32>(vertexIndex), static_cast<u32>(vertexIndex), static_cast<u32>(vertexIndex) });
        instance.meshletLocalVertexRefs.push_back(NWB::Impl::MeshletLocalVertexRef{ static_cast<u16>(vertexIndex), static_cast<u16>(vertexIndex) });
        instance.attributeSkins.push_back(static_cast<u32>(vertexIndex));
    }
    for(const u32 index : s_TriangleIndices)
        instance.meshletPrimitiveIndices.push_back(static_cast<u8>(index));
    const b8 meshletRefsEncoded = NWB::Impl::EncodeMeshletRefDeltas(
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        true,
        [](const usize, const TStringView)noexcept{ return false; },
        instance.meshlets,
        instance.meshletPositionRefDeltas,
        instance.meshletAttributeRefDeltas
    );
    NWB_FATAL_ASSERT(meshletRefsEncoded);
    instance.meshletPositionRefCount = static_cast<u32>(meshletPositionStreamRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(meshletAttributeStreamRefs.size());

    return instance;
}

static NWB::Impl::SkeletonJointMatrix MakeIdentityJointMatrix()noexcept{
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

#endif

static NWB::Impl::SkeletonPoseComponent MakeTwoJointSkeletonPose(const NWB::Impl::SkeletonJointMatrix& rootJoint, const NWB::Impl::SkeletonJointMatrix& childJoint){
    NWB::Impl::SkeletonPoseComponent pose;
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonInvalidJointIndex);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

TEST(EcsGraphics, NonUniformScaleCannotBecomeRigidJointRotation){
    EXPECT_FALSE(MatrixTryBuildRigidRotationQuaternion(LoadFloat(MakeNonUniformScaleJointMatrix()), NWB::Impl::s_SkeletonAffineEpsilon, NWB::Impl::s_SkeletonRigidJointEpsilon));
}

TEST(EcsGraphics, InvalidSkeletonParentsAndJointCountsAreRejected){
    NWB::Impl::SkeletonPoseComponent pose = MakeTwoJointSkeletonPose(MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f), MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f));

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    ::Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::ScratchArena> resolvedJoints(scratchArena);
    resolvedJoints.reserve(pose.localJoints.size());
    ASSERT_TRUE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints));
    pose.parentJoints[1u] = 1u;
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints));
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints));
}

#if defined(NWB_FINAL)
TEST(EcsGraphics, MeshSkinningPayloadValidatesSkeletonAndPalette){

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    NWB::Impl::MeshSkinningRuntimeInstance instance = MakeTriangleInstance(scratchArena);
    AssignSingleJointSkin(0u, instance);
    instance.handle.value = 517u;

    NWB::Impl::SkeletonJointPaletteComponent joints;
    joints.joints.push_back(MakeIdentityJointMatrix());

    ::Vector<NWB::Impl::SkeletonJointMatrix, NWB::Core::Alloc::ScratchArena> jointMatrices(scratchArena);
    jointMatrices.reserve(joints.joints.size());
    CapturingLogger runtimeValidationLogger;
    NWB::Core::Common::LoggerRegistrationGuard runtimeValidationLoggerRegistrationGuard(runtimeValidationLogger);

    NWB::Impl::MeshSkinningRuntimeInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, ::MatrixIdentity<Float34>());
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(outsidePalette, joints.joints, joints.skinningMode, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(nonAffineJoint, joints.joints, joints.skinningMode, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(scaledDualQuaternionJoint, joints.joints, joints.skinningMode, jointMatrices));

    EXPECT_EQ(runtimeValidationLogger.errorCount(), 3u);
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette count")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("failed dual-quaternion payload build")));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

