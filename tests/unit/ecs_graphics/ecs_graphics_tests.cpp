// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/assets_mesh/meshlet_ref_codec.h>
#include <impl/assets_mesh/meshlet_payload_packing.h>
#include <impl/assets_mesh/skin_types.h>
#include <impl/assets_mesh/asset.h>

#include <core/common/log.h>

#include <global/binary.h>
#include <global/compile.h>
#include <global/limit.h>
#include <global/span.h>
#include <global/timer.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CapturingLogger = NWB::Tests::CapturingLogger;
using NWB::Tests::MakeTriangleIndices;
using NWB::Tests::NearlyEqual;
template<typename T>
using Vector = NWB::Tests::TestVector<T>;

inline constexpr Name s_ScratchArena("tests/ecs_graphics/scratch");


TEST(EcsGraphics, MissingOpticalAttachmentsDisableAuxiliarySamplingInBothPresentationModes){
    NWB::Impl::AvboitFrameTargets targets;
    targets.fullWidth = 1920u;
    targets.fullHeight = 1080u;
    targets.lowWidth = 480u;
    targets.lowHeight = 270u;
    targets.virtualSliceCount = 128u;
    targets.physicalSliceCount = 64u;
    targets.deferredSlotsBufferDescriptor = NWB::Core::GpuDescriptorHandle::Make(
        NWB::Core::GpuDescriptorClass::UniformBuffer,
        23u
    );

    const auto sdr = NWB::Impl::BuildRendererAvboitPushConstants(targets, false);
    const auto hdr10 = NWB::Impl::BuildRendererAvboitPushConstants(targets, true);
    EXPECT_FLOAT_EQ(sdr.params.raw[NWB_AVBOIT_PUSH_PARAMS_REFRACTION_CAPTURE], NWB_AVBOIT_REFRACTION_DISABLED);
    EXPECT_FLOAT_EQ(hdr10.params.raw[NWB_AVBOIT_PUSH_PARAMS_REFRACTION_CAPTURE], NWB_AVBOIT_REFRACTION_DISABLED);
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

TEST(EcsGraphics, MeshSkinningGraphOwnedDispatchCommitsSelectorWithAcceptedCompute){
    NWB::Impl::RuntimeMeshDirtyFlags dirtyFlags = static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    );
    // The immutable selector upload and compute dispatch share one graph packet. A rejected packet keeps both the
    // selector and deformation retryable; acceptance publishes both CPU facts together.
    bool bindlessResourceSlotsUploaded = false;
    NWB::Impl::MeshSkinningSubmissionCommit commit;
    commit.editRevision = 23u;
    commit.handledDirtyFlags = dirtyFlags;
    commit.bindlessResourceSlotsUploadRecorded = true;

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(
        false,
        23u,
        dirtyFlags,
        bindlessResourceSlotsUploaded,
        commit
    );

    EXPECT_EQ(dirtyFlags, static_cast<NWB::Impl::RuntimeMeshDirtyFlags>(
        NWB::Impl::RuntimeMeshDirtyFlag::SkinningInputDirty
        | NWB::Impl::RuntimeMeshDirtyFlag::MeshletBoundsDirty
    ));
    EXPECT_FALSE(bindlessResourceSlotsUploaded);

    NWB::Impl::ApplyMeshSkinningSubmissionCommit(
        true,
        23u,
        dirtyFlags,
        bindlessResourceSlotsUploaded,
        commit
    );

    EXPECT_EQ(dirtyFlags, NWB::Impl::RuntimeMeshDirtyFlag::None);
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

    EXPECT_FALSE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));

    posedReceiverBounds.minBounds.w |= NWB::Impl::s_CsgBoundsFiniteFlag;
    EXPECT_TRUE(NWB::Impl::CsgReceiverBoundsCanCull(posedReceiverBounds));
}


