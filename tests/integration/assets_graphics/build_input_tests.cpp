// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <pipeline/asset_builder/build.h>
#include <pipeline/asset_builder/build_inputs.h>
#include <core/task/cpu/scheduler.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_build_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_A_NWB = "a.nwb";
static constexpr AStringView s_ASSETS = "assets";
static constexpr AStringView s_PROJECT = "project";
static constexpr AStringView s_ASSETS_A_NWB = "assets/a.nwb";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr u32 s_ExpectedDualCount = 2u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = NWB::Core::Assets;
namespace Builder = NWB::Pipeline::AssetBuilder;

class BuildInputSelection : public testing::Test{
public:
    virtual ~BuildInputSelection()noexcept override{
        m_cpuScheduler.drain();
    }


protected:
    virtual void SetUp()override{
        m_root = NWB::Tests::RepoRootOf(m_testArena.arena, __FILE__)
            / "__build_obj" / "build_input_tests";
        ASSERT_TRUE(EnsureDirectories(m_root / s_ASSETS / "empty"));
        m_paths.repoRoot = m_root;
        m_paths.assetRoots.emplace_back(m_root / s_ASSETS, ACompactString(s_PROJECT));
    }

    virtual void TearDown()override{
        m_cpuScheduler.wait();
    }

    void addFile(AStringView relativePath){
        const NWB::Path path = m_root / relativePath;
        ASSERT_TRUE(EnsureDirectories(path.parentPath()));
        GlobalFilesystemDetail::OutputFileStream stream(path, GlobalFilesystemDetail::OutputFileStream::binary);
        ASSERT_TRUE(stream);
        Assets::AssetString normalized = PathToString(m_testArena.arena, path.lexicallyNormal());
        CanonicalizeTextInPlace(normalized);
        m_files.emplace_back(m_testArena.arena, m_root / s_ASSETS, path, normalized, ACompactString(s_PROJECT));
    }

    void addInput(AStringView relativePath){
        m_options.inputs.emplace_back(relativePath, m_testArena.arena);
    }

    bool select(){
        NWB::Core::Alloc::ScratchArena scratch(Name("tests/assets/build_inputs"));
        return Builder::SelectBuildInputs(m_options, m_paths, m_files, scratch);
    }


protected:
    NWB::Tests::TestArena<> m_testArena;
    NWB::Core::CpuTaskScheduler m_cpuScheduler{ 1u };
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerGuard{ m_logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal };
    NWB::Path m_root{ m_testArena.arena };
    Assets::ResolvedCookPaths m_paths{ m_testArena.arena };
    Assets::DiscoveredNwbFileVector m_files{ m_testArena.arena };
    Builder::AssetBuildOptions m_options{ m_testArena.arena, m_cpuScheduler };
};

TEST_F(BuildInputSelection, EmptyBuildDomainIsRejectedBeforeInputDiscovery){
    m_options.assetType.clear();
    EXPECT_FALSE(Builder::BuildAssets(m_options));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("unsupported --asset-type ''")));
    EXPECT_EQ(m_logger.errorCount(), 1u);
    EXPECT_TRUE(m_options.assetRoots.empty());
    EXPECT_TRUE(m_options.outputDirectory.empty());

    ASSERT_TRUE(m_options.assetType.assign(Builder::s_GraphicsAssetBuildType));
    EXPECT_FALSE(Builder::BuildAssets(m_options));
    EXPECT_TRUE(m_logger.sawErrorContaining(NWB_TEXT("no asset roots specified")));
    EXPECT_EQ(m_logger.errorCount(), 2u);
}

TEST_F(BuildInputSelection, ExactInputsPreserveDiscoveredOrderAndDuplicateRecords){
    addFile("assets/z.nwb");
    addFile(s_ASSETS_A_NWB);
    addFile("assets/excluded.nwb");
    addFile(s_ASSETS_A_NWB);
    addInput("assets/./a.nwb");
    addInput("assets/z.nwb");
    addInput(s_ASSETS_A_NWB);
    ASSERT_TRUE(select());
    ASSERT_EQ(m_files.size(), 3u);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), "z.nwb");
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[1].filePath.filename()), s_A_NWB);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[2].filePath.filename()), s_A_NWB);
}

TEST_F(BuildInputSelection, DirectoryInputsRespectBoundariesAndAllowEmptyRoots){
    addFile("assets/part/a.nwb");
    addFile("assets/partial/b.nwb");
    addFile("assets/part/nested/c.nwb");
    addInput("assets/part/../part");
    addInput("assets/part/nested");
    addInput("assets/empty");
    ASSERT_TRUE(select());
    ASSERT_EQ(m_files.size(), s_ExpectedDualCount);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), s_A_NWB);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[1].filePath.filename()), "c.nwb");
}

TEST_F(BuildInputSelection, RejectedInputsLeaveDiscoveredFilesIntact){
    addFile(s_ASSETS_A_NWB);
    addFile("outside/b.nwb");
    addInput(s_ASSETS_A_NWB);
    addInput("outside");
    EXPECT_FALSE(select());
    EXPECT_EQ(m_files.size(), s_ExpectedDualCount);
    m_options.inputs.clear();
    addInput("assets/missing.nwb");
    EXPECT_FALSE(select());
    EXPECT_EQ(m_files.size(), s_ExpectedDualCount);
    m_options.inputs.clear();
    addInput("assets/empty");
    ASSERT_TRUE(select());
    EXPECT_TRUE(m_files.empty());
}

