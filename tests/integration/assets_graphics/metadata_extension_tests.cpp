// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_model/cook.h>
#include <impl/assets_sampler/cook.h>
#include <impl/assets_shader/asset.h>

#include <core/assets/cook_metadata.h>
#include <core/task/cpu/scheduler.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>
#include <global/text_utils.h>

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
constexpr AStringView s_BuildDirectory = "__build_obj";

constexpr u32 s_ExpectedDualCount = 2u;


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

class NullAllocatedBucket final : public ICookEntryBucket{
public:
    static void* operator new(usize)noexcept{ return nullptr; }

    static void operator delete(void* pointer)noexcept{ ::operator delete(pointer); }


public:
    explicit NullAllocatedBucket(CookArena&)noexcept{}


public:
    [[nodiscard]] virtual usize size()const noexcept override{ return 0u; }

    virtual bool parseDocument(const NWB::Path&, AStringView, const NWB::Path&, const Metascript::Document&, CookEntryParseContext&)noexcept override{ return false; }

    virtual bool parseValue(Name, const NWB::Path&, const Metascript::Value&, CookEntryParseContext&)noexcept override{ return false; }

    virtual bool writeCookedAssets(CookEntryWriteContext&)noexcept override{ return false; }
};

class NullAllocatedAsset final : public TypedAsset<NullAllocatedAsset>{
public:
    inline static u32 s_ConstructorCalls = 0u;
    inline static bool s_LoadCalled = false;
    [[nodiscard]] static const Name& AssetTypeName()noexcept{
        static constexpr Name s_Type("tests/metadata_registry/null_allocation");
        return s_Type;
    }

    static void* operator new(usize)noexcept{ return nullptr; }

    static void operator delete(void* pointer)noexcept{ ::operator delete(pointer); }


public:
    NullAllocatedAsset(AssetArena&, const Name& virtualPath)noexcept
        : TypedAsset(virtualPath)
    {
        ++s_ConstructorCalls;
    }


public:
    bool loadBinary(const AssetBytes&)noexcept{
        s_LoadCalled = true;
        return true;
    }
};

class NullAllocatedCodec final : public AssetCodec<NullAllocatedAsset>{
public:
    NullAllocatedCodec()noexcept = default;


public:
    virtual bool serialize(const IAsset&, AssetBytes&)const noexcept override{ return false; }
};

struct ThrowingImplicitAssetName{
    explicit operator Name() & noexcept{ return s_NameNone; }

    operator const Name&()const&{ throw 197u; }
};

class ImplicitNameAsset final : public TypedAsset<ImplicitNameAsset>{
public:
    [[nodiscard]] static ThrowingImplicitAssetName& AssetTypeName()noexcept{
        static ThrowingImplicitAssetName s_Value;
        return s_Value;
    }


public:
    ImplicitNameAsset() = default;
    explicit ImplicitNameAsset(const Name& virtualPath)
        : TypedAsset(virtualPath)
    {}
    ImplicitNameAsset(AssetArena&, const Name& virtualPath)
        : TypedAsset(virtualPath)
    {}


public:
    bool loadBinary(const AssetBytes&)noexcept{ return true; }
};

class ImplicitNameCodec final : public AssetCodec<ImplicitNameAsset>{
public:
    ImplicitNameCodec() = default;


public:
    virtual bool serialize(const IAsset&, AssetBytes&)const noexcept override{ return false; }
};

class ImplicitNameShader final : public Impl::TypedShader<ImplicitNameShader>{
public:
    static constexpr Core::ShaderType::Enum s_Stage = Core::ShaderType::ComputeStage;
    [[nodiscard]] static ThrowingImplicitAssetName& AssetTypeName()noexcept{
        static ThrowingImplicitAssetName s_Value;
        return s_Value;
    }


public:
    using Impl::TypedShader<ImplicitNameShader>::TypedShader;
};


struct ThrowingComparisonAnswer{
    explicit operator bool()const{ throw 199u; }
};

struct AssetComparisonTag{};

[[nodiscard]] inline ThrowingComparisonAnswer operator!=(const Name&, const AssetComparisonTag&)noexcept{ return {}; }

