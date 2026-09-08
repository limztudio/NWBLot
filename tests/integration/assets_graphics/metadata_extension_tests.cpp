// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/assets/cook_metadata.h>
#include <core/alloc/cpu_task.h>

#include <impl/assets_model/cook.h>
#include <impl/assets_sampler/cook.h>

#include <tests/common/capturing_logger.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metadata_extension_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


struct ExtensionState{
    usize constructions = 0u;
    usize destructions = 0u;
    usize memberDestructions = 0u;
};

struct ConstructionFailure{};
struct InsertionFailure{};
struct CallerFailure{};

struct ExtensionMember{
    ExtensionState& state;
    AssetVector<u8> bytes;

    ExtensionMember(AssetArena& arena, ExtensionState& inState)
        : state(inState)
        , bytes(arena)
    {
        bytes.resize(513u, 0x5au);
    }
    ~ExtensionMember()noexcept{
        ++state.memberDestructions;
    }
};

struct alignas(128u) AlignedExtension{
    ExtensionMember member;
    ExtensionState& state;
    usize identity;

    AlignedExtension(AssetArena& arena, ExtensionState& inState, const bool fail = false)
        : member(arena, inState)
        , state(inState)
        , identity(++state.constructions)
    {
        if(fail)
            throw ConstructionFailure{};
    }
    ~AlignedExtension()noexcept{
        ++state.destructions;
    }
};

struct TextExtension{
    ExtensionState& state;
    AssetVector<AssetString> strings;

    TextExtension(AssetArena& arena, ExtensionState& inState)
        : state(inState)
        , strings(arena)
    {
        ++state.constructions;
        strings.emplace_back("first separately allocated extension payload", arena);
        strings.emplace_back("second separately allocated extension payload", arena);
    }
    ~TextExtension()noexcept{
        ++state.destructions;
    }
};

struct ReentrantExtension{
    ExtensionState& state;
    usize identity;
    ReentrantExtension* nested = nullptr;

    ReentrantExtension(ParsedAssetMetadata& metadata, const Name& name, ExtensionState& inState, const bool recurse)
        : state(inState)
        , identity(++state.constructions)
    {
        if(recurse)
            nested = &RequireParsedMetadataExtension<ReentrantExtension>(metadata, name, metadata, name, state, false);
    }
    ~ReentrantExtension()noexcept{
        ++state.destructions;
    }
};

struct ThrowingExtensionHasher{
    NotNull<bool*> fail;

    [[nodiscard]] usize operator()(const Name& key)const{
        if(*fail)
            throw InsertionFailure{};
        return Hasher<Name>{}(key);
    }
};

struct RegistryBorrowingExtension{
    const CookEntryRegistry& registry;
    usize& observedBucketCount;

    ~RegistryBorrowingExtension()noexcept{
        observedBucketCount = registry.bucketCount();
    }
};

