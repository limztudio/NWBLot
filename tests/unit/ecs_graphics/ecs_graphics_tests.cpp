// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/resource_names.h>
#include <impl/ecs_mesh/skinning/skin_payload.h>
#include <impl/ecs_mesh/skinning/submission_state.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/ecs_test_world.h>
#include <tests/common/meshlet_ref_test_data.h>
#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <core/common/module.h>
#include <core/ecs/module.h>
#include <core/graphics/rhi/device.h>
#include <core/mesh/classification.h>
#include <impl/ecs_mesh/components.h>
#include <impl/ecs_skeleton/components.h>
#include <impl/ecs_mesh/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_csg/module.h>
#include <impl/ecs_render/csg/renderer_csg_types.h>
#include <impl/ecs_render/avboit/avboit.h>
#include <impl/ecs_render/material/material_typed_private.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/shared/renderer_state.h>
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


TEST(EcsGraphics, DeprecatedFeatureSlotsKeepUnsupportedAbiGaps){
    // SamplerFeedback and VirtualResources are retired, but external users still rely on
    // their enum positions. Keep the surrounding ordinals here so either ABI gap cannot move.
    EXPECT_EQ(static_cast<u32>(NWB::Core::Feature::SamplerFeedback), 13u);
    EXPECT_EQ(static_cast<u32>(NWB::Core::Feature::ShaderExecutionReordering), 14u);
    EXPECT_EQ(static_cast<u32>(NWB::Core::Feature::VirtualResources), 19u);
    EXPECT_EQ(static_cast<u32>(NWB::Core::Feature::WaveLaneCountMinMax), 20u);
    EXPECT_EQ(static_cast<u32>(NWB::Core::Feature::kCount), 23u);
}

TEST(EcsGraphics, RayTracingStateInvalidationClearsSurfelAgeFreePipelineFailureLatch){
    NWB::Tests::TestArena<> testArena;
    NWB::Impl::RendererRayTracingState state(testArena.arena);
    state.m_surfelAgeFreePipelineFailed = true;

    state.invalidateResources();

    EXPECT_FALSE(state.m_surfelAgeFreePipelineFailed);
}

TEST(EcsGraphics, AvboitPushConstantsCarryHdrPolicyWithoutChangingCoverageData){
    NWB::Impl::AvboitFrameTargets targets;
    targets.fullWidth = 1920u;
    targets.fullHeight = 1080u;
    targets.lowWidth = 480u;
    targets.lowHeight = 270u;
    targets.virtualSliceCount = 128u;
    targets.physicalSliceCount = 64u;
    targets.deferredSlotsBufferDescriptor = NWB::Core::GpuDescriptorHandle::make(
        NWB::Core::GpuDescriptorClass::UniformBuffer,
        23u
    );

    const NWB::Impl::RendererAvboitPushConstants sdr = NWB::Impl::BuildRendererAvboitPushConstants(targets, false);
    const NWB::Impl::RendererAvboitPushConstants hdr10 = NWB::Impl::BuildRendererAvboitPushConstants(targets, true);

    EXPECT_FLOAT_EQ(sdr.params.raw[NWB_AVBOIT_PUSH_PARAMS_PRESENTATION_MODE], NWB_AVBOIT_PRESENTATION_SDR);
    EXPECT_FLOAT_EQ(hdr10.params.raw[NWB_AVBOIT_PUSH_PARAMS_PRESENTATION_MODE], NWB_AVBOIT_PRESENTATION_HDR10);
    EXPECT_EQ(sdr.params.raw[NWB_AVBOIT_PUSH_PARAMS_EXTINCTION_FIXED_SCALE], hdr10.params.raw[NWB_AVBOIT_PUSH_PARAMS_EXTINCTION_FIXED_SCALE]);
    EXPECT_EQ(sdr.params.raw[NWB_AVBOIT_PUSH_PARAMS_SELF_OCCLUSION_SLICE_BIAS], hdr10.params.raw[NWB_AVBOIT_PUSH_PARAMS_SELF_OCCLUSION_SLICE_BIAS]);
    EXPECT_EQ(sdr.heapSlots[NWB_AVBOIT_PUSH_HEAP_SLOT_DEFERRED_BINDLESS_RESOURCES], 23u);
    EXPECT_EQ(hdr10.heapSlots[NWB_AVBOIT_PUSH_HEAP_SLOT_DEFERRED_BINDLESS_RESOURCES], 23u);
}


TEST(EcsGraphics, RuntimeResourceNameBuilderMatchesFormattedSuffix){
    NWB::Tests::TestArena<> arena;
    const Name sourceName("project/meshes/mesh_skinning_source");
    const auto suffix = NWB::Impl::BuildRuntimeResourceSuffix(arena.arena, 42u, 17u, "mesh_skinning_ranges");
    EXPECT_EQ(AStringView(suffix.data(), suffix.size()), AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));

    const Name builtName = NWB::Impl::DeriveRuntimeResourceName(sourceName, 42u, 17u, "mesh_skinning_ranges");
    const Name formattedName = DeriveName(sourceName, AStringView(":runtime_42_revision_17_mesh_skinning_ranges"));
    EXPECT_EQ(builtName, formattedName);
}

