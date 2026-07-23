// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/resource_names.h>
#include <impl/ecs_mesh/skinning/skin_payload.h>

#include <tests/capturing_logger.h>
#include <tests/ecs_test_world.h>
#include <tests/meshlet_ref_test_data.h>
#include <tests/test_context.h>
#include <gtest/gtest.h>

#include <core/common/module.h>
#include <core/ecs/module.h>
#include <core/mesh/classification.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_skeleton/components.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skin_types.h>
#include <impl/assets_mesh/asset.h>

#include <core/common/log.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
using AString = NWB::Tests::TestAString;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;

inline constexpr Name s_ScratchArena("tests/ecs_graphics/scratch");


TEST(EcsGraphics, RuntimeResourceNameBuilderMatchesFormattedSuffix){
    NWB::Tests::TestArena<> arena;
    const Name sourceName("project/meshes/mesh_skinning_source");
    const auto suffix = NWB::Impl::BuildRuntimeResourceSuffix(arena.arena, 42u, 17u, "mesh_skinning_ranges");
    EXPECT_EQ(AStringView(suffix.data(), suffix.size()), AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "mesh_skinning_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));
    EXPECT_EQ(builtName, formattedName);
}


using TestWorld = NWB::Tests::EcsTestWorld;

TEST(EcsGraphics, MeshViewWorldToClipMatrixKeepsVectorLanesIntact){
    const SIMDMatrix worldToClip = NWB::Impl::ECSRenderDetail::BuildWorldToClipMatrix(
        VectorSet(3.0f, -2.0f, 5.0f, 0.75f),
        s_SIMDIdentityR0,
        s_SIMDIdentityR1,
        s_SIMDIdentityR2,
        VectorSet(2.0f, 3.0f, 4.0f, -1.0f)
    );

    Float44 matrix = {};
    StoreFloat(worldToClip, &matrix);
    EXPECT_FLOAT_EQ(matrix._11, 2.0f);
    EXPECT_FLOAT_EQ(matrix._14, -6.0f);
    EXPECT_FLOAT_EQ(matrix._22, 3.0f);
    EXPECT_FLOAT_EQ(matrix._24, 6.0f);
    EXPECT_FLOAT_EQ(matrix._33, 4.0f);
    EXPECT_FLOAT_EQ(matrix._34, -18.0f);
    EXPECT_FLOAT_EQ(matrix._43, 1.0f);
    EXPECT_FLOAT_EQ(matrix._44, -4.25f);

    Float4 clipPosition;
    StoreFloat(Vector4Transform(VectorSet(3.0f, -2.0f, 5.0f, 1.0f), worldToClip), &clipPosition);
    EXPECT_FLOAT_EQ(clipPosition.x, 0.0f);
    EXPECT_FLOAT_EQ(clipPosition.y, 0.0f);
    EXPECT_FLOAT_EQ(clipPosition.z, 2.0f);
    EXPECT_FLOAT_EQ(clipPosition.w, 0.75f);
}

TEST(EcsGraphics, MeshSystemResolvesMeshComponent){
    TestWorld testWorld;
    auto& meshSystem = testWorld.world.addSystem<NWB::Impl::MeshSystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& mesh = entity.addComponent<NWB::Impl::MeshComponent>();
    mesh.mesh.virtualPath = Name("project/meshes/static_mesh");

    NWB::Core::Assets::AssetRef<NWB::Impl::Mesh> resolvedMesh;
    EXPECT_TRUE(meshSystem.resolveMesh(entity.id(), resolvedMesh));
    EXPECT_EQ(resolvedMesh.name(), mesh.mesh.name());
    EXPECT_EQ(meshSystem.findMesh(entity.id()), &mesh);

    auto missingMeshEntity = testWorld.world.createEntity();
    EXPECT_FALSE(meshSystem.resolveMesh(missingMeshEntity.id(), resolvedMesh));
    EXPECT_FALSE(resolvedMesh.valid());
}

static u32 TestFloatBits(const f32 value){
    u32 bits = 0u;
    NWB_MEMCPY(&bits, sizeof(bits), &value, sizeof(value));
    return bits;
}

TEST(EcsGraphics, MaterialInstanceComponentSetters){
    TestWorld testWorld;
    const Name materialInterface("project/material_interfaces/test_surface");
    auto entity = testWorld.world.createEntity();
    auto& materialInstance = entity.addComponent<NWB::Impl::MaterialInstanceComponent>(
        NWB::Tests::TestDetail::Arena(),
        materialInterface
    );
    EXPECT_EQ(materialInstance.materialInterface, materialInterface);

    EXPECT_TRUE(NWB::Impl::SetMaterialMutableFloat(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.fade_alpha",
        0.5f
    ));
    EXPECT_EQ(materialInstance.overrides.size(), 1u);
    EXPECT_EQ(materialInstance.revision, 1u);
    EXPECT_EQ(materialInstance.overrides[0u].parameterName, Name("runtime.fade_alpha"));
    EXPECT_EQ(materialInstance.overrides[0u].blockName, Name("runtime"));
    EXPECT_EQ(materialInstance.overrides[0u].fieldName, Name("fade_alpha"));
    EXPECT_EQ(materialInstance.overrides[0u].fieldType, NWB::Impl::MaterialLayoutFieldType::Float);
    EXPECT_EQ(materialInstance.overrides[0u].value.raw[0u], TestFloatBits(0.5f));

    EXPECT_TRUE(NWB::Impl::SetMaterialMutableFloat(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.fade_alpha",
        0.75f
    ));
    EXPECT_EQ(materialInstance.overrides.size(), 1u);
    EXPECT_EQ(materialInstance.revision, 2u);
    EXPECT_EQ(materialInstance.overrides[0u].value.raw[0u], TestFloatBits(0.75f));

    EXPECT_TRUE(NWB::Impl::SetMaterialMutableFloat4(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.tint",
        Float4(1.0f, 0.5f, 0.25f, 0.125f)
    ));
    EXPECT_EQ(materialInstance.overrides.size(), 2u);
    EXPECT_EQ(materialInstance.revision, 3u);
    EXPECT_EQ(materialInstance.overrides[1u].parameterName, Name("runtime.tint"));
    EXPECT_EQ(materialInstance.overrides[1u].fieldType, NWB::Impl::MaterialLayoutFieldType::Float4);
    EXPECT_EQ(materialInstance.overrides[1u].value.raw[0u], TestFloatBits(1.0f));
    EXPECT_EQ(materialInstance.overrides[1u].value.raw[1u], TestFloatBits(0.5f));
    EXPECT_EQ(materialInstance.overrides[1u].value.raw[2u], TestFloatBits(0.25f));
    EXPECT_EQ(materialInstance.overrides[1u].value.raw[3u], TestFloatBits(0.125f));

    EXPECT_TRUE(NWB::Impl::SetMaterialMutableHalf4(
        testWorld.world,
        entity.id(),
        materialInterface,
        "runtime.color_tint",
        Float4(1.0f, 0.5f, 0.25f, 0.125f)
    ));
    const NWB::Impl::MaterialInstanceParameter& half4Override = materialInstance.overrides[2u];
    const Half4U expectedHalf4 = MakeHalf4U(1.0f, 0.5f, 0.25f, 0.125f);
    EXPECT_EQ(materialInstance.overrides.size(), 3u);
    EXPECT_EQ(materialInstance.revision, 4u);
    EXPECT_EQ(half4Override.parameterName, Name("runtime.color_tint"));
    EXPECT_EQ(half4Override.fieldType, NWB::Impl::MaterialLayoutFieldType::Half4);
    EXPECT_EQ(half4Override.value.raw[0u], static_cast<u32>(expectedHalf4.x) | (static_cast<u32>(expectedHalf4.y) << 16u));
    EXPECT_EQ(half4Override.value.raw[1u], static_cast<u32>(expectedHalf4.z) | (static_cast<u32>(expectedHalf4.w) << 16u));
}

TEST(EcsGraphics, MaterialTypedByteRangeDeduplicatesContent){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using MaterialTypedByteContentKey = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKey;
    using RangeMap = ::HashMap<
        MaterialTypedByteContentKey,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteRange,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKeyHasher,
        ::EqualTo<MaterialTypedByteContentKey>,
        NWB::Core::Alloc::ScratchArena
    >;

    ByteVector uploadBytes{scratchArena};
    RangeMap ranges(
        0,
        NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKeyHasher(),
        ::EqualTo<MaterialTypedByteContentKey>(),
        scratchArena
    );

    ByteVector firstBytes{scratchArena};
    firstBytes.push_back(1u);
    firstBytes.push_back(2u);
    firstBytes.push_back(3u);
    firstBytes.push_back(4u);

    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange firstRange;
    EXPECT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        firstBytes,
        firstRange
    ));
    EXPECT_EQ(firstRange.byteOffset, 0u);
    EXPECT_EQ(firstRange.byteCount, 4u);
    EXPECT_EQ(uploadBytes.size(), 4u);

    ByteVector duplicateBytes{scratchArena};
    duplicateBytes.assign(firstBytes.begin(), firstBytes.end());
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange duplicateRange;
    EXPECT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        duplicateBytes,
        duplicateRange
    ));
    EXPECT_EQ(duplicateRange.byteOffset, firstRange.byteOffset);
    EXPECT_EQ(duplicateRange.byteCount, firstRange.byteCount);
    EXPECT_EQ(uploadBytes.size(), 4u);

    ByteVector secondBytes{scratchArena};
    secondBytes.push_back(1u);
    secondBytes.push_back(2u);
    secondBytes.push_back(3u);
    secondBytes.push_back(5u);
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange secondRange;
    EXPECT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        secondBytes,
        secondRange
    ));
    EXPECT_EQ(secondRange.byteOffset, 4u);
    EXPECT_EQ(secondRange.byteCount, 4u);
    EXPECT_EQ(uploadBytes.size(), 8u);
    EXPECT_EQ(ranges.size(), 2u);

    ByteVector emptyBytes{scratchArena};
    NWB::Impl::ECSRenderDetail::MaterialTypedByteRange emptyRange;
    EXPECT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
        uploadBytes,
        ranges,
        emptyBytes,
        emptyRange
    ));
    EXPECT_EQ(emptyRange.byteOffset, 0u);
    EXPECT_EQ(emptyRange.byteCount, 0u);
    EXPECT_EQ(uploadBytes.size(), 8u);

    for(u32 instanceIndex = 0u; instanceIndex < 128u; ++instanceIndex){
        ByteVector overrideBytes{scratchArena};
        const u8 packedValue = static_cast<u8>(64u + (instanceIndex % 32u));
        overrideBytes.push_back(packedValue);
        overrideBytes.push_back(static_cast<u8>(packedValue + 1u));
        overrideBytes.push_back(static_cast<u8>(packedValue + 2u));
        overrideBytes.push_back(static_cast<u8>(packedValue + 3u));

        NWB::Impl::ECSRenderDetail::MaterialTypedByteRange stressRange;
        EXPECT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(
            uploadBytes,
            ranges,
            overrideBytes,
            stressRange
        ));
        EXPECT_EQ(stressRange.byteCount, 4u);
    }
    EXPECT_EQ(ranges.size(), 34u);
    EXPECT_EQ(uploadBytes.size(), 136u);

    NWB::Impl::ECSRenderDetail::MaterialTypedInstanceRanges instanceRange;
    instanceRange.constantRange = firstRange;
    instanceRange.mutableRange = secondRange;
    const NWB::Impl::InstanceGpuData gpuData = NWB::Impl::ECSRenderDetail::BuildInstanceGpuData(nullptr, instanceRange);
    EXPECT_EQ(gpuData.translation.w, secondRange.byteOffset);