TEST(EcsGraphics, CsgCutterWorkBoundsIgnoreUntrustedReceiverAndKeepTrustedRejection){
    NWB::Tests::TestArena<> arena;
    NWB::Impl::CsgShapeRegistry registry(arena.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB::Impl::CsgShapeTypeInfo shape;
    ASSERT_TRUE(registry.findShapeType(NWB::Impl::s_CsgBoxShapeName, shape));
    NWB::Impl::CsgBoxShapeParameters parameters;
    parameters.halfExtents = Float4(0.25f, 0.125f, 0.5f, 0.f);
    const SIMDMatrix shapeToWorld = MatrixTranslation(0.5f, 0.25f, 2.f);
    NWB::Impl::CsgClipWorkBounds receiver;
    receiver.minBounds = VectorSet(-20.f, -20.f, -20.f, 0.f);
    receiver.maxBounds = VectorSet(-10.f, -10.f, -10.f, 0.f);
    NWB::Impl::CsgClipWorkBounds work;
    ASSERT_TRUE(work.resolveCutter(
        registry, shape, shapeToWorld, reinterpret_cast<const u8*>(&parameters), sizeof(parameters), receiver
    ));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(VectorGetX(work.minBounds), 0.25f);
    EXPECT_FLOAT_EQ(VectorGetY(work.minBounds), 0.125f);
    EXPECT_FLOAT_EQ(VectorGetZ(work.minBounds), 1.5f);
    EXPECT_FLOAT_EQ(VectorGetX(work.maxBounds), 0.75f);
    EXPECT_FLOAT_EQ(VectorGetY(work.maxBounds), 0.375f);
    EXPECT_FLOAT_EQ(VectorGetZ(work.maxBounds), 2.5f);
    receiver.valid = true;
    EXPECT_FALSE(work.resolveCutter(
        registry, shape, shapeToWorld, reinterpret_cast<const u8*>(&parameters), sizeof(parameters), receiver
    ));
    EXPECT_FALSE(work.valid);
    receiver.minBounds = VectorSet(0.5f, 0.25f, 1.75f, 0.f);
    receiver.maxBounds = VectorSet(1.f, 1.f, 3.f, 0.f);
    ASSERT_TRUE(work.resolveCutter(
        registry, shape, shapeToWorld, reinterpret_cast<const u8*>(&parameters), sizeof(parameters), receiver
    ));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(VectorGetX(work.minBounds), 0.5f);
    EXPECT_FLOAT_EQ(VectorGetY(work.minBounds), 0.25f);
    EXPECT_FLOAT_EQ(VectorGetZ(work.minBounds), 1.75f);
    EXPECT_FLOAT_EQ(VectorGetX(work.maxBounds), 0.75f);
    EXPECT_FLOAT_EQ(VectorGetY(work.maxBounds), 0.375f);
    EXPECT_FLOAT_EQ(VectorGetZ(work.maxBounds), 2.5f);
}

TEST(EcsGraphics, CsgCutterWorkBoundsKeepUnknownReceiverFallbacksAndResetPreviousBounds){
    NWB::Tests::TestArena<> arena;
    NWB::Impl::CsgShapeRegistry registry(arena.arena);
    ASSERT_TRUE(NWB::Impl::RegisterBuiltInCsgShapeTypes(registry));
    NWB::Impl::CsgShapeTypeInfo shape;
    ASSERT_TRUE(registry.findShapeType(NWB::Impl::s_CsgBoxShapeName, shape));
    NWB::Impl::CsgClipWorkBounds receiver;
    NWB::Impl::CsgClipWorkBounds work;
    ASSERT_TRUE(work.resolveCutter(registry, shape, MatrixIdentity(), nullptr, 0u, receiver));
    ASSERT_TRUE(work.valid);

    NWB::Impl::CsgBoxShapeParameters invalidParameters;
    invalidParameters.halfExtents.x = 0.f;
    EXPECT_TRUE(work.resolveCutter(
        registry, shape, MatrixIdentity(), reinterpret_cast<const u8*>(&invalidParameters), sizeof(invalidParameters), receiver
    ));
    EXPECT_FALSE(work.valid);
    receiver.minBounds = VectorReplicate(-2.f);
    receiver.maxBounds = VectorReplicate(2.f);
    receiver.valid = true;
    EXPECT_FALSE(work.resolveCutter(
        registry, shape, MatrixIdentity(), reinterpret_cast<const u8*>(&invalidParameters), sizeof(invalidParameters), receiver
    ));
    EXPECT_FALSE(work.valid);

    ASSERT_TRUE(registry.findShapeType(NWB::Impl::s_CsgPlaneShapeName, shape));
    ASSERT_TRUE(work.resolveCutter(registry, shape, MatrixIdentity(), nullptr, 0u, receiver));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(VectorGetX(work.minBounds), -2.f);
    EXPECT_FLOAT_EQ(VectorGetX(work.maxBounds), 2.f);
    receiver.valid = false;
    EXPECT_TRUE(work.resolveCutter(registry, shape, MatrixIdentity(), nullptr, 0u, receiver));
    EXPECT_FALSE(work.valid);

    ASSERT_TRUE(registry.findShapeType(NWB::Impl::s_CsgBoxShapeName, shape));
    NWB::Impl::CsgShapeTypeDesc custom = shape.desc;
    custom.name = Name("tests/csg/finite_custom_cutter");
    custom.shaderModule = Name("tests/csg/finite_custom_cutter_eval");
    custom.shaderModuleInclude = ACompactString("tests/csg/finite_custom_cutter_eval.slangi");
    NWB::Impl::CsgShapeTypeId customId = NWB::Impl::s_InvalidCsgShapeTypeId;
    ASSERT_TRUE(registry.registerShapeType(custom, customId));
    ASSERT_TRUE(registry.findShapeType(customId, shape));
    EXPECT_TRUE(work.resolveCutter(registry, shape, MatrixIdentity(), nullptr, 0u, receiver));
    EXPECT_FALSE(work.valid);
    receiver.valid = true;
    ASSERT_TRUE(work.resolveCutter(registry, shape, MatrixIdentity(), nullptr, 0u, receiver));
    ASSERT_TRUE(work.valid);
    EXPECT_FLOAT_EQ(VectorGetX(work.minBounds), -1.f);
    EXPECT_FLOAT_EQ(VectorGetX(work.maxBounds), 1.f);
}


TEST(EcsGraphics, CsgReceiverWorkRegionKeepsAbsoluteBoundsForSubrectDispatch){
    NWB::Impl::CsgFrameWorkRegion region;
    region.expandWorldBounds(
        MatrixIdentity(), VectorSet(-0.5f, -0.25f, 1.f, 0.f), VectorSet(0.5f, 0.25f, 2.f, 0.f), 1001u, 701u
    );
    ASSERT_TRUE(region.bounded());
    const NWB::Core::Rect rect = region.resolveRect(1001u, 701u);
    EXPECT_EQ(rect.minX, 248);
    EXPECT_EQ(rect.maxX, 753);
    EXPECT_EQ(rect.minY, 260);
    EXPECT_EQ(rect.maxY, 441);
}


TEST(EcsGraphics, CsgReceiverWorkRegionFallsBackConservativelyAndClampsLargeProjections){
    NWB::Impl::CsgFrameWorkRegion unknown;
    const NWB::Core::Rect unknownRect = unknown.resolveRect(1001u, 701u);
    EXPECT_EQ(unknownRect.minX, 0);
    EXPECT_EQ(unknownRect.minY, 0);
    EXPECT_EQ(unknownRect.maxX, 1001);
    EXPECT_EQ(unknownRect.maxY, 701);

    NWB::Impl::CsgFrameWorkRegion crossing;
    crossing.expandWorldBounds(
        MatrixPerspectiveFovLH(s_PIDIV2, 1.f, 0.1f, 100.f),
        VectorSet(-1.f, -1.f, -1.f, 0.f), VectorSet(1.f, 1.f, 1.f, 0.f), 1001u, 701u
    );
    EXPECT_TRUE(crossing.fullFrame);
    EXPECT_EQ(crossing.resolveRect(1001u, 701u).maxX, 1001);

    NWB::Impl::CsgFrameWorkRegion invalid;
    invalid.expandWorldBounds(MatrixIdentity(), VectorSet(1.f, 1.f, 1.f, 0.f), VectorZero(), 1001u, 701u);
    EXPECT_TRUE(invalid.fullFrame);
    NWB::Impl::CsgFrameWorkRegion nonfinite;
    nonfinite.expandWorldBounds(
        MatrixIdentity(), VectorSet(-Limit<f32>::s_Infinity, -1.f, 1.f, 0.f),
        VectorSet(1.f, 1.f, 2.f, 0.f), 1001u, 701u
    );
    EXPECT_TRUE(nonfinite.fullFrame);

    NWB::Impl::CsgFrameWorkRegion large;
    large.expandWorldBounds(
        MatrixIdentity(), VectorSet(-1e20f, -0.25f, 1.f, 0.f), VectorSet(0.f, 0.25f, 2.f, 0.f), 1000u, 600u
    );
    ASSERT_TRUE(large.bounded());
    EXPECT_EQ(large.minX, 0u);
    EXPECT_EQ(large.maxX, 502u);
    large.expandFull();
    EXPECT_EQ(large.resolveRect(1000u, 600u).maxX, 1000);
}


using TestWorld = NWB::Tests::EcsTestWorld;


TEST(EcsGraphics, MissingMeshClearsPreviousResolution){
    TestWorld testWorld;
    auto& meshSystem = testWorld.world.addSystem<NWB::Impl::MeshSystem>(testWorld.world);

    auto entity = testWorld.world.createEntity();
    auto& mesh = entity.addComponent<NWB::Impl::MeshComponent>();
    mesh.mesh.virtualPath = Name("project/meshes/static_mesh");

    NWB::Core::Assets::AssetRef<NWB::Impl::Mesh> resolvedMesh;
    EXPECT_TRUE(meshSystem.resolveMesh(entity.id(), resolvedMesh));

    auto missingMeshEntity = testWorld.world.createEntity();
    EXPECT_FALSE(meshSystem.resolveMesh(missingMeshEntity.id(), resolvedMesh));
    EXPECT_FALSE(resolvedMesh.valid());
}

TEST(EcsGraphics, MaterialTypedByteRangeRepeatedHitsAvoidScratchAllocations){
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;
    using ByteRange = NWB::Impl::ECSRenderDetail::MaterialTypedByteRange;

    NWB::Core::Alloc::ScratchArena sourceScratch(Name("tests/material_typed_dedup/hit_sources"));
    ByteVector sourceBytes(sourceScratch);
    sourceBytes.resize(4096u);
    for(usize index = 0u; index < sourceBytes.size(); ++index)
        sourceBytes[index] = static_cast<u8>((index * 31u) & 255u);

    const Array<usize, 2> byteCounts = {512u, 4096u};
    for(const usize byteCount : byteCounts){
        NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
        ByteVector uploadBytes(scratchArena);
        uploadBytes.reserve(byteCount);
        RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
        ranges.reserve(1u);
        const Span<const u8> bytes(sourceBytes.data(), byteCount);
        ByteRange firstRange;
        ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, bytes, firstRange));
        for(usize warmup = 0u; warmup < 64u; ++warmup){
            ByteRange range;
            ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, bytes, range));
        }

        const ArenaMemoryStats before = scratchArena.memoryStats();
        for(usize lookup = 0u; lookup < 256u; ++lookup){
            ByteRange range;
            ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, bytes, range));
            EXPECT_EQ(range.byteOffset, firstRange.byteOffset);
            EXPECT_EQ(range.byteCount, firstRange.byteCount);
        }
        const Span<const u8> emptyBytes;
        ByteRange emptyRange{4u, 4u};
        ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, emptyBytes, emptyRange));
        const ArenaMemoryStats after = scratchArena.memoryStats();
        EXPECT_EQ(after.allocationCount, before.allocationCount);
        EXPECT_EQ(after.reallocationCount, before.reallocationCount);
        EXPECT_EQ(after.deallocationCount, before.deallocationCount);
        EXPECT_EQ(after.usedBytes, before.usedBytes);
        EXPECT_EQ(after.peakUsedBytes, before.peakUsedBytes);
        EXPECT_EQ(after.reservedBytes, before.reservedBytes);
        EXPECT_EQ(emptyRange.byteOffset, 0u);
        EXPECT_EQ(emptyRange.byteCount, 0u);
        EXPECT_EQ(uploadBytes.size(), byteCount);
        EXPECT_EQ(ranges.size(), 1u);
    }
}