class ContextualComparisonAsset final : public IAsset{
public:
    [[nodiscard]] static AssetComparisonTag AssetTypeName()noexcept{ return {}; }


public:
    ContextualComparisonAsset()noexcept
        : IAsset(Name("tests/asset_constructor/contextual_comparison"))
    {}
};


static_assert(!IsConstructible_V<ParsedMetadataExtension, const ParsedMetadataExtension&>);
static_assert(IsNothrowMoveConstructible_V<ParsedMetadataExtension>);
static_assert(IsNothrowDestructible_V<ParsedMetadataExtension>);


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
        const ParsedAssetMetadata& borrowed = metadata;
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(borrowed, name), &first);
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(metadata, Name("tests/metadata_extension/absent")), nullptr);
        const Name textName("tests/metadata_extension/text");
        const TextExtension& text = RequireParsedMetadataExtension<TextExtension>(metadata, textName, arena, textState);
        EXPECT_EQ(FindParsedMetadataExtension<TextExtension>(metadata, textName), &text);
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
        const Name firstName(s_FirstMetadataName);
        AlignedExtension* const first = &RequireParsedMetadataExtension<AlignedExtension>(metadata, firstName, arena, state);
        for(usize index = 0u; index < 80u; ++index){
            const AssetString nameText = StringFormat(arena, "tests/metadata_extension/growth_{}", index);
            const auto nameResult = ToName(nameText);
            ASSERT_TRUE(nameResult);
            ASSERT_TRUE(*nameResult);
            const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(metadata, **nameResult, arena, state);
            EXPECT_EQ(created.identity, index + s_ExpectedDualCount);
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
    const Name firstName(s_FirstMetadataName);
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
        EXPECT_EQ(static_cast<TextExtension*>(secondMap.at(firstName).get())->strings.size(), s_ExpectedDualCount);
    }
    EXPECT_EQ(firstState.destructions, 1u);
    EXPECT_EQ(secondState.destructions, s_ExpectedDualCount);
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
        AlignedExtension* const first = &RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state);
        ParsedMetadataExtension detached = Move(metadata.extensions.at(name));
        EXPECT_EQ(detached.get(), first);
        EXPECT_EQ(FindParsedMetadataExtension<AlignedExtension>(metadata, name), nullptr);
        AlignedExtension& replacement = RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state);
        EXPECT_NE(&replacement, first);
        EXPECT_EQ(replacement.identity, s_ExpectedDualCount);
        EXPECT_EQ(metadata.extensions.size(), 1u);
        detached.reset();
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, s_ExpectedDualCount);
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
            EXPECT_EQ(RequireParsedMetadataExtension<AlignedExtension>(metadata, name, arena, state, true).identity, 1u);
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
        const AlignedExtension& created = RequireParsedMetadataExtension<AlignedExtension>(metadata, Name(s_CallerUnwindMetadataName), arena, state);
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
        ReentrantExtension& result = RequireParsedMetadataExtension<ReentrantExtension>(metadata, name, metadata, name, state, true);
        EXPECT_EQ(result.identity, s_ExpectedDualCount);
        EXPECT_EQ(result.nested, nullptr);
        EXPECT_EQ(&result, FindParsedMetadataExtension<ReentrantExtension>(metadata, name));
        EXPECT_EQ(metadata.extensions.size(), 1u);
        EXPECT_EQ(state.constructions, s_ExpectedDualCount);
        EXPECT_EQ(state.destructions, 1u);
    }
    EXPECT_EQ(state.destructions, s_ExpectedDualCount);
    EXPECT_EQ(arena.memoryStats().usedBytes, 0u);
}

