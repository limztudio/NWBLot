// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_model/asset.h>
#include <impl/assets_model/binary_payload.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/binary.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_payload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;


struct ModelInputs{
    Core::Assets::AssetArena arena{ Name("tests/model_payload/inputs") };
    Core::Alloc::ScratchArena scratchArena{ Name("tests/model_payload/validation_scratch") };
    Model model{ arena, Name("tests/model_payload/model") };
    Model::SkeletonObjectVector skeletonObjects{ arena };
    Model::StaticMeshObjectVector staticMeshObjects{ arena };
    Model::SkinnedMeshObjectVector skinnedMeshObjects{ arena };


    ModelInputs(){
        skeletonObjects.push_back(ModelSkeletonObject{
            .name = Name("rig"),
            .skeleton = Core::Assets::AssetRef<Skeleton>("tests/model_payload/skeleton"),
        });
        staticMeshObjects.push_back(ModelStaticMeshObject{
            .name = Name("prop"),
            .mesh = Core::Assets::AssetRef<Mesh>("tests/model_payload/mesh"),
            .material = {},
            .parentObject = Name("rig"),
            .parentJoint = Name("hand"),
        });
        skinnedMeshObjects.push_back(ModelSkinnedMeshObject{
            .name = Name("body"),
            .mesh = Core::Assets::AssetRef<Mesh>("tests/model_payload/mesh"),
            .skin = Core::Assets::AssetRef<Skin>("tests/model_payload/skin"),
            .material = {},
            .skeletonObject = Name("rig"),
        });
        publish();
    }

    void publish(){
        model.setObjects(
            Model::SkeletonObjectVector(skeletonObjects),
            Model::StaticMeshObjectVector(staticMeshObjects),
            Model::SkinnedMeshObjectVector(skinnedMeshObjects)
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Name IndexedName(const Name prefix, const usize index){
    char indexText[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, indexText));
}

static void MeasureValidation(ModelInputs& inputs, const usize iterations){
    ASSERT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    const ArenaMemoryStats beforeScratch = inputs.scratchArena.memoryStats();
    const ArenaMemoryStats before = HeapBackingMemoryStats();
    usize accepted = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(inputs.model.validatePayload(inputs.scratchArena))
            ++accepted;
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats after = HeapBackingMemoryStats();
    EXPECT_EQ(accepted, iterations);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
    const ArenaMemoryStats afterScratch = inputs.scratchArena.memoryStats();
    EXPECT_EQ(afterScratch.usedBytes, beforeScratch.usedBytes);
    EXPECT_EQ(afterScratch.reservedBytes, beforeScratch.reservedBytes);

    const usize objectCount = inputs.model.skeletonObjects().size()
        + inputs.model.staticMeshObjects().size() + inputs.model.skinnedMeshObjects().size();
    char durationText[32u] = {};
    char iterationsText[32u] = {};
    char objectsText[32u] = {};
    char allocationsText[32u] = {};
    char reservedText[32u] = {};
    char peakText[32u] = {};
    testing::Test::RecordProperty("model_validation_scope", "validation_with_reused_caller_scratch");
    testing::Test::RecordProperty("model_validation_scratch_reserved_bytes", FormatDecimal(afterScratch.reservedBytes, reservedText).data());
    testing::Test::RecordProperty("model_validation_scratch_peak_bytes", FormatDecimal(afterScratch.peakUsedBytes, peakText).data());
    testing::Test::RecordProperty("model_validation_ns", FormatDecimal(elapsed, durationText).data());
    testing::Test::RecordProperty("model_validation_iterations", FormatDecimal(iterations, iterationsText).data());
    testing::Test::RecordProperty("model_object_count", FormatDecimal(objectCount, objectsText).data());
    testing::Test::RecordProperty(
        "model_validation_backing_allocations", FormatDecimal(after.allocationCount - before.allocationCount, allocationsText).data()
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ModelPayload, AcceptsMixedObjectsAndOptionalBindings){
    ModelInputs inputs;
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.staticMeshObjects.front().parentJoint = NAME_NONE;
    inputs.staticMeshObjects.front().material = Core::Assets::AssetRef<Material>("tests/model_payload/material");
    inputs.skinnedMeshObjects.front().material = inputs.staticMeshObjects.front().material;
    inputs.publish();
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.staticMeshObjects.front().parentObject = NAME_NONE;
    inputs.staticMeshObjects.front().material.reset();
    inputs.skinnedMeshObjects.front().material.reset();
    inputs.publish();
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
}

TEST(ModelPayload, RejectsDuplicateNamesWithinAndAcrossObjectKinds){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 duplicateCase = 0u; duplicateCase < 7u; ++duplicateCase){
        ModelInputs inputs;
        switch(duplicateCase){
        case 0u: inputs.skeletonObjects.push_back(inputs.skeletonObjects.front()); break;
        case 1u: inputs.staticMeshObjects.push_back(inputs.staticMeshObjects.front()); break;
        case 2u: inputs.skinnedMeshObjects.push_back(inputs.skinnedMeshObjects.front()); break;
        case 3u: inputs.staticMeshObjects.front().name = inputs.skeletonObjects.front().name; break;
        case 4u: inputs.skinnedMeshObjects.front().name = inputs.skeletonObjects.front().name; break;
        case 5u: inputs.skinnedMeshObjects.front().name = inputs.staticMeshObjects.front().name; break;
        case 6u: inputs.staticMeshObjects.front().name = Name("RIG"); break;
        }
        inputs.publish();
        EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena)) << duplicateCase;
    }
    EXPECT_EQ(logger.errorCount(), 7u);
}

