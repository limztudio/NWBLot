// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <tests/integration/assets_graphics/case_types.adhd.h>

#include <impl/assets_model/cook.h>
#include <impl/assets_sampler/cook.h>
#include <impl/assets_shader/asset.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/paths.h>
#include <core/common/name_symbols.h>
#include <core/task/cpu/scheduler.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metadata_extension_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_FirstMetadataName = "tests/metadata_extension/first";
constexpr AStringView s_MovedSlotMetadataName = "tests/metadata_extension/moved_slot";
constexpr AStringView s_ConstructorUnwindMetadataName = "tests/metadata_extension/constructor_unwind";
constexpr AStringView s_CallerUnwindMetadataName = "tests/metadata_extension/caller_unwind";
constexpr AStringView s_ReentrantMetadataName = "tests/metadata_extension/reentrant";
constexpr AStringView s_RegistryLifetimeMetadataName = "tests/metadata_extension/registry_lifetime";
constexpr AStringView s_ProjectVirtualRoot = "project";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


using ExtensionState = NWB::Tests::AssetsGraphicsFixture::MetadataExtensionState;

struct ConstructionFailure{};
struct InsertionFailure{};
struct CallerFailure{};

struct ExtensionMember{
    ExtensionState& state;
    AssetVector<u8> bytes;


public:
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


public:
    AlignedExtension(AssetArena& arena, ExtensionState& inState, const b8 fail = false)
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


public:
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


public:
    ReentrantExtension(ParsedAssetMetadata& metadata, const Name& name, ExtensionState& inState, const b8 recurse)
        : state(inState)
        , identity(++state.constructions)
    {
        if(recurse)
            nested = &RequireParsedMetadataExtension<ReentrantExtension>(name, metadata, metadata, name, state, false);
    }
    ~ReentrantExtension()noexcept{
        ++state.destructions;
    }
};

struct ThrowingExtensionHasher{
    NotNull<b8*> fail;


public:
    [[nodiscard]] usize operator()(const Name& key)const{
        if(*fail)
            throw InsertionFailure{};
        return Hasher<Name>{}(key);
    }
};

struct RegistryBorrowingExtension{
    const CookEntryRegistry& registry;
    usize& observedBucketCount;


public:
    ~RegistryBorrowingExtension()noexcept{
        observedBucketCount = registry.bucketCount();
    }
};


static_assert(!IsConstructible_V<ParsedMetadataExtension, const ParsedMetadataExtension&>);
static_assert(IsNothrowMoveConstructible_V<ParsedMetadataExtension>);
static_assert(IsNothrowDestructible_V<ParsedMetadataExtension>);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(MetadataExtensionOwnership, RequireRetainsAlignedTypedPayloadAndReturnsTheStoredObject){
    AssetArena arena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionRequire)};
    ExtensionState alignedState;
    ExtensionState textState;
    const u64 baseline = arena.memoryStats().usedBytes;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name = NWB::Tests::AssetsGraphicsFixture::s_MetadataExtensionAligned;
        AlignedExtension& first = RequireParsedMetadataExtension<AlignedExtension>(name, metadata, arena, alignedState);
        const u64 afterFirst = arena.memoryStats().allocationCount;
        AlignedExtension& second = RequireParsedMetadataExtension<AlignedExtension>(name, metadata, arena, alignedState, true);
        EXPECT_EQ(&first, &second);
        EXPECT_EQ(arena.memoryStats().allocationCount, afterFirst);
        EXPECT_EQ(reinterpret_cast<usize>(&first) % alignof(AlignedExtension), 0u);
        const ParsedAssetMetadata& borrowed = metadata;
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(name, borrowed), &first);
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(Name(NWB::Tests::AssetsGraphicsFixture::s_AssetFixtureNameTestsMetadataExtensionAbsent), metadata), nullptr);
        const Name textName = NWB::Tests::AssetsGraphicsFixture::s_MetadataExtensionText;
        const TextExtension& text = RequireParsedMetadataExtension<TextExtension>(textName, metadata, arena, textState);
        EXPECT_EQ(FindParsedMetadataExtension<TextExtension>(textName, metadata), &text);
    }
    EXPECT_EQ(alignedState.constructions, 1u);
    EXPECT_EQ(alignedState.destructions, 1u);
    EXPECT_EQ(alignedState.memberDestructions, 1u);
    EXPECT_EQ(textState.destructions, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, baseline);
}

