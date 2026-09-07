// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/assets/bunch/cook.h>
#include <core/assets/cook_metadata.h>

#include <impl/assets_model/cook.h>
#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_bunch_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;
using Metascript::Value;


struct BunchFixture{
    Metascript::MetaArena metadataArena{ Name("tests/asset_bunch/metadata") };
    AssetArena outputArena{ Name("tests/asset_bunch/output") };
    Alloc::ScratchArena scratchArena{ Name("tests/asset_bunch/scratch") };
    Metascript::Document document{ metadataArena };
    const NWB::Path assetRoot{ outputArena, "tests/asset_bunch/assets" };
    const NWB::Path filePath{ outputArena, "tests/asset_bunch/assets/fixtures/bundle.nwb" };
};


static constexpr AStringView s_OwnershipMetadata = R"(
metadata local = { "label": "a separately owned metadata string", "items": [1, 2, 3] };
probe first;
probe second;
first.local = local;
first.target = second;
second.items = [local, { "target": first }];
asset_bunch bunch = [second, first];
)";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void VerifyOwnedOutput(const ExpandedAssetMetadataVector& output, const Metascript::Document& document){
    ASSERT_EQ(output.size(), 2u);
    EXPECT_EQ(output[0u].assetType, Name("probe"));
    EXPECT_EQ(output[0u].virtualPath, Name("project/fixtures/bundle/second"));
    EXPECT_EQ(output[1u].virtualPath, Name("project/fixtures/bundle/first"));
    const Value* const items = output[0u].value.findField("items");
    ASSERT_NE(items, nullptr);
    ASSERT_TRUE(items->isList());
    ASSERT_EQ(items->asList().size(), 2u);
    const Value* const label = items->asList()[0u].findField("label");
    ASSERT_NE(label, nullptr);
    EXPECT_EQ(label->asString(), "a separately owned metadata string");
    const Value* const target = items->asList()[1u].findField("target");
    ASSERT_NE(target, nullptr);
    EXPECT_EQ(target->asString(), "project/fixtures/bundle/first");
    const Value* const first = document.findVariable("first");
    ASSERT_NE(first, nullptr);
    const Value* const sourceTarget = first->findField("target");
    ASSERT_NE(sourceTarget, nullptr);
    EXPECT_TRUE(sourceTarget->isReference());
    EXPECT_EQ(sourceTarget->asReference(), "second");
    const Value* const sourceLocal = first->findField("local");
    ASSERT_NE(sourceLocal, nullptr);
    EXPECT_TRUE(sourceLocal->isReference());
    EXPECT_EQ(sourceLocal->asReference(), "local");
}