TEST(EcsGraphics, MaterialTypedByteRangeHashCollisionRetainsOwnedBytes){
    using ByteKey = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentKey;
    using ByteLookup = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentLookup;
    using ByteRange = NWB::Impl::ECSRenderDetail::MaterialTypedByteRange;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;

    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
    ranges.reserve(2u);
    constexpr u64 s_CollisionHash = 41u;
    const Array<u8, 4> originalFirstBytes = {1u, 2u, 3u, 4u};
    const Array<u8, 4> secondBytes = {1u, 2u, 3u, 5u};
    {
        Array<u8, 4> firstBytes = originalFirstBytes;
        const ByteLookup lookup{s_CollisionHash, Span<const u8>(firstBytes.data(), firstBytes.size())};
        ASSERT_TRUE(ranges.emplace(ByteKey(scratchArena, lookup), ByteRange{0u, 4u}).second);
        firstBytes.fill(0u);
    }

    // Equal hash and length must still distinguish content; the first source has already expired.
    const ByteLookup firstLookup{s_CollisionHash, Span<const u8>(originalFirstBytes.data(), originalFirstBytes.size())};
    const ByteLookup secondLookup{s_CollisionHash, Span<const u8>(secondBytes.data(), secondBytes.size())};
    const auto firstFound = ranges.find(firstLookup);
    ASSERT_NE(firstFound, ranges.end());
    EXPECT_EQ(firstFound.value().byteOffset, 0u);
    EXPECT_EQ(ranges.find(secondLookup), ranges.end());
    ASSERT_TRUE(ranges.emplace(ByteKey(scratchArena, secondLookup), ByteRange{4u, 4u}).second);
    const auto secondFound = ranges.find(secondLookup);
    ASSERT_NE(secondFound, ranges.end());
    EXPECT_EQ(secondFound.value().byteOffset, 4u);
    EXPECT_EQ(ranges.size(), 2u);
}