static_assert(!IsConstructible_V<ParsedMetadataExtension, const ParsedMetadataExtension&>);
static_assert(IsNothrowMoveConstructible_V<ParsedMetadataExtension>);
static_assert(IsNothrowDestructible_V<ParsedMetadataExtension>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

[[nodiscard]] static bool WriteFixtureFile(const NWB::Path& path, const AStringView text){
    GlobalFilesystemDetail::OutputFileStream file(path, GlobalFilesystemDetail::OutputFileStream::binary);
    file.write(text.data(), static_cast<GlobalFilesystemDetail::StreamSize>(text.size()));
    file.close();
    return !file.fail();
}

static void BenchmarkMetadataParsing(const usize pairCount, const usize iterations){
    AssetArena fixtureArena(Name("tests/metadata_extension/parse_fixture"));
    AssetArena parseArena(Name("tests/metadata_extension/parse_output"));
    Alloc::CpuTaskScheduler cpuScheduler(0u);
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const AssetString caseName = StringFormat(fixtureArena, "pairs_{}", pairCount);
    const NWB::Path root = NWB::Path(fixtureArena, __FILE__).parent_path().parent_path().parent_path().parent_path()
        / "__build_obj" / "metadata_extension_tests" / caseName;
    DiscoveredNwbFileVector files(fixtureArena);
    files.reserve(pairCount * 2u);
    for(usize index = 0u; index < pairCount; ++index){
        const AssetString directoryName = StringFormat(fixtureArena, "pair_{:04}", index);
        const NWB::Path directory = root / directoryName;
        ErrorCode error;
        ASSERT_TRUE(EnsureDirectories(directory, error));
        ASSERT_TRUE(WriteFixtureFile(directory / "shader.slang", "[numthreads(1, 1, 1)] void main(){}\r\n"));
        ASSERT_TRUE(WriteFixtureFile(directory / "include.slangi", "static const uint fixtureValue = 1;\r\n"));
        ASSERT_TRUE(WriteFixtureFile(directory / "shader.nwb", "shader asset;\r\nasset.stage = \"cs\";\r\nasset.target_profile = \"spirv_1_5\";\r\nasset.entry_point = \"main\";\r\n"));
        ASSERT_TRUE(WriteFixtureFile(directory / "include.nwb", "include asset;\r\nasset.defines = { \"FIXTURE_OPTION\": [\"0\", \"1\"] };\r\n"));
        for(const AStringView fileName : { AStringView("shader.nwb"), AStringView("include.nwb") }){
            const NWB::Path path = directory / fileName;
            AssetString normalized = PathToString(fixtureArena, path.lexically_normal());
            CanonicalizeTextInPlace(normalized);
            files.emplace_back(fixtureArena, root, path, normalized, ACompactString("project"));
        }
    }

    usize registeredBucketCount = 0u;
    {
        Alloc::ScratchArena scratchArena(Name("tests/metadata_extension/parse_scratch"));
        ParsedAssetMetadata metadata(parseArena);
        ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry));
        registeredBucketCount = metadata.entryRegistry.bucketCount();
        ASSERT_GE(registeredBucketCount, 7u);
        ASSERT_TRUE(ParseAssetMetadata(parseArena, files, metadata, cpuScheduler, scratchArena));
        ASSERT_EQ(metadata.entryRegistry.entryCount(), 0u);
        ASSERT_EQ(metadata.extensions.size(), 1u);
    }
    ASSERT_EQ(parseArena.memoryStats().usedBytes, 0u);

    u64 elapsedNanoseconds = 0u;
    u64 liveBytes = 0u;
    const ArenaMemoryStats before = parseArena.memoryStats();
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        bool registered = false;
        bool parsed = false;
        u64 entryCount = 0u;
        usize extensionCount = 0u;
        usize bucketCount = 0u;
        u64 iterationLiveBytes = 0u;
        const Timer begin = TimerNow();
        {
            Alloc::ScratchArena scratchArena(Name("tests/metadata_extension/parse_scratch"));
            ParsedAssetMetadata metadata(parseArena);
            registered = RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry);
            parsed = registered && ParseAssetMetadata(parseArena, files, metadata, cpuScheduler, scratchArena);
            entryCount = metadata.entryRegistry.entryCount();
            extensionCount = metadata.extensions.size();
            bucketCount = metadata.entryRegistry.bucketCount();
            iterationLiveBytes = parseArena.memoryStats().usedBytes;
        }
        const Timer end = TimerNow();
        elapsedNanoseconds += DurationInNS<u64>(end, begin);
        ASSERT_TRUE(registered);
        ASSERT_TRUE(parsed);
        ASSERT_EQ(entryCount, 0u);
        ASSERT_EQ(extensionCount, 1u);
        ASSERT_EQ(bucketCount, registeredBucketCount);
        ASSERT_EQ(parseArena.memoryStats().usedBytes, before.usedBytes);
        liveBytes = Max(liveBytes, iterationLiveBytes);
    }
    const ArenaMemoryStats after = parseArena.memoryStats();
    EXPECT_EQ(logger.errorCount(), 0u);
    EXPECT_GT(liveBytes, 0u);
    RecordUnsignedProperty(MakeNotNull("metadata_parse_ns"), elapsedNanoseconds);
    RecordUnsignedProperty(MakeNotNull("metadata_file_count"), files.size());
    RecordUnsignedProperty(MakeNotNull("metadata_registered_buckets"), registeredBucketCount);
    RecordUnsignedProperty(MakeNotNull("metadata_parse_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("metadata_parse_allocations"), after.allocationCount - before.allocationCount);
    RecordUnsignedProperty(MakeNotNull("metadata_parse_reallocations"), after.reallocationCount - before.reallocationCount);
    RecordUnsignedProperty(MakeNotNull("metadata_parse_deallocations"), after.deallocationCount - before.deallocationCount);
    RecordUnsignedProperty(MakeNotNull("metadata_retained_live_bytes"), liveBytes);
    RecordUnsignedProperty(MakeNotNull("metadata_peak_used_bytes"), after.peakUsedBytes);
    RecordUnsignedProperty(MakeNotNull("metadata_final_used_bytes"), after.usedBytes);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MetadataExtensionOwnership, RequireRetainsAlignedTypedPayloadAndReturnsTheStoredObject){
    AssetArena arena(Name("tests/metadata_extension/require"));
    ExtensionState alignedState;
    ExtensionState textState;
    const u64 baseline = arena.memoryStats().usedBytes;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name("tests/metadata_extension/aligned");
        AlignedExtension& first = RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, alignedState);
        const u64 afterFirst = arena.memoryStats().allocationCount;
        AlignedExtension& second = RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, alignedState, true);
        EXPECT_EQ(&first, &second);
        EXPECT_EQ(arena.memoryStats().allocationCount, afterFirst);
        EXPECT_EQ(reinterpret_cast<usize>(&first) % alignof(AlignedExtension), 0u);
        EXPECT_EQ(first.member.bytes.size(), 513u);
        EXPECT_EQ(first.member.bytes.back(), 0x5au);
        const ParsedAssetMetadata& borrowed = metadata;
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(borrowed, name), &first);
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(metadata, Name("tests/metadata_extension/absent")), nullptr);
        TextExtension& text = RequireParsedMetadataExtension<TextExtension>(metadata, Name("tests/metadata_extension/text"), arena, textState);
        ASSERT_EQ(text.strings.size(), 2u);
        EXPECT_EQ(text.strings[1u], "second separately allocated extension payload");
        EXPECT_EQ(metadata.extensions.size(), 2u);
    }
    EXPECT_EQ(alignedState.constructions, 1u);
    EXPECT_EQ(alignedState.destructions, 1u);
    EXPECT_EQ(alignedState.memberDestructions, 1u);
    EXPECT_EQ(textState.destructions, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, baseline);
}