[[nodiscard]] static bool ParseLookupFixture(BunchFixture& fixture, const AStringView metadata, const usize unusedDeclarations){
    AString<Alloc::ScratchArena> source(fixture.scratchArena);
    source.reserve(unusedDeclarations * 32u + metadata.size());
    for(usize index = 0u; index < unusedDeclarations; ++index)
        source += StringFormat(fixture.scratchArena, "metadata unused_{};\n", index);
    source.append(metadata.data(), metadata.size());
    return fixture.document.parse(AStringView(source));
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkDeclarationExpansion(const usize assetCount, const usize iterations){
    BunchFixture fixture;
    AString<Metascript::MetaArena> source(fixture.metadataArena);
    source.reserve(assetCount * 180u + 64u);
    for(usize index = 0u; index < assetCount; ++index){
        source += StringFormat(fixture.metadataArena, "metadata local_{} = {{ \"ordinal\": {}, \"items\": [1, 2, 3] }};\n", index, index);
    }
    for(usize index = 0u; index < assetCount; ++index)
        source += StringFormat(fixture.metadataArena, "probe asset_{};\n", index);
    for(usize index = 0u; index < assetCount; ++index){
        source += StringFormat(fixture.metadataArena, "asset_{}.local = local_{};\nasset_{}.target = asset_{};\n",
            index, (index * 73u) % assetCount, index, (index + 1u) % assetCount
        );
    }
    source += "asset_bunch bunch = [";
    for(usize index = 0u; index < assetCount; ++index){
        if(index)
            source += ',';
        source += StringFormat(fixture.metadataArena, "asset_{}", assetCount - index - 1u);
    }
    source += "];\n";
    ASSERT_TRUE(fixture.document.parse(source));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    ExpandedAssetMetadataVector output(fixture.scratchArena);
    ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
        fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
    ));
    ASSERT_EQ(output.size(), assetCount);
    output.clear();
    ASSERT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    const ArenaMemoryStats beforeHeap = HeapBackingMemoryStats();
    usize accepted = 0u;
    const Timer begin = TimerNow();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        if(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ))
            ++accepted;
        output.clear();
    }
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    const ArenaMemoryStats afterHeap = HeapBackingMemoryStats();
    ASSERT_EQ(accepted, iterations);
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
        fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
    ));
    ASSERT_EQ(output.size(), assetCount);
    for(usize outputIndex = 0u; outputIndex < assetCount; ++outputIndex){
        const usize assetIndex = assetCount - outputIndex - 1u;
        const auto expectedPath = StringFormat(fixture.metadataArena, "project/fixtures/bundle/asset_{}", assetIndex);
        const auto targetPath = StringFormat(fixture.metadataArena, "project/fixtures/bundle/asset_{}", (assetIndex + 1u) % assetCount);
        EXPECT_EQ(output[outputIndex].virtualPath, Name(AStringView(expectedPath)));
        const Value* const local = output[outputIndex].value.findField("local");
        ASSERT_NE(local, nullptr);
        const Value* const ordinal = local->findField("ordinal");
        ASSERT_NE(ordinal, nullptr);
        EXPECT_EQ(ordinal->asInteger(), static_cast<i64>((assetIndex * 73u) % assetCount));
        const Value* const target = output[outputIndex].value.findField("target");
        ASSERT_NE(target, nullptr);
        EXPECT_EQ(target->asString(), AStringView(targetPath));
    }
    output.clear();
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    RecordUnsignedProperty(MakeNotNull("bunch_expand_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("bunch_expand_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("bunch_declaration_count"), fixture.document.declarations().size());
    RecordUnsignedProperty(MakeNotNull("bunch_export_count"), assetCount);
    RecordUnsignedProperty(MakeNotNull("bunch_reference_count"), assetCount * 2u);
    RecordUnsignedProperty(MakeNotNull("bunch_heap_allocations"), afterHeap.allocationCount - beforeHeap.allocationCount);
    RecordUnsignedProperty(MakeNotNull("bunch_scratch_reserved_bytes"), fixture.scratchArena.memoryStats().reservedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetBunchOwnership, ClearReplacementAndDestructionReleaseAllResolvedValues){
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(s_OwnershipMetadata));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        for(usize iteration = 0u; iteration < 8u; ++iteration){
            ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            VerifyOwnedOutput(output, fixture.document);
            const ArenaMemoryStats liveMetadata = fixture.metadataArena.memoryStats();
            EXPECT_GT(liveMetadata.usedBytes, baselineMetadata.usedBytes);
            ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            VerifyOwnedOutput(output, fixture.document);
            EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, liveMetadata.usedBytes);
            output.clear();
            EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
        }
        ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        VerifyOwnedOutput(output, fixture.document);
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
}

TEST(AssetBunchOwnership, OutputMoveAndGrowthPreserveValuesUntilTheirFinalOwnerDies){
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(s_OwnershipMetadata));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    {
        ExpandedAssetMetadataVector source(fixture.scratchArena);
        ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, source, fixture.scratchArena
        ));
        ExpandedAssetMetadataVector destination(Move(source));
        destination.reserve(destination.capacity() + 37u);
        VerifyOwnedOutput(destination, fixture.document);
        destination[1u].value.field("local").field("label").setString("changed output");
        const Value* const local = fixture.document.findVariable("local");
        ASSERT_NE(local, nullptr);
        const Value* const label = local->findField("label");
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->asString(), "a separately owned metadata string");
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
}