TEST(MetadataExtensionOwnership, EraseClearAndGrowthDestroyEveryConcretePayloadOnce){
    AssetArena arena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionEraseClear)};
    ExtensionState state;
    const u64 baseline = arena.memoryStats().usedBytes;
    {
        ParsedAssetMetadata metadata(arena);
        const Name firstName(s_FirstMetadataName);
        const NotNull<AlignedExtension*> first = MakeNotNull(&RequireParsedMetadataExtension<AlignedExtension>(firstName, metadata, arena, state));
        for(usize index = 0u; index < 80u; ++index){
            const AssetString nameText = StringFormat(arena, "tests/metadata_extension/growth_{}", index);
            const auto nameResult = ToName(nameText);
            ASSERT_TRUE(nameResult);
            ASSERT_TRUE(*nameResult);
            const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(**nameResult, metadata, arena, state);
            EXPECT_EQ(created.identity, index + 2u);
        }
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(firstName, metadata), first.get());
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
    AssetArena firstMapArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionMapFirst)};
    AssetArena secondMapArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionMapSecond)};
    AssetArena firstOwnerArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionOwnerFirst)};
    AssetArena secondOwnerArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionOwnerSecond)};
    ExtensionState firstState;
    ExtensionState secondState;
    const Name firstName(s_FirstMetadataName);
    const Name otherName = NWB::Tests::AssetsGraphicsFixture::s_MetadataExtensionOther;
    {
        ParsedMetadataExtensionMap firstMap(0u, Hasher<Name>{}, EqualTo<Name>{}, firstMapArena);
        ParsedMetadataExtension first = MakeParsedMetadataExtension<AlignedExtension>(firstOwnerArena, firstOwnerArena, firstState);
        const NotNull<void*> firstAddress = MakeNotNull(first.get());
        ASSERT_TRUE(firstMap.try_emplace(firstName, Move(first)).second);
        ParsedMetadataExtensionMap movedMap(Move(firstMap));
        EXPECT_EQ(movedMap.at(firstName).get(), firstAddress.get());
        EXPECT_TRUE(firstMap.empty());
        ParsedMetadataExtensionMap secondMap(0u, Hasher<Name>{}, EqualTo<Name>{}, secondMapArena);
        ASSERT_TRUE(secondMap.try_emplace(otherName, MakeParsedMetadataExtension<TextExtension>(secondOwnerArena, secondOwnerArena, secondState)).second);
        secondMap = Move(movedMap);
        EXPECT_EQ(secondState.destructions, 1u);
        EXPECT_EQ(secondOwnerArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(secondMap.at(firstName).get(), firstAddress.get());
        EXPECT_TRUE(movedMap.empty());
        secondMap.at(firstName) = MakeParsedMetadataExtension<TextExtension>(secondOwnerArena, secondOwnerArena, secondState);
        EXPECT_EQ(firstState.destructions, 1u);
        EXPECT_EQ(firstState.memberDestructions, 1u);
        EXPECT_EQ(firstOwnerArena.memoryStats().usedBytes, 0u);
        ASSERT_GT(secondOwnerArena.memoryStats().usedBytes, 0u);
        EXPECT_EQ(MakeNotNull(static_cast<TextExtension*>(secondMap.at(firstName).get()))->strings.size(), 2u);
    }
    EXPECT_EQ(firstState.destructions, 1u);
    EXPECT_EQ(secondState.destructions, 2u);
    EXPECT_EQ(firstMapArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(secondMapArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(firstOwnerArena.memoryStats().usedBytes, 0u);
    EXPECT_EQ(secondOwnerArena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, RequireRefillsAnExplicitlyMovedOutOwner){
    AssetArena arena{ Name(s_MovedSlotMetadataName) };
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name(s_MovedSlotMetadataName);
        const NotNull<AlignedExtension*> first = MakeNotNull(&RequireParsedMetadataExtension<AlignedExtension>(name, metadata, arena, state));
        ParsedMetadataExtension detached = Move(metadata.extensions.at(name));
        EXPECT_EQ(detached.get(), first.get());
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(name, metadata), nullptr);
        AlignedExtension& replacement = RequireParsedMetadataExtension<AlignedExtension>(name, metadata, arena, state);
        EXPECT_NE(&replacement, first.get());
        EXPECT_EQ(replacement.identity, 2u);
        EXPECT_EQ(metadata.extensions.size(), 1u);
        detached.reset();
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, 2u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, ConstructorUnwindReleasesStorageAndConstructedMembers){
    AssetArena arena{ Name(s_ConstructorUnwindMetadataName) };
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name(s_ConstructorUnwindMetadataName);
        const u64 baseline = arena.memoryStats().usedBytes;
        EXPECT_THROW({
            EXPECT_EQ(RequireParsedMetadataExtension<AlignedExtension>(name, metadata, arena, state, true).identity, 1u);
        }, ConstructionFailure
        );
        EXPECT_TRUE(metadata.extensions.empty());
        EXPECT_EQ(state.constructions, 1u);
        EXPECT_EQ(state.destructions, 0u);
        EXPECT_EQ(state.memberDestructions, 1u);
        EXPECT_EQ(arena.memoryStats().usedBytes, baseline);
    }
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, CallerUnwindDestroysPublishedExtensions){
    AssetArena arena{ Name(s_CallerUnwindMetadataName) };
    ExtensionState state;
    EXPECT_THROW({
        ParsedAssetMetadata metadata(arena);
        const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(Name(s_CallerUnwindMetadataName), metadata, arena, state);
        EXPECT_EQ(created.identity, 1u);
        throw CallerFailure{};
    }, CallerFailure
    );
    EXPECT_EQ(state.destructions, 1u);
    EXPECT_EQ(state.memberDestructions, 1u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, ReentrantInsertionReturnsTheActualStoredValue){
    AssetArena arena{ Name(s_ReentrantMetadataName) };
    ExtensionState state;
    {
        ParsedAssetMetadata metadata(arena);
        const Name name(s_ReentrantMetadataName);
        ReentrantExtension& result = RequireParsedMetadataExtension<ReentrantExtension>(name, metadata, metadata, name, state, true);
        EXPECT_EQ(result.identity, 2u);
        EXPECT_EQ(result.nested, nullptr);
        EXPECT_EQ(&result, FindParsedMetadataExtension<ReentrantExtension>(name, metadata));
        EXPECT_EQ(metadata.extensions.size(), 1u);
        EXPECT_EQ(state.constructions, 2u);
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, 2u);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, InsertionUnwindDestroysThePendingErasedOwner){
    AssetArena mapArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionInsertionMap)};
    AssetArena ownerArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionInsertionOwner)};
    ExtensionState state;
    b8 fail = false;
    {
        HashMap<Name, ParsedMetadataExtension, AssetArena, ThrowingExtensionHasher, EqualTo<Name>> values(0u, ThrowingExtensionHasher{ MakeNotNull(&fail) }, EqualTo<Name>{}, mapArena);
        values.reserve(4u);
        const u64 mapBaseline = mapArena.memoryStats().usedBytes;
        fail = true;
        EXPECT_THROW({
            ParsedMetadataExtension pending = MakeParsedMetadataExtension<AlignedExtension>(ownerArena, ownerArena, state);
            EXPECT_TRUE(values.try_emplace(Name(NWB::Tests::AssetsGraphicsFixture::s_AssetFixtureNameTestsMetadataExtensionInsertionUnwind), Move(pending)).second);
        }, InsertionFailure
        );
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
    AssetArena arena{ Name(s_RegistryLifetimeMetadataName) };
    Alloc::ScratchArena scratchArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionRegistryLifetimeScratch)};
    usize observedBucketCount = 0u;
    usize expectedBucketCount = 0u;
    {
        ParsedAssetMetadata metadata(arena);
        ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry, scratchArena));
        expectedBucketCount = metadata.entryRegistry.bucketCount();
        ASSERT_GE(expectedBucketCount, 7u);
        const RegistryBorrowingExtension& created = RequireParsedMetadataExtension<RegistryBorrowingExtension>(Name(s_RegistryLifetimeMetadataName), metadata, metadata.entryRegistry, observedBucketCount);
        EXPECT_EQ(&created.registry, &metadata.entryRegistry);
    }
    EXPECT_EQ(observedBucketCount, expectedBucketCount);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, GraphicsParserFailureReleasesItsActualExtensionPayload){
    AssetArena fixtureArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionGraphicsFixture)};
    AssetArena ownerArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionGraphicsOwner)};
    Alloc::ScratchArena scratchArena{Name(NWB::Tests::AssetsGraphicsFixture::s_AssetCaseTestsMetadataExtensionGraphicsScratch)};
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    Metascript::Document document(fixtureArena);
    ASSERT_TRUE(document.parse("compute_shader asset;"));
    const NWB::Path root(fixtureArena, "tests/metadata_extension/assets");
    const NWB::Path path = root / "invalid_shader.nwb";
    const DiscoveredNwbFile file(fixtureArena, root, path, "tests/metadata_extension/assets/invalid_shader.nwb", s_ProjectVirtualRoot);
    for(usize iteration = 0u; iteration < 4u; ++iteration){
        {
            ParsedAssetMetadata metadata(ownerArena);
            AssetDocumentMetadataParseContext context{ ownerArena, file, Impl::ComputeShader::AssetTypeName(), document, metadata, scratchArena };
            EXPECT_EQ(TryAutoCollectedDocumentMetadataParsers(context), AssetMetadataParseResult::Error);
            EXPECT_EQ(metadata.extensions.size(), 1u);
            EXPECT_GT(ownerArena.memoryStats().usedBytes, 0u);
        }
        EXPECT_EQ(ownerArena.memoryStats().usedBytes, 0u);
    }
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("asset is not a map")));
    const Metascript::Value* const asset = document.findVariable("asset");
    ASSERT_NE(asset, nullptr);
    EXPECT_TRUE(asset->isNull());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

