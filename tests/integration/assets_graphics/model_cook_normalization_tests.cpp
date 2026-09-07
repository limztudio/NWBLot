// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_model/cook.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_cook_normalization_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using Core::Metascript::Value;


struct ModelMetadata{
    Core::Metascript::MetaArena metadataArena{ Name("tests/model_cook_normalization/metadata") };
    Core::Assets::AssetArena outputArena{ Name("tests/model_cook_normalization/output") };
    Core::Alloc::ScratchArena scratchArena{ Name("tests/model_cook_normalization/scratch") };
    Value asset{ metadataArena };
    ModelCookEntry entry{ outputArena };
    HashMap<Name, Name, Core::Metascript::MetaArena> expectedSkeletons{ metadataArena };
    const NWB::Path path{ outputArena, "tests/model_cook_normalization/model.nwb" };


    ModelMetadata(){
        asset.makeMap();
    }

    void addSkeleton(const AStringView objectName, const AStringView skeletonAsset){
        Value& object = asset.field("skeletons").field(objectName);
        object.field("skeleton").setString(skeletonAsset);
    }

    void addSkinnedMesh(const AStringView objectName, const AStringView skeleton, const AStringView expectedObject){
        Value& object = asset.field("skinned_meshes").field(objectName);
        object.field("mesh").setString("tests/model_cook_normalization/mesh");
        object.field("skin").setString("tests/model_cook_normalization/skin");
        object.field("skeleton").setString(skeleton);
        expectedSkeletons.insert_or_assign(Name(objectName), Name(expectedObject));
    }

    [[nodiscard]] bool parse(){
        return ParseModelCookMetadata(Name("tests/model_cook_normalization/model"), path, asset, entry, scratchArena);
    }

    void verifyNormalizedMeshes()const{
        ASSERT_EQ(entry.skinnedMeshObjects.size(), expectedSkeletons.size());
        for(const ModelSkinnedMeshObject& object : entry.skinnedMeshObjects){
            const auto expected = expectedSkeletons.find(object.name);
            ASSERT_NE(expected, expectedSkeletons.end());
            EXPECT_EQ(object.skeletonObject, expected->second);
            EXPECT_EQ(object.mesh.name(), Name("tests/model_cook_normalization/mesh"));
            EXPECT_EQ(object.skin.name(), Name("tests/model_cook_normalization/skin"));
        }
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void AddIndexedObjects(ModelMetadata& metadata, const usize skeletonCount, const usize meshCount, const bool useAliases){
    for(usize index = 0u; index < skeletonCount; ++index){
        const auto objectName = StringFormat(metadata.metadataArena, "rig_{}", index);
        const auto assetName = StringFormat(metadata.metadataArena, "tests/model_cook_normalization/skeleton_{}", index);
        metadata.addSkeleton(objectName, assetName);
    }
    for(usize index = 0u; index < meshCount; ++index){
        const usize skeletonIndex = (index * 73u) % skeletonCount;
        const auto meshName = StringFormat(metadata.metadataArena, "mesh_{}", index);
        const auto objectName = StringFormat(metadata.metadataArena, "rig_{}", skeletonIndex);
        const auto assetName = StringFormat(metadata.metadataArena, "tests/model_cook_normalization/skeleton_{}", skeletonIndex);
        metadata.addSkinnedMesh(meshName, useAliases ? AStringView(assetName) : AStringView(objectName), objectName);
    }
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkNormalization(const usize count, const bool useAliases, const usize iterations = 3u){
    ModelMetadata metadata;
    AddIndexedObjects(metadata, count, count, useAliases);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyNormalizedMeshes();
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
    metadata.verifyNormalizedMeshes();
    const ArenaMemoryStats currentScratch = metadata.scratchArena.memoryStats();
    EXPECT_EQ(currentScratch.usedBytes, warmScratch.usedBytes);
    EXPECT_EQ(currentScratch.reservedBytes, warmScratch.reservedBytes);
    RecordUnsignedProperty(MakeNotNull("model_parse_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("model_parse_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("model_skeleton_count"), count);
    RecordUnsignedProperty(MakeNotNull("model_skinned_mesh_count"), count);
    RecordUnsignedProperty(MakeNotNull("model_parse_heap_allocations"), after.allocationCount - before.allocationCount);
    RecordUnsignedProperty(MakeNotNull("model_parse_scratch_reserved_bytes"), currentScratch.reservedBytes);
    RecordUnsignedProperty(MakeNotNull("model_parse_scratch_used_bytes"), currentScratch.usedBytes);
    testing::Test::RecordProperty("model_skeleton_reference_mode", useAliases ? "asset_alias" : "object_name");
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ModelCookNormalization, DirectObjectNamesTakePrecedenceOverAmbiguousAssetAliases){
    ModelMetadata metadata;
    metadata.addSkeleton("shared", "tests/model_cook_normalization/direct");
    metadata.addSkeleton("other_a", "SHARED");
    metadata.addSkeleton("other_b", "shared");
    metadata.addSkeleton("unique_rig", "Tests/Model_Cook_Normalization/Unique");
    metadata.addSkinnedMesh("direct_mesh", "ShArEd", "shared");
    metadata.addSkinnedMesh("alias_mesh", "tests/model_cook_normalization/unique", "unique_rig");
    metadata.addSkinnedMesh("other_direct_mesh", "OTHER_A", "other_a");
    ASSERT_TRUE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    EXPECT_EQ(metadata.entry.skeletonObjects.size(), 4u);
}

TEST(ModelCookNormalization, ResolvesRepeatedAliasesWithoutChangingMetadataOrObjectOrder){
    ModelMetadata metadata;
    AddIndexedObjects(metadata, 33u, 257u, true);
    ASSERT_TRUE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    const Value* meshes = metadata.asset.findField("skinned_meshes");
    ASSERT_NE(meshes, nullptr);
    usize index = 0u;
    for(const auto& [objectName, object] : meshes->asMap()){
        ASSERT_LT(index, metadata.entry.skinnedMeshObjects.size());
        const auto& normalized = metadata.entry.skinnedMeshObjects[index++];
        EXPECT_EQ(normalized.name, Name(AStringView(objectName)));
        const Value* skeleton = object.findField("skeleton");
        ASSERT_NE(skeleton, nullptr);
        EXPECT_NE(Name(skeleton->asString()), normalized.skeletonObject);
    }
    ASSERT_TRUE(metadata.parse());
    metadata.verifyNormalizedMeshes();
}

TEST(ModelCookNormalization, AmbiguousAliasesFailBeforeMissingTargetValidationAndReparseCleanly){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton("rig_a", "tests/model_cook_normalization/shared");
    metadata.addSkeleton("rig_b", "TESTS/MODEL_COOK_NORMALIZATION/SHARED");
    metadata.addSkinnedMesh("unknown_mesh", "missing_rig", "missing_rig");
    metadata.addSkinnedMesh("ambiguous_mesh", "tests/model_cook_normalization/shared", "rig_a");
    EXPECT_FALSE(metadata.parse());
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("matches multiple skeleton objects")));
    EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("targets a missing skeleton object")));
    metadata.asset.field("skinned_meshes").field("unknown_mesh").field("skeleton").setString("rig_b");
    metadata.asset.field("skinned_meshes").field("ambiguous_mesh").field("skeleton").setString("rig_a");
    metadata.expectedSkeletons.at(Name("unknown_mesh")) = Name("rig_b");
    ASSERT_TRUE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    EXPECT_EQ(logger.errorCount(), 1u);
}

