// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_model/cook.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_cook_metadata_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_SKELETON = "skeleton";
static constexpr AStringView s_SKINNED_MESHES = "skinned_meshes";
static constexpr AStringView s_MeshVirtualPath = "tests/model_cook_metadata/mesh";
static constexpr TStringView s_TARGETS_A_MISSING_SKELETON_OBJECT = GLB_TEXT("targets a missing skeleton object");
static constexpr AStringView s_RIG_A = "rig_a";
static constexpr AStringView s_RIG_B = "rig_b";
static constexpr AStringView s_RIG = "rig";
static constexpr AStringView s_BODY = "body";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using Core::Metascript::Value;


struct ModelMetadata{
    Core::Metascript::MetaArena metadataArena{ Name("tests/model_cook_metadata/metadata") };
    Core::Assets::AssetArena outputArena{ Name("tests/model_cook_metadata/output") };
    Core::Alloc::ScratchArena scratchArena{ Name("tests/model_cook_metadata/scratch") };
    Value asset{ metadataArena };
    ModelCookEntry entry{ outputArena };
    HashMap<Name, Name, Core::Metascript::MetaArena> expectedSkeletons{ metadataArena };
    const NWB::Path path{ outputArena, "tests/model_cook_metadata/model.nwb" };


    ModelMetadata(){
        asset.makeMap();
    }

    void addSkeleton(const AStringView objectName, const AStringView skeletonAsset){
        Value& object = asset.field("skeletons").field(objectName);
        object.field(s_SKELETON).setString(skeletonAsset);
    }

    void addSkinnedMesh(const AStringView objectName, const AStringView skeleton, const AStringView expectedObject){
        Value& object = asset.field(s_SKINNED_MESHES).field(objectName);
        object.field("skin").setString("tests/model_cook_metadata/skin");
        object.field(s_SKELETON).setString(skeleton);
        expectedSkeletons.insert_or_assign(Name(objectName), Name(expectedObject));
    }

    [[nodiscard]] bool parse(){
        return ParseModelCookMetadata(Name("tests/model_cook_metadata/model"), path, asset, entry, scratchArena);
    }