TEST(EcsGraphics, MaterialTypedByteRangeUploadAliasSurvivesGrowthAndMutation){
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using ByteRange = NWB::Impl::ECSRenderDetail::MaterialTypedByteRange;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;

    NWB::Core::Alloc::ScratchArena sourceScratch(Name("tests/material_typed_dedup/alias_source"));
    ByteVector expectedBytes(sourceScratch);
    NWB::Core::Alloc::ScratchArena scratchArena(s_ScratchArena);
    ByteVector uploadBytes(scratchArena);
    uploadBytes.reserve(32u);
    uploadBytes.resize(uploadBytes.capacity());
    for(usize index = 0u; index < uploadBytes.size(); ++index)
        uploadBytes[index] = static_cast<u8>((index * 31u) & 255u);
    expectedBytes.assign(uploadBytes.begin(), uploadBytes.end());
    const usize initialCapacity = uploadBytes.capacity();
    const Span<const u8> aliasedBytes(uploadBytes.data(), uploadBytes.size());
    RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratchArena);
    ranges.reserve(1u);
    ByteRange appendedRange;
    ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, aliasedBytes, appendedRange));
    ASSERT_GT(uploadBytes.capacity(), initialCapacity);
    ASSERT_EQ(appendedRange.byteCount, expectedBytes.size());
    ASSERT_EQ(uploadBytes.size(), expectedBytes.size() * 2u);
    EXPECT_EQ(NWB_MEMCMP(uploadBytes.data() + appendedRange.byteOffset, expectedBytes.data(), expectedBytes.size()), 0);

    // Mutating the original upload bytes cannot change the retained dedup key.
    for(usize index = 0u; index < expectedBytes.size(); ++index)
        uploadBytes[index] ^= 255u;
    const usize uploadByteCount = uploadBytes.size();
    ByteRange foundRange;
    ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, expectedBytes, foundRange));
    EXPECT_EQ(foundRange.byteOffset, appendedRange.byteOffset);
    EXPECT_EQ(foundRange.byteCount, appendedRange.byteCount);
    EXPECT_EQ(uploadBytes.size(), uploadByteCount);
    EXPECT_EQ(ranges.size(), 1u);
}