TEST(MetadataExtensionOwnership, EraseClearAndGrowthDestroyEveryConcretePayloadOnce){
    AssetArena arena(Name("tests/metadata_extension/erase_clear"));
    ExtensionState state;
    const u64 baseline = arena.memoryStats().usedBytes;
    {
        ParsedAssetMetadata metadata(arena);
        const Name firstName("tests/metadata_extension/first");
        AlignedExtension* const first = &RequireParsedMetadataExtension<AlignedExtension>(metadata, firstName, arena, state);
        for(usize index = 0u; index < 80u; ++index){
            const AssetString nameText = StringFormat(arena, "tests/metadata_extension/growth_{}", index);
            const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(metadata, ToName(nameText), arena, state);
            EXPECT_EQ(created.identity, index + 2u);
        }
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(metadata, firstName), first);
        EXPECT_EQ(metadata.extensions.erase(firstName), 1u);
        EXPECT_EQ(state.destructions, 1u);
        metadata.extensions.clear();
        EXPECT_EQ(state.destructions, 81u);
        EXPECT_EQ(state.memberDestructions, 81u);
        EXPECT_TRUE(metadata.extensions.empty());
    }
    EXPECT_EQ(state.constructions, state.destructions);
    EXPECT_EQ(arena.memoryStats().usedBytes, baseline);
}