TEST(ModelPayload, RejectsMissingNamesRequiredReferencesAndEmptyModels){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 invalidCase = 0u; invalidCase < 8u; ++invalidCase){
        ModelInputs inputs;
        switch(invalidCase){
        case 0u: inputs.skeletonObjects.front().name = NAME_NONE; break;
        case 1u: inputs.skeletonObjects.front().skeleton.reset(); break;
        case 2u: inputs.staticMeshObjects.front().name = NAME_NONE; break;
        case 3u: inputs.staticMeshObjects.front().mesh.reset(); break;
        case 4u: inputs.skinnedMeshObjects.front().name = NAME_NONE; break;
        case 5u: inputs.skinnedMeshObjects.front().mesh.reset(); break;
        case 6u: inputs.skinnedMeshObjects.front().skin.reset(); break;
        case 7u: inputs.skinnedMeshObjects.front().skeletonObject = NAME_NONE; break;
        }
        inputs.publish();
        EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena)) << invalidCase;
    }
    ModelInputs inputs;
    inputs.skeletonObjects.clear();
    inputs.staticMeshObjects.clear();
    inputs.skinnedMeshObjects.clear();
    inputs.publish();
    EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena));
    Model unnamed(inputs.arena);
    EXPECT_FALSE(unnamed.validatePayload(inputs.scratchArena));
}

TEST(ModelPayload, RejectsMissingOrWrongKindParents){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    for(u32 invalidCase = 0u; invalidCase < 7u; ++invalidCase){
        ModelInputs inputs;
        switch(invalidCase){
        case 0u: inputs.staticMeshObjects.front().parentObject = Name("missing"); break;
        case 1u: inputs.staticMeshObjects.front().parentObject = Name("prop"); break;
        case 2u: inputs.staticMeshObjects.front().parentObject = Name("body"); break;
        case 3u: inputs.staticMeshObjects.front().parentObject = NAME_NONE; break;
        case 4u: inputs.skinnedMeshObjects.front().skeletonObject = Name("missing"); break;
        case 5u: inputs.skinnedMeshObjects.front().skeletonObject = Name("prop"); break;
        case 6u: inputs.skinnedMeshObjects.front().skeletonObject = Name("body"); break;
        }
        inputs.publish();
        EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena)) << invalidCase;
    }
    EXPECT_EQ(logger.errorCount(), 7u);
}

TEST(ModelPayload, RevalidatesReplacedObjectsAndRecoversAfterRejection){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelInputs inputs;
    ASSERT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.skeletonObjects.front().name = Name("replacement_rig");
    inputs.publish();
    EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.staticMeshObjects.front().parentObject = Name("replacement_rig");
    inputs.skinnedMeshObjects.front().skeletonObject = Name("replacement_rig");
    inputs.publish();
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.skinnedMeshObjects.front().name = Name("prop");
    inputs.publish();
    EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.skinnedMeshObjects.clear();
    inputs.publish();
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
}

TEST(ModelPayload, SingleObjectValidationDoesNotAllocateOrResolveItselfAsASkeleton){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelInputs inputs;
    inputs.skeletonObjects.clear();
    inputs.skinnedMeshObjects.clear();
    inputs.publish();
    EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.staticMeshObjects.front().parentObject = inputs.staticMeshObjects.front().name;
    inputs.publish();
    EXPECT_FALSE(inputs.model.validatePayload(inputs.scratchArena));
    inputs.staticMeshObjects.front().parentObject = NAME_NONE;
    inputs.staticMeshObjects.front().parentJoint = NAME_NONE;
    inputs.publish();
    EXPECT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    EXPECT_EQ(inputs.scratchArena.memoryStats().allocationCount, 0u);
}

TEST(ModelPayload, RepeatedValidationReclaimsCallerScratchAfterSuccessAndRejection){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelInputs inputs;
    ASSERT_TRUE(inputs.model.validatePayload(inputs.scratchArena));
    const ArenaMemoryStats warm = inputs.scratchArena.memoryStats();
    for(usize iteration = 0u; iteration < 16u; ++iteration){
        inputs.skinnedMeshObjects.front().name = iteration % 2u == 0u ? Name("body") : Name("prop");
        inputs.publish();
        EXPECT_EQ(inputs.model.validatePayload(inputs.scratchArena), iteration % 2u == 0u);
        const ArenaMemoryStats current = inputs.scratchArena.memoryStats();
        EXPECT_EQ(current.usedBytes, warm.usedBytes);
        EXPECT_EQ(current.reservedBytes, warm.reservedBytes);
    }
}