TEST(AssetBunchOwnership, ValuesOutliveTheirSourceDocumentWhileItsArenaRemainsAlive){
    BunchFixture fixture;
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        {
            Metascript::Document sourceDocument(fixture.metadataArena);
            ASSERT_TRUE(sourceDocument.parse(s_OwnershipMetadata));
            ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, sourceDocument, output, fixture.scratchArena
            ));
            VerifyOwnedOutput(output, sourceDocument);
        }
        ASSERT_EQ(output.size(), 2u);
        const Value* const items = output[0u].value.findField("items");
        ASSERT_NE(items, nullptr);
        ASSERT_TRUE(items->isList());
        ASSERT_EQ(items->asList().size(), 2u);
        const Value* const label = items->asList()[0u].findField("label");
        ASSERT_NE(label, nullptr);
        EXPECT_EQ(label->asString(), "a separately owned metadata string");
        const Value* const target = items->asList()[1u].findField("target");
        ASSERT_NE(target, nullptr);
        EXPECT_EQ(target->asString(), "project/fixtures/bundle/first");
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
}

TEST(AssetBunchOwnership, CallerUnwindDestroysEveryPublishedResolvedValue){
    struct CallerFailure{};
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(s_OwnershipMetadata));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    EXPECT_THROW({
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        VerifyOwnedOutput(output, fixture.document);
        throw CallerFailure{};
    }, CallerFailure);
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
}

TEST(AssetBunchOwnership, FailedLaterLocalCycleReleasesTheUnpublishedValueAndPriorOutput){
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(R"(
metadata loop_a;
metadata loop_b;
loop_a = { "copied": [1, 2, 3], "next": loop_b };
loop_b = { "next": loop_a };
probe first = { "owned": "first output remains valid on failure" };
probe second = [ { "copied": [4, 5, 6] }, loop_a ];
asset_bunch bunch = [first, second];
)"));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        for(usize iteration = 0u; iteration < 8u; ++iteration){
            EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            ASSERT_EQ(output.size(), 1u);
            EXPECT_EQ(output[0u].virtualPath, Name("project/fixtures/bundle/first"));
            output.clear();
            EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
        }
        EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("cyclic local metadata reference")));
}

TEST(AssetBunchOwnership, MissingLaterReferenceReleasesPartialNestedCollections){
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(R"(
metadata known;
probe first = { "items": ["copied string", 1, 2] };
probe second = [ { "copied": [4, 5, 6] }, known.missing ];
asset_bunch bunch = [first, second];
)"));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        ASSERT_EQ(output.size(), 1u);
        EXPECT_EQ(output[0u].assetType, Name("probe"));
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("does not target a declared asset")));
}

TEST(AssetBunchOwnership, RegisteredExpanderTransfersValuesToTheModelParserWithoutBorrowedLifetime){
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse(R"(
metadata rig = { "skeleton": "project/shared/skeleton" };
model model;
model.skeletons = { "rig": rig };
asset_bunch bunch = [model];
)"));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    Impl::ModelCookEntry entry(fixture.outputArena);
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        AssetBunchExpandContext context{
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        };
        ASSERT_EQ(TryAutoCollectedAssetBunchExpanders(context), AssetBunchExpandResult::Parsed);
        ASSERT_EQ(output.size(), 1u);
        EXPECT_EQ(output[0u].assetType, Name("model"));
        ASSERT_TRUE(Impl::ParseModelCookMetadata(
            output[0u].virtualPath, fixture.filePath, output[0u].value, entry, fixture.scratchArena
        ));
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    ASSERT_EQ(entry.skeletonObjects.size(), 1u);
    EXPECT_EQ(entry.skeletonObjects[0u].name, Name("rig"));
    EXPECT_EQ(entry.skeletonObjects[0u].skeleton.name(), Name("project/shared/skeleton"));
}

TEST(AssetBunchOwnership, NullValueReachesTheTypeParserAndKeepsItsRejectionContract){
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    BunchFixture fixture;
    ASSERT_TRUE(fixture.document.parse("model model; asset_bunch bunch = [model];"));
    const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
    Impl::ModelCookEntry entry(fixture.outputArena);
    {
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        AssetBunchExpandContext context{
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        };
        ASSERT_EQ(TryAutoCollectedAssetBunchExpanders(context), AssetBunchExpandResult::Parsed);
        ASSERT_EQ(output.size(), 1u);
        EXPECT_TRUE(output[0u].value.isNull());
        EXPECT_FALSE(Impl::ParseModelCookMetadata(
            output[0u].virtualPath, fixture.filePath, output[0u].value, entry, fixture.scratchArena
        ));
    }
    EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("asset is not a map")));
}