static void BenchmarkMaterialTypedRanges(
    const usize byteCount,
    const usize valueCount,
    const usize lookupCount,
    const usize iterations,
    const bool repeatedHits
){
    using ByteVector = ::Vector<u8, NWB::Core::Alloc::ScratchArena>;
    using RangeMap = NWB::Impl::ECSRenderDetail::MaterialTypedByteContentRangeMap;
    using ByteRange = NWB::Impl::ECSRenderDetail::MaterialTypedByteRange;

    NWB::Core::Alloc::ScratchArena setupScratch(Name("tests/material_typed_dedup/benchmark_setup"));
    ByteVector sourceBytes(setupScratch);
    sourceBytes.resize(byteCount * valueCount);
    for(usize valueIndex = 0u; valueIndex < valueCount; ++valueIndex){
        const usize offset = valueIndex * byteCount;
        for(usize byteIndex = 0u; byteIndex < byteCount; ++byteIndex)
            sourceBytes[offset + byteIndex] = static_cast<u8>((byteIndex * 31u + valueIndex) & 255u);
        const u64 identity = valueIndex;
        NWB_MEMCPY(sourceBytes.data() + offset, byteCount, &identity, sizeof(identity));
    }

    u64 elapsed = 0u;
    u64 allocations = 0u;
    u64 reallocations = 0u;
    u64 deallocations = 0u;
    u64 scratchPeak = 0u;
    u64 scratchPeakDelta = 0u;
    u64 scratchReserved = 0u;
    u64 scratchUsedBefore = 0u;
    u64 scratchUsedAfter = 0u;
    u64 observedOffsets = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        NWB::Core::Alloc::ScratchArena scratch(Name("tests/material_typed_dedup/benchmark_operation"));
        ByteVector uploadBytes(scratch);
        uploadBytes.reserve(byteCount * valueCount);
        RangeMap ranges(0u, RangeMap::hasher(), RangeMap::key_equal(), scratch);
        ranges.reserve(valueCount);
        if(repeatedHits){
            // Warm the table and scratch backing before measuring duplicate probes.
            const Span<const u8> bytes(sourceBytes.data(), byteCount);
            for(usize warmup = 0u; warmup < 64u; ++warmup){
                ByteRange range;
                ASSERT_TRUE(NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, bytes, range));
            }
        }

        const ArenaMemoryStats before = scratch.memoryStats();
        const Timer begin = TimerNow();
        bool success = true;
        for(usize lookup = 0u; lookup < lookupCount; ++lookup){
            const usize sourceIndex = repeatedHits ? 0u : lookup;
            const Span<const u8> bytes(sourceBytes.data() + sourceIndex * byteCount, byteCount);
            ByteRange range;
            if(!NWB::Impl::ECSRenderDetail::FindOrAppendMaterialTypedByteRange(uploadBytes, ranges, bytes, range)){
                success = false;
                break;
            }
            observedOffsets += range.byteOffset;
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        const ArenaMemoryStats after = scratch.memoryStats();
        ASSERT_TRUE(success);
        EXPECT_EQ(ranges.size(), valueCount);
        EXPECT_EQ(uploadBytes.size(), byteCount * valueCount);
        if(repeatedHits)
            EXPECT_EQ(after.usedBytes, before.usedBytes);
        allocations += after.allocationCount - before.allocationCount;
        reallocations += after.reallocationCount - before.reallocationCount;
        deallocations += after.deallocationCount - before.deallocationCount;
        scratchPeak = Max(scratchPeak, after.peakUsedBytes);
        scratchPeakDelta = Max(scratchPeakDelta, after.peakUsedBytes - before.peakUsedBytes);
        scratchReserved = Max(scratchReserved, after.reservedBytes);
        scratchUsedBefore = Max(scratchUsedBefore, before.usedBytes);
        scratchUsedAfter = Max(scratchUsedAfter, after.usedBytes);
    }
    const u64 expectedOffsets = repeatedHits ? 0u : byteCount * valueCount * (valueCount - 1u) / 2u * iterations;
    EXPECT_EQ(observedOffsets, expectedOffsets);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_ns", elapsed);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_byte_count", byteCount);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_value_count", valueCount);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_lookup_count", lookupCount * iterations);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_iterations", iterations);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_repeated_hits", repeatedHits ? 1u : 0u);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_allocations", allocations);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_reallocations", reallocations);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_deallocations", deallocations);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_scratch_peak_bytes", scratchPeak);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_scratch_peak_delta_bytes", scratchPeakDelta);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_scratch_reserved_bytes", scratchReserved);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_scratch_used_before_bytes", scratchUsedBefore);
    NWB::Tests::RecordUnsignedTestProperty("material_typed_dedup_scratch_used_after_bytes", scratchUsedAfter);
}