TEST(MetadataExtensionOwnership, MapMoveAndReplacementUseEachOriginalObjectArena){
    AssetArena firstMapArena(Name("tests/metadata_extension/map_first"));
    AssetArena secondMapArena(Name("tests/metadata_extension/map_second"));
    AssetArena firstOwnerArena(Name("tests/metadata_extension/owner_first"));
    AssetArena secondOwnerArena(Name("tests/metadata_extension/owner_second"));
    ExtensionState firstState;
    ExtensionState secondState;
    const Name firstName("tests/metadata_extension/first");
    const Name otherName("tests/metadata_extension/other");
    {
        ParsedMetadataExtensionMap firstMap(0u, Hasher<Name>{}, EqualTo<Name>{}, firstMapArena);
        ParsedMetadataExtension first = MakeParsedMetadataExtension<AlignedExtension>(firstOwnerArena, firstOwnerArena, firstState);
        void* const firstAddress = first.get();
        ASSERT_TRUE(firstMap.try_emplace(firstName, Move(first)).second);
        ParsedMetadataExtensionMap movedMap(Move(firstMap));
        EXPECT_EQ(movedMap.at(firstName).get(), firstAddress);
        EXPECT_TRUE(firstMap.empty());
        ParsedMetadataExtensionMap secondMap(0u, Hasher<Name>{}, EqualTo<Name>{}, secondMapArena);
        ASSERT_TRUE(secondMap.try_emplace(otherName, MakeParsedMetadataExtension<TextExtension>(secondOwnerArena, secondOwnerArena, secondState)).second);
        secondMap = Move(movedMap);
        EXPECT_EQ(secondState.destructions, 1u);
        EXPECT_EQ(secondOwnerArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(secondMap.at(firstName).get(), firstAddress);
        EXPECT_TRUE(movedMap.empty());
        secondMap.at(firstName) = MakeParsedMetadataExtension<TextExtension>(secondOwnerArena, secondOwnerArena, secondState);
        EXPECT_EQ(firstState.destructions, 1u);
        EXPECT_EQ(firstState.memberDestructions, 1u);
        EXPECT_EQ(firstOwnerArena.memoryStats().usedBytes, 0u);
        ASSERT_GT(secondOwnerArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(static_cast<TextExtension*>(secondMap.at(firstName).get())->strings.size(), 2u);
    }
    EXPECT_EQ(firstState.destructions, 1u);
    EXPECT_EQ(secondState.destructions, 2u);
    EXPECT_EQ(firstMapArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(secondMapArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(firstOwnerArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(secondOwnerArena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, RequireRefillsAnExplicitlyMovedOutOwner){
    AssetArena arena(Name("tests/metadata_extension/moved_slot"));
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name("tests/metadata_extension/moved_slot");
        AlignedExtension* const first = &RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state);
        ParsedMetadataExtension detached = Move(metadata.extensions.at(name));
        EXPECT_EQ(detached.get(), first);
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(metadata, name), nullptr);
        AlignedExtension& replacement = RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state);
        EXPECT_NE(&replacement, first);
        EXPECT_EQ(replacement.identity, 2u);
        EXPECT_EQ(metadata.extensions.size(), 1u);
        detached.reset();
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, 2u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, ConstructorUnwindReleasesStorageAndConstructedMembers){
    AssetArena arena(Name("tests/metadata_extension/constructor_unwind"));
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name("tests/metadata_extension/constructor_unwind");
        const u64 baseline = arena.memoryStats().usedBytes;
        EXPECT_THROW({
            const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state, true);
            EXPECT_EQ(created.identity, 1u);
        }, ConstructionFailure);
        EXPECT_TRUE(metadata.extensions.empty());
        EXPECT_EQ(state.constructions, 1u);
        EXPECT_EQ(state.destructions, 0u);
        EXPECT_EQ(state.memberDestructions, 1u);
        EXPECT_EQ(arena.memoryStats().usedBytes, baseline);
    }
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, CallerUnwindDestroysPublishedExtensions){
    AssetArena arena(Name("tests/metadata_extension/caller_unwind"));
    ExtensionState state;
    EXPECT_THROW({
        ParsedAssetMetadata metadata(arena);
        const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(metadata, Name("tests/metadata_extension/caller_unwind"), arena, state);
        EXPECT_EQ(created.identity, 1u);
        throw CallerFailure{};
    }, CallerFailure);
    EXPECT_EQ(state.destructions, 1u);
    EXPECT_EQ(state.memberDestructions, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, ReentrantInsertionReturnsTheActualStoredValue){
    AssetArena arena(Name("tests/metadata_extension/reentrant"));
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name("tests/metadata_extension/reentrant");
        ReentrantExtension& result = RequireParsedMetadataExtension<ReentrantExtension>(metadata, name, metadata, name, state, true);
        EXPECT_EQ(result.identity, 2u);
        EXPECT_EQ(result.nested, nullptr);
        EXPECT_EQ(&result, FindParsedMetadataExtension<ReentrantExtension>(metadata, name));
        EXPECT_EQ(metadata.extensions.size(), 1u);
        EXPECT_EQ(state.constructions, 2u);
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, 2u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, InsertionUnwindDestroysThePendingErasedOwner){
    AssetArena mapArena(Name("tests/metadata_extension/insertion_map"));
    AssetArena ownerArena(Name("tests/metadata_extension/insertion_owner"));
    ExtensionState state;
    bool fail = false;
    {
        HashMap<Name, ParsedMetadataExtension, ThrowingExtensionHasher, EqualTo<Name>, AssetArena> values(0u, ThrowingExtensionHasher{ MakeNotNull(&fail) }, EqualTo<Name>{}, mapArena);
        values.reserve(4u);
        const u64 mapBaseline = mapArena.memoryStats().usedBytes;
        fail = true;
        EXPECT_THROW({
            ParsedMetadataExtension pending = MakeParsedMetadataExtension<AlignedExtension>(ownerArena, ownerArena, state);
            EXPECT_TRUE(values.try_emplace(Name("tests/metadata_extension/insertion_unwind"), Move(pending)).second);
        }, InsertionFailure);
        fail = false;
        EXPECT_TRUE(values.empty());
        EXPECT_EQ(state.destructions, 1u);
        EXPECT_EQ(state.memberDestructions, 1u);
        EXPECT_EQ(ownerArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(mapArena.memoryStats().usedBytes, mapBaseline);
    }
    EXPECT_EQ(mapArena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, ExtensionsRetireBeforeTheEntryRegistryTheyBorrow){
    AssetArena arena(Name("tests/metadata_extension/registry_lifetime"));
    usize observedBucketCount = 0u;
    usize expectedBucketCount = 0u;
    {
        ParsedAssetMetadata metadata(arena);
        ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry));
        expectedBucketCount = metadata.entryRegistry.bucketCount();
        ASSERT_GE(expectedBucketCount, 7u);
        const RegistryBorrowingExtension& created = RequireParsedMetadataExtension<RegistryBorrowingExtension>(metadata, Name("tests/metadata_extension/registry_lifetime"), metadata.entryRegistry, observedBucketCount);
        EXPECT_EQ(&created.registry, &metadata.entryRegistry);
    }
    EXPECT_EQ(observedBucketCount, expectedBucketCount);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, GraphicsParserFailureReleasesItsActualExtensionPayload){
    AssetArena fixtureArena(Name("tests/metadata_extension/graphics_fixture"));
    AssetArena ownerArena(Name("tests/metadata_extension/graphics_owner"));
    Alloc::ScratchArena scratchArena(Name("tests/metadata_extension/graphics_scratch"));
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Metascript::Document document(fixtureArena);
    ASSERT_TRUE(document.parse("shader asset;"));
    const NWB::Path root(fixtureArena, "tests/metadata_extension/assets");
    const NWB::Path path = root / "invalid_shader.nwb";
    const DiscoveredNwbFile file(fixtureArena, root, path, "tests/metadata_extension/assets/invalid_shader.nwb", ACompactString("project"));
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        {
            ParsedAssetMetadata metadata(ownerArena);
            AssetDocumentMetadataParseContext context{ ownerArena, file, Name("shader"), document, metadata, scratchArena };
            EXPECT_EQ(TryAutoCollectedDocumentMetadataParsers(context), AssetMetadataParseResult::Error);
            EXPECT_EQ(metadata.extensions.size(), 1u);
            EXPECT_GT(ownerArena.memoryStats().usedBytes, 0u);
        }
        EXPECT_EQ(ownerArena.memoryStats().usedBytes, 0u);
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("asset is not a map")));
    EXPECT_TRUE(document.asset().isNull());
}