#if defined(NWB_DEBUG)
    NWB::Impl::ECSRenderDetail::MaterialTypedInstanceRangeVector instanceRanges{scratchArena};
    instanceRanges.push_back(instanceRange);
    NWB::Impl::ECSRenderDetail::AssertMaterialTypedUploadRanges(
        instanceRanges,
        uploadBytes
    );
#endif
}

TEST(EcsGraphics, CsgReceiverRangeCarriesMaterialSurfaceContext){
    const NWB::Impl::CsgReceiverRangeGpuData defaultRange;
    EXPECT_EQ(defaultRange.surfaceDispatchId, Limit<u32>::s_Max);
    EXPECT_EQ(defaultRange.materialConstantByteOffset, 0u);
    EXPECT_EQ(defaultRange.meshInstanceIndex, 0u);
    EXPECT_EQ(defaultRange.materialContextPadding, 0u);

    NWB::Impl::CsgReceiverRangeGpuData range;
    range.surfaceDispatchId = 19u;
    range.materialConstantByteOffset = 96u;
    range.meshInstanceIndex = 7u;

    EXPECT_EQ(range.surfaceDispatchId, 19u);
    EXPECT_EQ(range.materialConstantByteOffset, 96u);
    EXPECT_EQ(range.meshInstanceIndex, 7u);
}