// Opt in with --gtest_also_run_disabled_tests and a MaterialTypedDedupBenchmark.* filter.
TEST(MaterialTypedDedupBenchmark, DISABLED_Repeated32Bytes){
    BenchmarkMaterialTypedRanges(32u, 1u, 262144u, 1u, true);
}

TEST(MaterialTypedDedupBenchmark, DISABLED_Repeated512Bytes){
    BenchmarkMaterialTypedRanges(512u, 1u, 65536u, 1u, true);
}

TEST(MaterialTypedDedupBenchmark, DISABLED_Repeated4096Bytes){
    BenchmarkMaterialTypedRanges(4096u, 1u, 16384u, 1u, true);
}

TEST(MaterialTypedDedupBenchmark, DISABLED_Unique32Bytes){
    BenchmarkMaterialTypedRanges(32u, 256u, 256u, 16u, false);
}

TEST(MaterialTypedDedupBenchmark, DISABLED_Unique512Bytes){
    BenchmarkMaterialTypedRanges(512u, 256u, 256u, 16u, false);
}

TEST(MaterialTypedDedupBenchmark, DISABLED_Unique4096Bytes){
    BenchmarkMaterialTypedRanges(4096u, 256u, 256u, 16u, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static NWB::Impl::SkeletonJointMatrix MakeTranslationJointMatrix(const f32 x, const f32 y, const f32 z){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0].w = x;
    joint.rows[1].w = y;
    joint.rows[2].w = z;
    return joint;
}


static NWB::Impl::SkeletonJointMatrix MakeNonUniformScaleJointMatrix(){
    NWB::Impl::SkeletonJointMatrix joint = ::Float34Identity();
    joint.rows[0] = Float4(2.0f, 0.0f, 0.0f, 0.0f);
    return joint;
}

#if defined(NWB_FINAL)
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
        [](const usize, const TStringView){ return false; }
    );
    NWB_FATAL_ASSERT(meshletRefsEncoded);
    instance.meshletPositionRefCount = static_cast<u32>(meshletPositionStreamRefs.size());
    instance.meshletAttributeRefCount = static_cast<u32>(meshletAttributeStreamRefs.size());

    return instance;
}