TEST(ModelPayload, CodecRejectsDuplicateNamesAndWrongParentsAndReloadsValidPayload){
    Tests::CapturingLogger logger;
    Core::Common::LoggerRegistrationGuard loggerRegistration(logger, Core::Common::LoggerBreakPolicy::BreakOnFatal);
    ModelInputs inputs;
    ModelAssetCodec codec;
    Core::Assets::AssetBytes binary(inputs.arena);
    ASSERT_TRUE(codec.serialize(inputs.model, binary));
    Model loaded(inputs.arena, inputs.model.virtualPath());
    ASSERT_TRUE(loaded.loadBinary(binary));
    ASSERT_EQ(loaded.skeletonObjects().size(), 1u);
    ASSERT_EQ(loaded.staticMeshObjects().size(), 1u);
    ASSERT_EQ(loaded.skinnedMeshObjects().size(), 1u);
    EXPECT_EQ(loaded.staticMeshObjects().front().parentObject, Name("rig"));
    EXPECT_EQ(loaded.skinnedMeshObjects().front().skeletonObject, Name("rig"));

    const usize staticOffset = sizeof(ModelBinaryPayload::ModelHeaderBinary) + sizeof(ModelBinaryPayload::ModelSkeletonObjectBinary);
    const usize skinnedOffset = staticOffset + sizeof(ModelBinaryPayload::ModelStaticMeshObjectBinary);
    Core::Assets::AssetBytes malformed(binary);
    ModelBinaryPayload::ModelSkinnedMeshObjectBinary skinnedObject;
    NWB_MEMCPY(&skinnedObject, sizeof(skinnedObject), malformed.data() + skinnedOffset, sizeof(skinnedObject));
    skinnedObject.nameHash = Name("prop").hash();
    NWB_MEMCPY(malformed.data() + skinnedOffset, malformed.size() - skinnedOffset, &skinnedObject, sizeof(skinnedObject));
    EXPECT_FALSE(loaded.loadBinary(malformed));
    EXPECT_TRUE(loaded.loadBinary(binary));

    malformed = binary;
    ModelBinaryPayload::ModelStaticMeshObjectBinary staticObject;
    NWB_MEMCPY(&staticObject, sizeof(staticObject), malformed.data() + staticOffset, sizeof(staticObject));
    staticObject.parentObjectNameHash = Name("body").hash();
    NWB_MEMCPY(malformed.data() + staticOffset, malformed.size() - staticOffset, &staticObject, sizeof(staticObject));
    EXPECT_FALSE(loaded.loadBinary(malformed));
    EXPECT_TRUE(loaded.loadBinary(binary));

    inputs.skinnedMeshObjects.front().name = Name("rig");
    inputs.publish();
    EXPECT_FALSE(codec.serialize(inputs.model, malformed));
    EXPECT_TRUE(loaded.validatePayload(inputs.scratchArena));
}


// These workloads are opt-in via --gtest_also_run_disabled_tests and a ModelPayloadBenchmark.* filter.
TEST(ModelPayloadBenchmark, DISABLED_ValidatesSingleObject){
    ModelInputs inputs;
    inputs.skeletonObjects.clear();
    inputs.skinnedMeshObjects.clear();
    inputs.staticMeshObjects.front().parentObject = NAME_NONE;
    inputs.staticMeshObjects.front().parentJoint = NAME_NONE;
    inputs.publish();
    MeasureValidation(inputs, 4096u);
}

TEST(ModelPayloadBenchmark, DISABLED_ValidatesLargeMixedModel){
    ModelInputs inputs;
    inputs.skeletonObjects.clear();
    inputs.staticMeshObjects.clear();
    inputs.skinnedMeshObjects.clear();
    constexpr usize s_SkeletonCount = 256u;
    constexpr usize s_MeshCount = 1920u;
    inputs.skeletonObjects.reserve(s_SkeletonCount);
    inputs.staticMeshObjects.reserve(s_MeshCount);
    inputs.skinnedMeshObjects.reserve(s_MeshCount);
    for(usize index = 0u; index < s_SkeletonCount; ++index){
        inputs.skeletonObjects.push_back(ModelSkeletonObject{
            .name = IndexedName(Name("rig/"), index),
            .skeleton = Core::Assets::AssetRef<Skeleton>("tests/model_payload/skeleton"),
        });
    }
    for(usize index = 0u; index < s_MeshCount; ++index){
        const Name parent = inputs.skeletonObjects[index % s_SkeletonCount].name;
        inputs.staticMeshObjects.push_back(ModelStaticMeshObject{
            .name = IndexedName(Name("prop/"), index),
            .mesh = Core::Assets::AssetRef<Mesh>("tests/model_payload/mesh"),
            .material = {},
            .parentObject = parent,
            .parentJoint = Name("hand"),
        });
        inputs.skinnedMeshObjects.push_back(ModelSkinnedMeshObject{
            .name = IndexedName(Name("body/"), index),
            .mesh = Core::Assets::AssetRef<Mesh>("tests/model_payload/mesh"),
            .skin = Core::Assets::AssetRef<Skin>("tests/model_payload/skin"),
            .material = {},
            .skeletonObject = parent,
        });
    }
    inputs.publish();
    MeasureValidation(inputs, 4u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