TEST(ModelCookNormalization, UnknownAliasesRemainUnchangedForPayloadValidation){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton("rig", "tests/model_cook_normalization/known");
    metadata.addSkinnedMesh("body", "tests/model_cook_normalization/unknown", "tests/model_cook_normalization/unknown");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("targets a missing skeleton object")));
    EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("matches multiple skeleton objects")));
}

TEST(ModelCookNormalization, CanonicalDuplicateObjectNamesRemainValidatorErrors){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.addSkeleton("rig", "tests/model_cook_normalization/first");
    metadata.addSkeleton("RIG", "tests/model_cook_normalization/second");
    metadata.addSkinnedMesh("body", "RiG", "rig");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("name is duplicated in the model")));
}

TEST(ModelCookNormalization, HandlesStaticOnlyAndMissingSkeletonCollections){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard registration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelMetadata metadata;
    metadata.asset.field("static_meshes").field("prop").field("mesh").setString("tests/model_cook_normalization/mesh");
    ASSERT_TRUE(metadata.parse());
    EXPECT_TRUE(metadata.entry.skeletonObjects.empty());
    EXPECT_TRUE(metadata.entry.skinnedMeshObjects.empty());
    metadata.addSkinnedMesh("body", "missing", "missing");
    EXPECT_FALSE(metadata.parse());
    metadata.verifyNormalizedMeshes();
    EXPECT_EQ(logger.errorCount(), 1u);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("targets a missing skeleton object")));
}

// Metadata construction and result checks stay outside the timed public cook-metadata parsing operation.
TEST(ModelCookNormalization, DISABLED_AliasBenchmarkSingleObject){
    BenchmarkNormalization(1u, true, 1024u);
}

TEST(ModelCookNormalization, DISABLED_DirectBenchmarkSingleObject){
    BenchmarkNormalization(1u, false, 1024u);
}

TEST(ModelCookNormalization, DISABLED_AliasBenchmark256Objects){
    BenchmarkNormalization(256u, true);
}

TEST(ModelCookNormalization, DISABLED_AliasBenchmark1024Objects){
    BenchmarkNormalization(1024u, true);
}

TEST(ModelCookNormalization, DISABLED_AliasBenchmark4096Objects){
    BenchmarkNormalization(4096u, true);
}

TEST(ModelCookNormalization, DISABLED_DirectBenchmark4096Objects){
    BenchmarkNormalization(4096u, false);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