static NWB::Impl::SkeletonJointMatrix MakeIdentityJointMatrix(){
    return MakeTranslationJointMatrix(0.0f, 0.0f, 0.0f);
}

#endif


TEST(EcsGraphics, NonUniformScaleCannotBecomeRigidJointRotation){
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
    const NWB::Impl::SkeletonJointMatrix& childJoint
){
    NWB::Impl::SkeletonPoseComponent pose(NWB::Tests::TestDetail::Arena());
    pose.parentJoints.push_back(NWB::Impl::s_SkeletonRootParent);
    pose.parentJoints.push_back(0u);
    pose.localJoints.push_back(rootJoint);
    pose.localJoints.push_back(childJoint);
    return pose;
}

TEST(EcsGraphics, InvalidSkeletonParentsAndJointCountsAreRejected){
    NWB::Impl::SkeletonPoseComponent pose = MakeTwoJointSkeletonPose(
        MakeTranslationJointMatrix(1.0f, 0.0f, 0.0f),
        MakeTranslationJointMatrix(0.0f, 2.0f, 0.0f)
    );

    Vector<NWB::Impl::SkeletonJointMatrix> resolvedJoints;
    u32 skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    ASSERT_TRUE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
    pose.parentJoints[1u] = 1u;
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
    pose.parentJoints[1u] = 0u;
    pose.parentJoints.pop_back();
    EXPECT_FALSE(NWB::Impl::SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(pose, resolvedJoints, skinningMode));
}
#if defined(NWB_FINAL)
TEST(EcsGraphics, MeshSkinningPayloadValidatesSkeletonAndPalette){
    constexpr u32 s_ExpectedDualCount = 2u;

    NWB::Impl::MeshSkinningRuntimeInstance instance = MakeTriangleInstance();
    AssignSingleJointSkin(instance, 0u);
    instance.handle.value = 517u;

    NWB::Impl::SkeletonJointPaletteComponent joints(NWB::Tests::TestDetail::Arena());
    joints.joints.push_back(MakeIdentityJointMatrix());

    Vector<NWB::Impl::SkeletonJointMatrix> jointMatrices;
    CapturingLogger runtimeValidationLogger;
    NWB::Core::Common::LoggerRegistrationGuard runtimeValidationLoggerRegistrationGuard(runtimeValidationLogger);

    NWB::Impl::MeshSkinningRuntimeInstance outsidePalette = instance;
    outsidePalette.skin[0u] = MakeSingleJointSkin(1u);
    outsidePalette.skeletonJointCount = s_ExpectedDualCount;
    outsidePalette.inverseBindMatrices.clear();
    joints.joints.resize(1u, ::Float34Identity());
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(outsidePalette, joints.joints, joints.skinningMode, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance nonAffineJoint = instance;
    joints.joints[0u] = MakeIdentityJointMatrix();
    joints.joints[0u].rows[0] = Float4(0.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(nonAffineJoint, joints.joints, joints.skinningMode, jointMatrices));

    NWB::Impl::MeshSkinningRuntimeInstance scaledDualQuaternionJoint = instance;
    scaledDualQuaternionJoint.inverseBindMatrices.clear();
    joints.skinningMode = NWB::Impl::SkeletonSkinningMode::DualQuaternion;
    joints.joints[0u] = MakeNonUniformScaleJointMatrix();
    EXPECT_FALSE(NWB::Impl::MeshSkinningPayload::BuildSkinJointPalette(
        scaledDualQuaternionJoint,
        joints.joints,
        joints.skinningMode,
        jointMatrices
    ));

    EXPECT_EQ(runtimeValidationLogger.errorCount(), 3u);
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette count")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("joint palette entry 0 is not a finite invertible affine matrix")));
    EXPECT_TRUE(runtimeValidationLogger.sawErrorContaining(NWB_TEXT("failed dual-quaternion payload build")));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

