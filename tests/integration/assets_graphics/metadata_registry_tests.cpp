// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/assets_model/cook.h>
#include <impl/assets_sampler/cook.h>

#include <core/assets/cook_metadata.h>
#include <core/assets/cook_dependency_registry.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/paths.h>
#include <core/common/name_symbols.h>
#include <core/task/cpu/scheduler.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>
#include <global/text_utils.h>
#include <global/thread.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metadata_registry_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ProjectVirtualRoot = "project";
constexpr AStringView s_BuildDirectory = "__build_obj";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


class NullAllocatedBucket final : public ICookEntryBucket{
public:
    static void* operator new(usize)noexcept{ return nullptr; }

    static void operator delete(void* pointer)noexcept{ ::operator delete(pointer); }


public:
    explicit NullAllocatedBucket(CookArena&)noexcept{}


public:
    [[nodiscard]] virtual usize size()const noexcept override{ return 0u; }

    virtual b8 parseDocument(const NWB::Path&, AStringView, const NWB::Path&, const Metascript::Document&, CookEntryParseContext&)noexcept override{ return false; }

    virtual b8 parseValue(Name, const NWB::Path&, const Metascript::Value&, CookEntryParseContext&)noexcept override{ return false; }

    virtual b8 appendCachedEntry(const CookEntrySourceLocation&, const Name&, Span<const u8>, CookEntryParseContext&)noexcept override{ return false; }

    virtual b8 writeCookedAssets(CookEntryWriteContext&)noexcept override{ return false; }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        files.emplace_back(fixtureArena, root, path, normalized, s_ProjectVirtualRoot);
    }
    for(const b8 rejectDuplicate : { false, true }){
        if(rejectDuplicate)
            files.push_back(files.front());
        {
            Alloc::ScratchArena scratchArena(Name("tests/metadata_registry/scratch"));
            ParsedAssetMetadata metadata(parseArena);
            ASSERT_TRUE(RegisterAutoCollectedCookEntryTypes(metadata.entryRegistry, scratchArena));
            auto& samplers = metadata.entryRegistry.entries<Impl::SamplerCookEntry>(Impl::Sampler::AssetTypeName());
            auto& models = metadata.entryRegistry.entries<Impl::ModelCookEntry>(Impl::Model::AssetTypeName());
            EXPECT_EQ(ParseAssetMetadata(files, root, NWB::Path(fixtureArena), parseArena, cpuScheduler, scratchArena, metadata), !rejectDuplicate);
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
                EXPECT_EQ(&samplers.get_allocator().arena(), &parseArena);
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


TEST(MetadataRegistryPublication, DefaultDependencyEntryOwnsItsPathText){
    AssetArena arena(Name("tests/metadata_registry/dependency_path"));
    CookDependencyRegistry registry(arena);
    ScratchArena scratchArena(Name("tests/metadata_registry/dependency_path_scratch"));
    ScratchString text("project/example", scratchArena);
    ASSERT_TRUE(registry.registerDefaultEntryAsset(Name("project/example"), Name("example"), AStringView(text)));
    text.assign("project/changed");
    EXPECT_EQ(registry.defaultEntryAsset().virtualPathText, "project/example");
    const AStringView stored = registry.defaultEntryAsset().virtualPathText;
    ASSERT_TRUE(registry.registerDefaultEntryAsset(Name("project/example"), Name("example"), stored));
    EXPECT_EQ(registry.defaultEntryAsset().virtualPathText, "project/example");
    CookDependencyRegistry moved(Move(registry));
    EXPECT_EQ(moved.defaultEntryAsset().virtualPathText, "project/example");
    EXPECT_FALSE(registry.defaultEntryAsset().virtualPath);
}


TEST(MetadataRegistryPublication, MetadataRegistrationKeepsCallerTextAlive){
    ScratchArena scratchArena(Name("tests/metadata_registry/owned_tokens"));
    ScratchString token("nwb_owned_token_71a7", scratchArena);
    ScratchString suffix("_nwb_owned_suffix_71a7", scratchArena);
    ScratchString extension(".nwb_owned_sidecar_71a7", scratchArena);
    RegisterNonStandaloneAssetType(Name("tests/owned_type_71a7"), AStringView(token));
    RegisterNonStandaloneFirstTokenSuffix(AStringView(suffix));
    RegisterAssetPairedSidecarRule(AssetPairedSidecarRule{ AStringView(extension) });
    token.assign("overwritten");
    suffix.assign("overwritten");
    extension.assign("overwritten");

    EXPECT_TRUE(IsLikelyNonStandaloneFirstToken("nwb_owned_token_71a7"));
    EXPECT_TRUE(IsLikelyNonStandaloneFirstToken("value_nwb_owned_suffix_71a7"));
    Vector<AssetPairedSidecarRule, ScratchArena> rules(scratchArena);
    CookMetadataDetail::CopyPairedSidecarRules(rules);
    b8 found = false;
    for(const AssetPairedSidecarRule& rule : rules)
        found = found || rule.sidecarExtension == ".nwb_owned_sidecar_71a7";
    EXPECT_TRUE(found);
#if defined(NWB_DEBUG)
    Name fallback("nwb_owned_fallback_71a7");
    RegisterNonStandaloneAssetType(fallback);
    fallback = Name("nwb_replaced_fallback_71a7");
    EXPECT_TRUE(IsLikelyNonStandaloneFirstToken("nwb_owned_fallback_71a7"));
#else
    const auto previousRecorder = NameDetail::SymbolRecorderState();
    const auto previousResolver = NameDetail::SymbolResolverState();
    ScopeExit restore([&]()noexcept{
        SetNameSymbolRecordCallback(previousRecorder.callback, previousRecorder.userData);
        SetNameSymbolResolveCallback(previousResolver.callback, previousResolver.userData);
    });
    Common::NameSymbols::InstallRuntimeRegistry();
    const Name fallback{ AStringView("nwb_owned_fallback_71a7") };
    RegisterNonStandaloneAssetType(fallback);
    const Name replacement{ AStringView("nwb_replaced_fallback_71a7") };
    for(usize i = 0u; i < NameDetail::s_SymbolTextBufferCount; ++i)
        EXPECT_EQ(replacement.resolvedText(), AStringView("nwb_replaced_fallback_71a7"));
    EXPECT_TRUE(IsLikelyNonStandaloneFirstToken("nwb_owned_fallback_71a7"));
#endif
}


TEST(MetadataRegistryPublication, ConcurrentMetadataRegistrationsKeepDistinctTokens){
    constexpr Array<AStringView, 4u> tokens = {
        "nwb_owned_thread_a_71a7", "nwb_owned_thread_b_71a7", "nwb_owned_thread_c_71a7", "nwb_owned_thread_d_71a7"
    };
    Array<Thread, tokens.size()> threads;
    for(usize i = 0u; i < tokens.size(); ++i){
        threads[i] = Thread([&, i](){
            ScratchArena scratchArena(Name("tests/metadata_registry/concurrent_tokens"));
            ScratchString text(tokens[i], scratchArena);
            RegisterNonStandaloneAssetType(s_NameNone, AStringView(text.data(), text.size()));
            text.assign("overwritten");
        });
    }
    for(Thread& thread : threads)
        thread.join();
    for(const AStringView token : tokens)
        EXPECT_TRUE(IsLikelyNonStandaloneFirstToken(token));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