TEST_F(BuildInputSelection, DirectoryCaseFollowsHostFilesystemContract){
    addFile("assets/Case/a.nwb");
    addFile("assets/case/b.nwb");
    addInput("assets/Case");
    ASSERT_TRUE(select());
#if defined(NWB_PLATFORM_WINDOWS)
    EXPECT_EQ(m_files.size(), s_ExpectedDualCount);
#else
    ASSERT_EQ(m_files.size(), 1u);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), s_A_NWB);
#endif
}

TEST_F(BuildInputSelection, InferredRootsDeduplicateAssetsAncestorsWithoutDroppingExternalDirectories){
    addFile("assets/part/a.nwb");
    addFile("impl/assets/b.nwb");
    addFile("loose/c.nwb");
    Assets::CookVector<Assets::CookString> sources(m_testArena.arena);
    for(const AStringView source : { "assets/part/../part/a.nwb", "impl/assets/b.nwb", "assets/empty", "loose/c.nwb" })
        sources.emplace_back(source, m_testArena.arena);
    Assets::ScratchArena scratchArena(Name("tests/assets/inferred_roots"));
    const auto roots = Assets::ResolveAssetRoots(
        m_root, sources, true, Assets::AssetRootDuplicatePolicy::ExactText, scratchArena
    );
    ASSERT_TRUE(roots);
    ASSERT_EQ(roots->size(), 3u);
    EXPECT_EQ((*roots)[0].path, (m_root / s_ASSETS).lexicallyNormal());
    EXPECT_EQ((*roots)[0].virtualRoot.view(), "project");
    EXPECT_EQ((*roots)[1].path, (m_root / "impl" / s_ASSETS).lexicallyNormal());
    EXPECT_EQ((*roots)[1].virtualRoot.view(), "engine");
    EXPECT_EQ((*roots)[2].path, (m_root / "loose").lexicallyNormal());
    EXPECT_EQ((*roots)[2].virtualRoot.view(), "project");
}

TEST_F(BuildInputSelection, ExplicitRootsPreserveFirstSpellingAndCallerDuplicatePolicy){
    Assets::CookVector<Assets::CookString> sources(m_testArena.arena);
    for(const AStringView source : { "assets/./Case", "assets/Case", "assets/case", "impl/assets/graphics" })
        sources.emplace_back(source, m_testArena.arena);
    sources.push_back(PathToString(m_testArena.arena, m_root / "assets" / "Case"));
    Assets::ScratchArena scratchArena(Name("tests/assets/explicit_roots"));
    const auto exact = Assets::ResolveAssetRoots(
        m_root, sources, false, Assets::AssetRootDuplicatePolicy::ExactText, scratchArena
    );
    ASSERT_TRUE(exact);
    ASSERT_EQ(exact->size(), 3u);
    EXPECT_EQ((*exact)[0].path, (m_root / "assets" / "Case").lexicallyNormal());
    EXPECT_EQ((*exact)[1].path, (m_root / "assets" / "case").lexicallyNormal());
    // Explicit roots remain exact directories rather than climbing to their assets ancestor.
    EXPECT_EQ((*exact)[2].path, (m_root / "impl" / "assets" / "graphics").lexicallyNormal());
    EXPECT_EQ((*exact)[2].virtualRoot.view(), "project");
    const auto host = Assets::ResolveAssetRoots(
        m_root, sources, false, Assets::AssetRootDuplicatePolicy::HostFilesystem, scratchArena
    );
    ASSERT_TRUE(host);
#if defined(NWB_PLATFORM_WINDOWS)
    ASSERT_EQ(host->size(), 2u);
    EXPECT_EQ((*host)[1].path, (*exact)[2].path);
#else
    ASSERT_EQ(host->size(), exact->size());
    EXPECT_EQ((*host)[1].path, (*exact)[1].path);
#endif
    EXPECT_EQ((*host)[0].path, (*exact)[0].path);
}

TEST_F(BuildInputSelection, RootResolutionFailureRetainsSourceIndexWithoutPublishingPartialRoots){
    Assets::CookVector<Assets::CookString> sources(m_testArena.arena);
    sources.emplace_back("assets", m_testArena.arena);
    sources.emplace_back("", m_testArena.arena);
    Assets::ScratchArena scratchArena(Name("tests/assets/rejected_roots"));
    const auto rejected = Assets::ResolveAssetRoots(
        m_root, sources, false, Assets::AssetRootDuplicatePolicy::ExactText, scratchArena
    );
    ASSERT_FALSE(rejected);
    EXPECT_EQ(rejected.error().sourceIndex, 1u);
    EXPECT_EQ(rejected.error().reason, Assets::AssetRootResolutionFailure::ResolvePath);
    EXPECT_TRUE(rejected.error().inputPath.empty());
    EXPECT_TRUE(static_cast<bool>(rejected.error().error));
    sources[1] = "assets/empty";
    const auto recovered = Assets::ResolveAssetRoots(
        m_root, sources, true, Assets::AssetRootDuplicatePolicy::ExactText, scratchArena
    );
    ASSERT_TRUE(recovered);
    ASSERT_EQ(recovered->size(), 1u);
    EXPECT_EQ((*recovered)[0].path, (m_root / s_ASSETS).lexicallyNormal());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