TEST(EcsGraphics, MeshSkinningSubmissionCommitRejectKeepsPoseAndSelectorPending){
    NWB::Impl::RuntimeMeshDirtyFlags dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::AttributesDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    bool bindlessResourceSlotsUploaded = false;
    NWB::Impl::MeshSkinningSubmissionCommit commit;
    commit.editRevision = 17u;
    commit.handledDirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    commit.bindlessResourceSlotsUploadRecorded = true;

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(
        false,
        17u,
        dirtyFlags,
        bindlessResourceSlotsUploaded,
        commit
    );

    EXPECT_EQ(dirtyFlags, static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::AttributesDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    ));
    EXPECT_FALSE(bindlessResourceSlotsUploaded);

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(
        true,
        17u,
        dirtyFlags,
        bindlessResourceSlotsUploaded,
        commit
    );

    EXPECT_EQ(dirtyFlags, NWB::Impl::RuntimeMeshDirtyFlag::AttributesDirty);
    EXPECT_TRUE(bindlessResourceSlotsUploaded);
}

TEST(EcsGraphics, MeshSkinningSubmissionCommitPreservesNewerEditRevision){
    NWB::Impl::RuntimeMeshDirtyFlags dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    bool bindlessResourceSlotsUploaded = false;
    NWB::Impl::MeshSkinningSubmissionCommit commit;
    commit.editRevision = 17u;
    commit.handledDirtyFlags = dirtyFlags;
    commit.bindlessResourceSlotsUploadRecorded = true;

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(
        true,
        18u,
        dirtyFlags,
        bindlessResourceSlotsUploaded,
        commit
    );

    EXPECT_EQ(dirtyFlags, static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    ));
    EXPECT_FALSE(bindlessResourceSlotsUploaded);
}

TEST(EcsGraphics, CsgNonFiniteReceiverBoundsDisableAabbCulling){
    NWB::Impl::CsgReceiverCpuBounds posedReceiverBounds;
    posedReceiverBounds.minBounds = Float3Int(-1.0f, -1.0f, -1.0f, NWB::Impl::s_CsgBoundsValidFlag);
    posedReceiverBounds.maxBounds = Float3Int(1.0f, 1.0f, 1.0f, 0);

    EXPECT_TRUE(posedReceiverBounds.valid());
    EXPECT_FALSE(posedReceiverBounds.finite());
    EXPECT_FALSE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));

    posedReceiverBounds.minBounds.w |= NWB::Impl::s_CsgBoundsFiniteFlag;
    EXPECT_TRUE(posedReceiverBounds.finite());
    EXPECT_TRUE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));
}


using TestWorld = NWB::Tests::EcsTestWorld;

TEST(EcsGraphics, SceneBvhTransparentSubtreeClassificationPropagatesToRoot){
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    using PrimitiveVector = ::Vector<NWB::Impl::SceneBvhPrimitiveCalculation, NWB::Core::Alloc::ScratchArena>;
    using IndexVector = ::Vector<u32, NWB::Core::Alloc::ScratchArena>;
    using NodeVector = ::Vector<NWB::Impl::SceneBvhNodeCalculation, NWB::Core::Alloc::ScratchArena>;

    PrimitiveVector primitives{ scratchArena };
    const bool transparent[] = { false, true, false };
    for(u32 index = 0u; index < 3u; ++index){
        NWB::Impl::SceneBvhPrimitiveCalculation primitive;
        const f32 x = static_cast<f32>(index) * 2.0f;
        primitive.aabbMin = VectorSet(x, 0.0f, 0.0f, 0.0f);
        primitive.aabbMax = VectorSet(x + 1.0f, 1.0f, 1.0f, 0.0f);
        primitive.centroid = VectorSet(x + 0.5f, 0.5f, 0.5f, 0.0f);
        primitive.transparentOccluder = transparent[index];
        primitives.push_back(primitive);
    }

    IndexVector indices{ scratchArena };
    indices.push_back(0u);
    indices.push_back(1u);
    indices.push_back(2u);
    NodeVector nodes{ scratchArena };
    const u32 root = NWB::Impl::__hidden_raytracing_system::BuildSceneBvhNode(
        indices.data(),
        0u,
        static_cast<u32>(indices.size()),
        primitives.data(),
        nodes
    );

    ASSERT_EQ(root, 0u);
    ASSERT_EQ(nodes.size(), 5u);
    EXPECT_TRUE(nodes[root].containsTransparentOccluder);
    EXPECT_LT(nodes[root].leftChild, nodes.size());
    EXPECT_LT(nodes[root].rightChild, nodes.size());

    u32 transparentLeafCount = 0u;
    for(const NWB::Impl::SceneBvhNodeCalculation& node : nodes){
        if((node.leftChild & NWB_BVH_LEAF_FLAG) == 0u)
            continue;
        const u32 primitiveIndex = node.leftChild & ~NWB_BVH_LEAF_FLAG;
        ASSERT_LT(primitiveIndex, primitives.size());
        EXPECT_EQ(node.containsTransparentOccluder, primitives[primitiveIndex].transparentOccluder);
        transparentLeafCount += node.containsTransparentOccluder ? 1u : 0u;
    }
    EXPECT_EQ(transparentLeafCount, 1u);
}

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
    NWB_FATAL_ASSERT(meshletRefsEncoded);
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
    ASSERT_TRUE(MatrixTryBuildRigidRotationQuaternion(
        joint,
        NWB::Impl::SkeletonRuntime::s_AffineEpsilon,
        NWB::Impl::SkeletonRuntime::s_RigidJointEpsilon,
        quaternion
    ));
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
    EXPECT_FALSE(MatrixTryBuildRigidRotationQuaternion(
        LoadFloat(MakeNonUniformScaleJointMatrix()),
        NWB::Impl::SkeletonRuntime::s_AffineEpsilon,
        NWB::Impl::SkeletonRuntime::s_RigidJointEpsilon,
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