    void verifyObjectReferences()const{
        ASSERT_EQ(entry.skinnedMeshObjects.size(), expectedSkeletons.size());
        for(const ModelSkinnedMeshObject& object : entry.skinnedMeshObjects){
            const auto expected = expectedSkeletons.find(object.name);
            ASSERT_NE(expected, expectedSkeletons.end());
            EXPECT_EQ(object.skeletonObject, expected->second);
            EXPECT_EQ(object.skin.name(), Name("tests/model_cook_metadata/skin"));
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddIndexedObjects(ModelMetadata& metadata, const usize skeletonCount, const usize meshCount){
    for(usize index = 0u; index < skeletonCount; ++index){
        const auto objectName = StringFormat(metadata.metadataArena, "rig_{}", index);
        const auto assetName = StringFormat(metadata.metadataArena, "tests/model_cook_metadata/skeleton_{}", index);
        metadata.addSkeleton(objectName, assetName);
    }
    for(usize index = 0u; index < meshCount; ++index){
        const usize skeletonIndex = (index * 73u) % skeletonCount;
        const auto meshName = StringFormat(metadata.metadataArena, "mesh_{}", index);
        const auto objectName = StringFormat(metadata.metadataArena, "rig_{}", skeletonIndex);
        metadata.addSkinnedMesh(meshName, objectName, objectName);
    }
}

static void BenchmarkObjectReferences(const usize count, const usize iterations = 3u){
    ModelMetadata metadata;
    AddIndexedObjects(metadata, count, count);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyObjectReferences();
    const ArenaMemoryStats warmScratch = metadata.scratchArena.memoryStats();
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    usize accepted = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(metadata.parse())
            ++accepted;
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    ASSERT_EQ(accepted, iterations);
    metadata.verifyObjectReferences();
    const ArenaMemoryStats currentScratch = metadata.scratchArena.memoryStats();
    EXPECT_EQ(currentScratch.usedBytes, warmScratch.usedBytes);
    EXPECT_EQ(currentScratch.reservedBytes, warmScratch.reservedBytes);
    Tests::RecordUnsignedTestProperty("model_parse_ns", elapsed);
    Tests::RecordUnsignedTestProperty("model_parse_iterations", iterations);
    Tests::RecordUnsignedTestProperty("model_skeleton_count", count);
    Tests::RecordUnsignedTestProperty("model_skinned_mesh_count", count);
    Tests::RecordUnsignedTestProperty("model_parse_heap_allocations", after.allocationCount - before.allocationCount);
    Tests::RecordUnsignedTestProperty("model_parse_scratch_reserved_bytes", currentScratch.reservedBytes);
    Tests::RecordUnsignedTestProperty("model_parse_scratch_used_bytes", currentScratch.usedBytes);
    testing::Test::RecordProperty("model_skeleton_reference_mode", "object_name");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ModelCookMetadata, UniqueAssetPathReferencesFailAndLocalObjectReferencesRecover){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(const usize skeletonCount : { 1u, 33u }){
        ModelMetadata metadata;
        AddIndexedObjects(metadata, skeletonCount, 257u);
        ASSERT_TRUE(metadata.parse());
        metadata.verifyObjectReferences();
        const AStringView assetPath = "tests/model_cook_metadata/skeleton_0";
        metadata.asset.field(s_SKINNED_MESHES).field("mesh_0").field(s_SKELETON).setString(assetPath);
        metadata.expectedSkeletons.at(Name("mesh_0")) = Name(assetPath);
        EXPECT_FALSE(metadata.parse());
        metadata.verifyObjectReferences();
        EXPECT_EQ(metadata.asset.field(s_SKINNED_MESHES).field("mesh_0").field(s_SKELETON).asString(), assetPath);
        metadata.asset.field(s_SKINNED_MESHES).field("mesh_0").field(s_SKELETON).setString("rig_0");
        metadata.expectedSkeletons.at(Name("mesh_0")) = Name("rig_0");
        ASSERT_TRUE(metadata.parse());
        metadata.verifyObjectReferences();
    }
    EXPECT_EQ(logger.errorCount(), 2u);
    EXPECT_TRUE(logger.sawErrorContaining(s_TARGETS_A_MISSING_SKELETON_OBJECT));
}

TEST(ModelCookMetadata, SharedAssetPathCannotSelectAmongLocalSkeletonObjects){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton(s_RIG_A, "tests/model_cook_metadata/shared");
    metadata.addSkeleton(s_RIG_B, "TESTS/MODEL_COOK_METADATA/SHARED");
    metadata.addSkinnedMesh(s_BODY, "tests/model_cook_metadata/shared", "tests/model_cook_metadata/shared");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_TRUE(logger.sawErrorContaining(s_TARGETS_A_MISSING_SKELETON_OBJECT));
    metadata.asset.field(s_SKINNED_MESHES).field(s_BODY).field(s_SKELETON).setString(s_RIG_A);
    metadata.expectedSkeletons.at(Name(s_BODY)) = Name(s_RIG_A);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_EQ(logger.errorCount(), 1u);
}

TEST(ModelCookMetadata, UnknownObjectReferencesRemainValidatorErrors){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton(s_RIG, "tests/model_cook_metadata/known");
    metadata.addSkinnedMesh(s_BODY, "tests/model_cook_metadata/unknown", "tests/model_cook_metadata/unknown");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(s_TARGETS_A_MISSING_SKELETON_OBJECT));
}

TEST(ModelCookMetadata, CanonicalDuplicateObjectNamesRemainValidatorErrors){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton(s_RIG, "tests/model_cook_metadata/first");
    metadata.addSkeleton("RIG", "tests/model_cook_metadata/second");
    metadata.addSkinnedMesh(s_BODY, "RiG", s_RIG);
    EXPECT_FALSE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("name is duplicated in the model")));
}