TEST(MetadataExtensionOwnership, InsertionUnwindDestroysThePendingErasedOwner){
    AssetArena mapArena(Name("tests/metadata_extension/insertion_map"));
    AssetArena ownerArena(Name("tests/metadata_extension/insertion_owner"));
    ExtensionState state;
    bool fail = false;
    {
        HashMap<Name, ParsedMetadataExtension, AssetArena, ThrowingExtensionHasher, EqualTo<Name>> values(0u, ThrowingExtensionHasher{ MakeNotNull(&fail) }, EqualTo<Name>{}, mapArena);
        values.reserve(4u);
        const u64 mapBaseline = mapArena.memoryStats().usedBytes;
        fail = true;
        EXPECT_THROW({
            ParsedMetadataExtension pending = MakeParsedMetadataExtension<AlignedExtension>(ownerArena, ownerArena, state);
            EXPECT_TRUE(values.try_emplace(Name("tests/metadata_extension/insertion_unwind"), Move(pending)).second);
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
    Alloc::ScratchArena scratchArena(Name("tests/metadata_extension/registry_lifetime_scratch"));
    usize observedBucketCount = 0u;
    usize expectedBucketCount = 0u;
    {
        ParsedAssetMetadata metadata(arena);
        ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry, scratchArena));
        expectedBucketCount = metadata.entryRegistry.bucketCount();
        ASSERT_GE(expectedBucketCount, 7u);
        const RegistryBorrowingExtension& created = RequireParsedMetadataExtension<RegistryBorrowingExtension>(metadata, Name(s_RegistryLifetimeMetadataName), metadata.entryRegistry, observedBucketCount);
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
    ASSERT_TRUE(document.parse("compute_shader asset;"));
    const NWB::Path root(fixtureArena, "tests/metadata_extension/assets");
    const NWB::Path path = root / "invalid_shader.nwb";
    const DiscoveredNwbFile file(fixtureArena, root, path, "tests/metadata_extension/assets/invalid_shader.nwb", ACompactString(s_ProjectVirtualRoot));
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

TEST(MetadataRegistryStorage, TypedGrowthPreservesInputOrderAndDoesNotReserveUnusedBuckets){
    AssetArena fixtureArena(Name("tests/metadata_registry/fixture"));
    AssetArena parseArena(Name("tests/metadata_registry/output"));
    CpuTaskScheduler cpuScheduler(0u);
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    const NWB::Path root = Tests::RepoRootOf(fixtureArena, __FILE__) / s_BuildDirectory / "metadata_registry_storage";
    ASSERT_TRUE(EnsureDirectories(root));
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
        "asset.max_anisotropy = 1.0;\r\n";
    for(usize index = 0u; index < s_SamplerCount; ++index){
        const usize identity = (index * 37u) % s_SamplerCount;
        const AssetString filename = StringFormat(fixtureArena, "sampler_{:03}.nwb", identity);
        const NWB::Path path = root / filename;
        AssetString source(s_SamplerFields, fixtureArena);
        source += StringFormat(fixtureArena, "asset.mip_bias = {};\r\n", identity);
        ASSERT_TRUE(::WriteTextFile(path, source));
        const AssetString normalized = PathToString(fixtureArena, path.lexicallyNormal());
        files.emplace_back(fixtureArena, root, path, normalized, ACompactString(s_ProjectVirtualRoot));
    }
    for(const bool rejectDuplicate : { false, true }){
        if(rejectDuplicate)
            files.push_back(files.front());
        {
            Alloc::ScratchArena scratchArena(Name("tests/metadata_registry/scratch"));
            ParsedAssetMetadata metadata(parseArena);
            ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry, scratchArena));
            auto& samplers = metadata.entryRegistry.entries<Impl::SamplerCookEntry>(Impl::Sampler::AssetTypeName());
            auto& models = metadata.entryRegistry.entries<Impl::ModelCookEntry>(Impl::Model::AssetTypeName());
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
                const auto expectedPath = ToName(virtualPath);
                ASSERT_TRUE(expectedPath);
                ASSERT_TRUE(*expectedPath);
                EXPECT_EQ(samplers[index].virtualPath, **expectedPath);
                EXPECT_EQ(&samplers[index].arena, &parseArena);
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


TEST(MetadataDiscoveryAdmission, CaseDistinctNativeFilesBothReachMetadataAdmission){
#if defined(NWB_PLATFORM_WINDOWS)
    GTEST_SKIP() << "The current Windows host identity folds physical path case";
#else
    AssetArena arena(Name("tests/metadata_discovery/case_identity"));
    Alloc::ScratchArena scratchArena(Name("tests/metadata_discovery/case_identity_scratch"));
    const NWB::Path root = Tests::RepoRootOf(arena, __FILE__) / s_BuildDirectory / "metadata_discovery_case_identity";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    const NWB::Path upperPath = root / "Case.nwb";
    const NWB::Path lowerPath = root / "case.nwb";
    ASSERT_TRUE(::WriteTextFile(upperPath, "upper native input"));
    ASSERT_TRUE(::WriteTextFile(lowerPath, "lower native input"));
    ScratchString upperContents(scratchArena);
    ASSERT_TRUE(::ReadTextFile(upperPath, upperContents));
    if(upperContents != "upper native input")
        GTEST_SKIP() << "The test filesystem does not preserve distinct native path case";

    CookVector<ResolvedAssetRoot> roots(arena);
    roots.emplace_back(NWB::Path(arena, root), ACompactString(s_ProjectVirtualRoot));
    roots.emplace_back(NWB::Path(arena, root), ACompactString(s_ProjectVirtualRoot));
    const auto files = DiscoverFilesWithExtension(arena, roots, s_NwbExtension, scratchArena);
    ASSERT_TRUE(files);
    ASSERT_EQ(files->size(), 2u);
    EXPECT_NE((*files)[0].filePath, (*files)[1].filePath);
    EXPECT_EQ((*files)[0].normalizedPathText, (*files)[1].normalizedPathText);
#endif
}


TEST(MetadataRegistryPublication, NullClassAllocationDoesNotPublishBucket){
    AssetArena arena(Name("tests/metadata_registry/null_allocation"));
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    CookEntryRegistry registry(arena);
    const Name type("tests/metadata_registry/null_bucket");
    EXPECT_FALSE(registry.registerCustomType<NullAllocatedBucket>(type));
    EXPECT_EQ(registry.bucketCount(), 0u);
    EXPECT_EQ(registry.find(type), nullptr);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("bucket allocation returned null")));
}

TEST(AssetCodecAdmission, NullClassAllocationDoesNotCallAssetLoad){
    AssetArena arena(Name("tests/asset_codec/null_allocation"));
    NullAllocatedAsset::s_ConstructorCalls = 0u;
    NullAllocatedAsset::s_LoadCalled = false;
    NullAllocatedCodec codec;
    AssetBytes bytes(arena);
    const auto result = codec.deserialize(arena, Name("project/tests/null_asset"), bytes);
    EXPECT_FALSE(result);
    EXPECT_EQ(NullAllocatedAsset::s_ConstructorCalls, 0u);
    EXPECT_FALSE(NullAllocatedAsset::s_LoadCalled);
}

TEST(AssetConstructorContracts, ImplicitNameBindingCanThrowDespiteNothrowExplicitConversion){
    static_assert(noexcept(Name(ImplicitNameAsset::AssetTypeName())));
    static_assert(!noexcept(DeclVal<void(*)(const Name&)noexcept>()(ImplicitNameAsset::AssetTypeName())));
    static_assert(!IsNothrowConstructible_V<ImplicitNameAsset>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameCodec>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameShader, AssetArena&>);
    static_assert(!IsNothrowConstructible_V<ImplicitNameShader, AssetArena&, const Name&>);
    static_assert(!IsNothrowConstructible_V<Impl::ShaderAssetCodec<ImplicitNameShader>>);
    EXPECT_THROW(ImplicitNameAsset{}, u32);
    EXPECT_THROW(ImplicitNameAsset{Name("project/tests/name_conversion")}, u32);
    EXPECT_THROW(ImplicitNameCodec{}, u32);
    AssetArena arena(Name("tests/asset_constructor/implicit_name"));
    EXPECT_THROW(ImplicitNameShader{arena}, u32);
    EXPECT_THROW((ImplicitNameShader{arena, Name("project/tests/shader_name_conversion")}), u32);
    EXPECT_THROW((Impl::ShaderAssetCodec<ImplicitNameShader>{}), u32);
}


TEST(AssetConstructorContracts, CastIncludesTheCompleteContextualComparisonConversion){
    static_assert(noexcept(DeclVal<const IAsset&>().assetType() != ContextualComparisonAsset::AssetTypeName()));
    static_assert(!noexcept(CastAsset<ContextualComparisonAsset>(DeclVal<const IAsset*>())));
    ContextualComparisonAsset asset;
    EXPECT_THROW({ EXPECT_EQ(CastAsset<ContextualComparisonAsset>(&asset), &asset); }, u32);
    EXPECT_EQ(CastAsset<ContextualComparisonAsset>(nullptr), nullptr);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