TEST(MetadataExtensionOwnership, PublicShaderAndIncludeParsingRetiresAllMetadata){
    BenchmarkMetadataParsing(1u, 2u);
}

TEST(MetadataRegistryStorage, TypedGrowthPreservesInputOrderAndDoesNotReserveUnusedBuckets){
    AssetArena fixtureArena(Name("tests/metadata_registry/fixture"));
    AssetArena parseArena(Name("tests/metadata_registry/output"));
    Alloc::CpuTaskScheduler cpuScheduler(0u);
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const NWB::Path root = NWB::Path(fixtureArena, __FILE__).parent_path().parent_path().parent_path().parent_path()
        / "__build_obj" / "metadata_registry_storage";
    ErrorCode error;
    ASSERT_TRUE(EnsureDirectories(root, error));
    DiscoveredNwbFileVector files(fixtureArena);
    constexpr usize s_SamplerCount = 65u;
    files.reserve(s_SamplerCount + 1u);
    constexpr AStringView s_SamplerFields =
        "sampler asset;\r\n"
        "asset.min_filter = \"linear\";\r\n"
        "asset.mag_filter = \"linear\";\r\n"
        "asset.mip_filter = \"linear\";\r\n"
        "asset.address_u = \"wrap\";\r\n"
        "asset.address_v = \"wrap\";\r\n"
        "asset.address_w = \"wrap\";\r\n"
        "asset.reduction = \"standard\";\r\n"
        "asset.max_anisotropy = 1.0;\r\n"
        "asset.border_color = [0.0, 0.0, 0.0, 0.0];\r\n";
    for(usize index = 0u; index < s_SamplerCount; ++index){
        const usize identity = (index * 37u) % s_SamplerCount;
        const AssetString filename = StringFormat(fixtureArena, "sampler_{:03}.nwb", identity);
        const NWB::Path path = root / filename;
        AssetString source(s_SamplerFields, fixtureArena);
        source += StringFormat(fixtureArena, "asset.mip_bias = {};\r\n", identity);
        ASSERT_TRUE(WriteFixtureFile(path, source));
        const AssetString normalized = PathToString(fixtureArena, path.lexically_normal());
        files.emplace_back(fixtureArena, root, path, normalized, ACompactString("project"));
    }
    for(const bool rejectDuplicate : { false, true }){
        if(rejectDuplicate)
            files.push_back(files.front());
        {
            Alloc::ScratchArena scratchArena(Name("tests/metadata_registry/scratch"));
            ParsedAssetMetadata metadata(parseArena);
            ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry));
            auto& samplers = metadata.entryRegistry.entries<Impl::SamplerCookEntry>(Impl::Sampler::AssetTypeName());
            auto& models = metadata.entryRegistry.entries<Impl::ModelCookEntry>(Impl::Model::AssetTypeName());
            ASSERT_EQ(samplers.capacity(), 0u);
            ASSERT_EQ(models.capacity(), 0u);
            EXPECT_EQ(ParseAssetMetadata(parseArena, files, metadata, cpuScheduler, scratchArena), !rejectDuplicate);
            ASSERT_EQ(samplers.size(), s_SamplerCount);
            EXPECT_GE(samplers.capacity(), s_SamplerCount);
            EXPECT_TRUE(models.empty());
            EXPECT_EQ(models.capacity(), 0u);
            EXPECT_TRUE(metadata.extensions.empty());
            EXPECT_EQ(metadata.entryRegistry.entryCount(), s_SamplerCount);
            for(usize index = 0u; index < s_SamplerCount; ++index){
                const usize identity = (index * 37u) % s_SamplerCount;
                const AssetString virtualPath = StringFormat(fixtureArena, "project/sampler_{:03}", identity);
                EXPECT_EQ(samplers[index].virtualPath, ToName(virtualPath));
                EXPECT_EQ(samplers[index].arena, &parseArena);
                EXPECT_FLOAT_EQ(samplers[index].description.mipBias, static_cast<f32>(identity));
            }
        }
        EXPECT_EQ(parseArena.memoryStats().usedBytes, 0u);
        if(rejectDuplicate)
            EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("duplicate property asset virtual path")));
        else
            EXPECT_EQ(logger.errorCount(), 0u);
    }
}

TEST(MetadataExtensionBenchmark, DISABLED_ParseShaderAndIncludePair){
    BenchmarkMetadataParsing(1u, 32u);
}

TEST(MetadataExtensionBenchmark, DISABLED_ParseShaderAndIncludeRegistry512Files){
    BenchmarkMetadataParsing(256u, 3u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