TEST(AssetBunchLookup, ExactCaseReferencesPreserveExportAndRepeatedLocalValueOrder){
    for(const usize unusedDeclarations : { 0u, 11u, 12u, 32u }){
        BunchFixture fixture;
        ASSERT_TRUE(ParseLookupFixture(fixture, R"(
metadata Local = { "id": 11 };
metadata local = { "id": 22 };
probe first = { "values": [Local, local, Local] };
probe second = { "values": [local] };
ASSET_BUNCH bunch = [second, first];
)", unusedDeclarations));
        const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        ASSERT_TRUE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        ASSERT_EQ(output.size(), 2u);
        EXPECT_EQ(output[0u].virtualPath, Name("project/fixtures/bundle/second"));
        EXPECT_EQ(output[1u].virtualPath, Name("project/fixtures/bundle/first"));
        const Value* const firstValues = output[1u].value.findField("values");
        ASSERT_NE(firstValues, nullptr);
        ASSERT_TRUE(firstValues->isList());
        ASSERT_EQ(firstValues->asList().size(), 3u);
        const i64 expectedIds[] = { 11, 22, 11 };
        for(usize index = 0u; index < LengthOf(expectedIds); ++index){
            const Value* const id = firstValues->asList()[index].findField("id");
            ASSERT_NE(id, nullptr);
            EXPECT_EQ(id->asInteger(), expectedIds[index]);
        }
        const Value* const secondValues = output[0u].value.findField("values");
        ASSERT_NE(secondValues, nullptr);
        ASSERT_TRUE(secondValues->isList());
        ASSERT_EQ(secondValues->asList().size(), 1u);
        const Value* const secondId = secondValues->asList()[0u].findField("id");
        ASSERT_NE(secondId, nullptr);
        EXPECT_EQ(secondId->asInteger(), 22);
        output.clear();
        EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
    }
}

TEST(AssetBunchLookup, CanonicalDuplicateExportsAreRejectedBeforeNestedResolution){
    for(const usize unusedDeclarations : { 0u, 32u }){
        Tests::CapturingLogger logger;
        Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
        BunchFixture fixture;
        ASSERT_TRUE(ParseLookupFixture(fixture, R"(
metadata known;
probe Asset = { "missing": known.missing };
probe asset;
asset_bunch bunch = [Asset, asset];
)", unusedDeclarations));
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        EXPECT_TRUE(output.empty());
        EXPECT_EQ(logger.errorCount(), 1u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("variable 'asset' is listed more than once")));
        EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("does not target a declared asset")));
    }
}

TEST(AssetBunchLookup, LocalCycleDetectionKeepsCanonicalVariableIdentity){
    for(const usize unusedDeclarations : { 0u, 32u }){
        Tests::CapturingLogger logger;
        Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
        BunchFixture fixture;
        ASSERT_TRUE(ParseLookupFixture(fixture, R"(
metadata Local;
metadata local = { "id": 5 };
Local.next = local;
probe asset = { "nested": Local };
asset_bunch bunch = [asset];
)", unusedDeclarations));
        const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
        ExpandedAssetMetadataVector output(fixture.scratchArena);
        EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
            fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
        ));
        EXPECT_TRUE(output.empty());
        EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
        EXPECT_EQ(logger.errorCount(), 1u);
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("cyclic local metadata reference 'local'")));
    }
}