TEST(ModelCookMetadata, HandlesStaticOnlyAndMissingSkeletonCollections){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.asset.field("static_meshes").field("prop").field("mesh").setString(s_MeshVirtualPath);
    ASSERT_TRUE(metadata.parse());
    EXPECT_TRUE(metadata.entry.skeletonObjects.empty());
    EXPECT_TRUE(metadata.entry.skinnedMeshObjects.empty());
    metadata.addSkinnedMesh(s_BODY, "missing", "missing");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(s_TARGETS_A_MISSING_SKELETON_OBJECT));
}

TEST(ModelCookMetadata, RedundantSkinnedMeshReferenceIsRejectedAndSkinOnlyReferenceRecovers){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton(s_RIG, "tests/model_cook_metadata/skeleton");
    metadata.addSkinnedMesh(s_BODY, s_RIG, s_RIG);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyObjectReferences();

    Value& object = metadata.asset.field(s_SKINNED_MESHES).field(s_BODY);
    object.field("mesh").setString(s_MeshVirtualPath);
    EXPECT_FALSE(metadata.parse());
    EXPECT_TRUE(metadata.entry.skinnedMeshObjects.empty());
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("unsupported asset field 'mesh'")));

    ASSERT_EQ(object.asMap().erase(AStringView("mesh")), 1u);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyObjectReferences();
    EXPECT_EQ(logger.errorCount(), 1u);
}

TEST(ModelCookMetadata, FourRowTransformFailsWithoutRetainingPreviousOutputAndThreeRowsRecover){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    Value& object = metadata.asset.field("static_meshes").field("prop");
    object.field("mesh").setString(s_MeshVirtualPath);
    Value& transform = object.field("transform");
    transform.makeList();
    for(usize rowIndex = 0u; rowIndex < 3u; ++rowIndex){
        Value row(metadata.metadataArena);
        row.makeList();
        for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex)
            row.append(Value(static_cast<i64>(rowIndex == columnIndex ? 1 : 0), metadata.metadataArena));
        transform.append(Move(row));
    }
    transform.asList()[0u].asList()[3u].setDouble(0.5);
    ASSERT_TRUE(metadata.parse());
    ASSERT_EQ(metadata.entry.staticMeshObjects.size(), 1u);
    EXPECT_EQ(metadata.entry.staticMeshObjects[0u].transform._14, 0.5f);

    Value homogeneousRow(metadata.metadataArena);
    homogeneousRow.makeList();
    for(usize columnIndex = 0u; columnIndex < 4u; ++columnIndex)
        homogeneousRow.append(Value(static_cast<i64>(columnIndex == 3u ? 1 : 0), metadata.metadataArena));
    transform.append(Move(homogeneousRow));
    EXPECT_FALSE(metadata.parse());
    EXPECT_TRUE(metadata.entry.staticMeshObjects.empty());
    EXPECT_TRUE(logger.sawErrorContaining(GLB_TEXT("must be a 3x4 affine matrix")));

    transform.asList().pop_back();
    ASSERT_TRUE(metadata.parse());
    ASSERT_EQ(metadata.entry.staticMeshObjects.size(), 1u);
    EXPECT_EQ(metadata.entry.staticMeshObjects[0u].transform._14, 0.5f);
    EXPECT_EQ(logger.errorCount(), 1u);
}

// Metadata construction and result checks stay outside the timed public cook-metadata parsing operation.
TEST(ModelCookMetadata, DISABLED_DirectBenchmarkSingleObject){
    BenchmarkObjectReferences(1u, 1024u);
}

TEST(ModelCookMetadata, DISABLED_DirectBenchmark4096Objects){
    BenchmarkObjectReferences(4096u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