static NWB::Impl::SkeletonJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0].w = x;
    joint.rows[1].w = y;
    joint.rows[2].w = z;
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeZHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeXHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[1] = Float4(0.0f, -1.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeYHalfTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    joint.rows[2] = Float4(0.0f, 0.0f, -1.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeZQuarterTurnJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0] = Float4(0.0f, 1.0f, 0.0f, 0.0f);
    joint.rows[1] = Float4(-1.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkeletonJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

static NWB::Impl::SkinInfluence4 MakeSingleJointSkin(const u16 joint){
    NWB::Impl::SkinInfluence4 skin{};
    skin.joint[0] = joint;
    skin.weight.x = 1.0f;
    return skin;
}

static void AssignSingleJointSkin(NWB::Impl::MeshSkinningRuntimeInstance& instance, const u16 joint){
    instance.meshClass = NWB::Core::Mesh::MeshClass::Skinned;
    instance.skin.assign(instance.restPositions.size(), MakeSingleJointSkin(joint));
    instance.skeletonJointCount = Max(instance.skeletonJointCount, static_cast<u32>(joint) + 1u);
}

static void AppendRuntimeVertex(NWB::Impl::MeshSkinningRuntimeInstance& instance, const Float3U& position, const f32 u){
    instance.restPositions.push_back(position);
    instance.restNormals.push_back(MakeHalf4U(0.0f, 0.0f, 1.0f, 0.0f));
    instance.restTangents.push_back(MakeHalf4U(1.0f, 0.0f, 0.0f, 1.0f));
    instance.uv0.push_back(Float2U(u, 0.0f));
    instance.colors.push_back(MakeHalf4U(1.0f, 1.0f, 1.0f, 1.0f));
}

static NWB::Impl::MeshSkinningRuntimeInstance MakeTriangleInstance(){
    NWB::Impl::MeshSkinningRuntimeInstance instance(NWB::Tests::TestDetail::Arena());
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
    NWB::Tests::AppendSequentialMeshletRefs(
        instance.restPositions.size(),
        meshletPositionStreamRefs,
        meshletAttributeStreamRefs,
        instance.meshletLocalVertexRefs
    );
    for(usize vertexIndex = 0u; vertexIndex < instance.restPositions.size(); ++vertexIndex){
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

static NWB::Impl::SkeletonJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

static void CheckJointRotationQuaternion(
    const SIMDMatrix& joint,
    const f32 x,
    const f32 y,
    const f32 z,
    const f32 w){
    SIMDVector quaternion = QuaternionIdentity();
    ASSERT_TRUE(NWB::Impl::SkeletonRuntime::TryBuildJointRotationQuaternion(joint, quaternion));
    EXPECT_TRUE(NearlyEqual(VectorGetX(quaternion), x));
    EXPECT_TRUE(NearlyEqual(VectorGetY(quaternion), y));
    EXPECT_TRUE(NearlyEqual(VectorGetZ(quaternion), z));
    EXPECT_TRUE(NearlyEqual(VectorGetW(quaternion), w));
}

TEST(EcsGraphics, JointRotationQuaternionBuildsColumnVectorRotations){
    constexpr f32 s_HalfSqrtTwo = 0.70710678118f;

    CheckJointRotationQuaternion(LoadFloat(MakeIdentityJointMatrix()), 0.0f, 0.0f, 0.0f, 1.0f);
    CheckJointRotationQuaternion(LoadFloat(MakeZQuarterTurnJointMatrix()), 0.0f, 0.0f, s_HalfSqrtTwo, s_HalfSqrtTwo);
    CheckJointRotationQuaternion(LoadFloat(MakeXHalfTurnJointMatrix()), 1.0f, 0.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(LoadFloat(MakeYHalfTurnJointMatrix()), 0.0f, 1.0f, 0.0f, 0.0f);
    CheckJointRotationQuaternion(LoadFloat(MakeZHalfTurnJointMatrix()), 0.0f, 0.0f, 1.0f, 0.0f);

    SIMDVector quaternion = QuaternionIdentity();
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::TryBuildJointRotationQuaternion(
            LoadFloat(MakeNonUniformScaleJointMatrix()),
            quaternion
        ));
}

static NWB::Impl::SkeletonPoseComponent MakeTwoJointSkeletonPose(
    const NWB::Impl::SkeletonJointMatrix& rootJoint,
    const NWB::Impl::SkeletonJointMatrix& childJoint){
    NWB::Impl::SkeletonPoseComponent pose(NWB::Tests::TestDetail::Arena());
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

TEST(EcsGraphics, SkeletonPoseBuildsHierarchicalPalette){
    NWB::Impl::SkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::SkeletonJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    ASSERT_TRUE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
    EXPECT_EQ(skinningMode, NWB::Impl::SkeletonSkinningMode::LinearBlend);
    ASSERT_EQ(resolvedJoints.size(), 2u);
    EXPECT_TRUE(NearlyEqual(resolvedJoints[0u].rows[0].w, 1.0f));
    EXPECT_TRUE(NearlyEqual(resolvedJoints[0u].rows[1].w, 0.0f));
    EXPECT_TRUE(NearlyEqual(resolvedJoints[1u].rows[0].w, 1.0f));
    EXPECT_TRUE(NearlyEqual(resolvedJoints[1u].rows[1].w, 2.0f));

    pose.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    ASSERT_TRUE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
    EXPECT_EQ(skinningMode, NWB::Impl::SkeletonSkinningMode::DualQuaternion);

    pose.parentJoints[1u] = 1u;
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
}
TEST(EcsGraphics, MeshSkinningPayloadValidatesSkeletonAndPalette){
    NWB::Impl::MeshSkinningRuntimeInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::SkeletonJointPaletteComponent joints(NWB::Tests::TestDetail::Arena());
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::MeshSkinningInfluenceGpu> skinInfluences;
    Vector<NWB::Impl::SkeletonJointMatrix> jointMatrices;
    EXPECT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices));
    EXPECT_EQ(skinInfluences.size(), instance.skin.size());
    EXPECT_EQ(jointMatrices.size(), 1u);
    EXPECT_EQ(skinInfluences[0u].joint[0u], 0u);
    EXPECT_TRUE(NearlyEqual(skinInfluences[0u].weight.x, 1.0f));

    instance.inverseBindMatrices.push_back(MakeTranslationJointMatrix(-0.25f, 0.0f, 0.0f));
    joints.joints[0u] = MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(instance, &joints, skinInfluences, jointMatrices));
    EXPECT_EQ(jointMatrices.size(), 1u);
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[0].w, 0.75f));
    joints.joints[0u] = MakeIdentityJointMatrix();

    NWB::Impl::MeshSkinningRuntimeInstance dualQuaternionInstance = MakeTriangleInstance();
    AssignSingleJointSkin(dualQuaternionInstance, 0u);
    dualQuaternionInstance.handle.value = instance.handle.value;
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeTranslationJointMatrix(2.0f, 4.0f, 6.0f);
    EXPECT_TRUE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(dualQuaternionInstance, &joints, skinInfluences, jointMatrices));
    EXPECT_EQ(jointMatrices.size(), 1u);
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[0].x, 0.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[0].y, 0.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[0].z, 0.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[0].w, 1.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[1].x, 1.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[1].y, 2.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[1].z, 3.0f));
    EXPECT_TRUE(NearlyEqual(jointMatrices[0u].rows[1].w, 0.0f));
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::LinearBlend;
    joints.joints[0u] = MakeIdentityJointMatrix();

#if defined(NWB_FINAL)
    CapturingLogger runtimeValidationLogger;
    NWB::Core::Common::LoggerRegistrationGuard runtimeValidationLoggerRegistrationGuard(runtimeValidationLogger);

    NWB::Impl::MeshSkinningRuntimeInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = 2u;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, ::Float34Identity());
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(outsidePalette, &joints, skinInfluences, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(nonAffineJoint, &joints, skinInfluences, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinPayload(
            scaledDualQuaternionJoint,
            &joints,
            skinInfluences,
            jointMatrices
        ));

    EXPECT_EQ(runtimeValidationLogger.errorCount(), 3u);
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette count")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("failed dual-quaternion payload build")));
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