TEST(AssetBunchLookup, BunchDeclarationsAreExcludedFromExportAndNestedReferenceLookup){
    const AStringView metadataCases[] = {
        "asset_bunch bunch; bunch = [bunch];",
        "probe asset; ASSET_BUNCH bunch; asset.items = [bunch]; bunch = [asset];",
    };
    for(const usize unusedDeclarations : { 0u, 32u }){
        for(usize caseIndex = 0u; caseIndex < LengthOf(metadataCases); ++caseIndex){
            Tests::CapturingLogger logger;
            Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
            BunchFixture fixture;
            ASSERT_TRUE(ParseLookupFixture(fixture, metadataCases[caseIndex], unusedDeclarations));
            const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
            ExpandedAssetMetadataVector output(fixture.scratchArena);
            EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            EXPECT_TRUE(output.empty());
            EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
            EXPECT_EQ(logger.errorCount(), 1u);
            EXPECT_TRUE(logger.sawErrorContaining(caseIndex == 0u
                ? TStringView(NWB_TEXT("item 0 references undeclared asset variable 'bunch'"))
                : TStringView(NWB_TEXT("reference 'bunch' does not target a declared asset"))
            ));
        }
    }
}

TEST(AssetBunchLookup, ExportLookupKeepsFullReferenceTextAndRejectsLiteralItems){
    const AStringView metadataCases[] = {
        "metadata known; probe asset; asset_bunch bunch = [known.missing, asset];",
        "probe asset; asset_bunch bunch = [\"asset\", asset];",
    };
    for(const usize unusedDeclarations : { 0u, 32u }){
        for(usize caseIndex = 0u; caseIndex < LengthOf(metadataCases); ++caseIndex){
            Tests::CapturingLogger logger;
            Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
            BunchFixture fixture;
            ASSERT_TRUE(ParseLookupFixture(fixture, metadataCases[caseIndex], unusedDeclarations));
            ExpandedAssetMetadataVector output(fixture.scratchArena);
            EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            EXPECT_TRUE(output.empty());
            EXPECT_EQ(logger.errorCount(), 1u);
            EXPECT_TRUE(logger.sawErrorContaining(caseIndex == 0u
                ? TStringView(NWB_TEXT("item 0 references undeclared asset variable 'known.missing'"))
                : TStringView(NWB_TEXT("item 0 must be a declared asset reference"))
            ));
        }
    }
}

TEST(AssetBunchLookup, NestedListFailureFollowsSourceTraversalOrder){
    const AStringView metadataCases[] = {
        "metadata known; metadata cycle; cycle.next = cycle; "
        "probe asset = [known.missing, cycle]; asset_bunch bunch = [asset];",
        "metadata known; metadata cycle; cycle.next = cycle; "
        "probe asset = [cycle, known.missing]; asset_bunch bunch = [asset];",
    };
    for(const usize unusedDeclarations : { 0u, 32u }){
        for(usize caseIndex = 0u; caseIndex < LengthOf(metadataCases); ++caseIndex){
            Tests::CapturingLogger logger;
            Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
            BunchFixture fixture;
            ASSERT_TRUE(ParseLookupFixture(fixture, metadataCases[caseIndex], unusedDeclarations));
            const ArenaMemoryStats baselineMetadata = fixture.metadataArena.memoryStats();
            ExpandedAssetMetadataVector output(fixture.scratchArena);
            EXPECT_FALSE(AssetsBunchCook::ExpandAssetBunch(
                fixture.assetRoot, "project", fixture.filePath, fixture.document, output, fixture.scratchArena
            ));
            EXPECT_TRUE(output.empty());
            EXPECT_EQ(fixture.metadataArena.memoryStats().usedBytes, baselineMetadata.usedBytes);
            EXPECT_EQ(logger.errorCount(), 1u);
            EXPECT_EQ(logger.sawErrorContaining(NWB_TEXT("does not target a declared asset")), caseIndex == 0u);
            EXPECT_EQ(logger.sawErrorContaining(NWB_TEXT("cyclic local metadata reference")), caseIndex == 1u);
        }
    }
}

TEST(AssetBunchBenchmark, DISABLED_FourAssetsAndFourLocalDeclarations){
    BenchmarkDeclarationExpansion(4u, 256u);
}

TEST(AssetBunchBenchmark, DISABLED_256AssetsAnd256LocalDeclarations){
    BenchmarkDeclarationExpansion(256u, 3u);
}

TEST(AssetBunchBenchmark, DISABLED_1024AssetsAnd1024LocalDeclarations){
    BenchmarkDeclarationExpansion(1024u, 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

