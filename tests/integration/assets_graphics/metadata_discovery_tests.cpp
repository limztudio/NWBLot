// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/assets/asset_file_discovery.h>
#include <core/assets/cook_metadata.h>
#include <core/assets/cook_metadata_internal.h>
#include <core/assets/paths.h>
#include <core/common/log.h>

#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <global/filesystem.h>
#include <global/text_utils.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_metadata_discovery_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ProjectVirtualRoot = "project";
#if !defined(NWB_PLATFORM_WINDOWS)
constexpr AStringView s_BuildDirectory = "__build_obj";
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


TEST(MetadataDiscoveryAdmission, DuplicateRootsKeepFirstOwnerAndPaths){
    AssetArena arena(Name("tests/metadata_discovery/duplicate_roots"));
    ScratchArena scratchArena(Name("tests/metadata_discovery/duplicate_roots_scratch"));
    const NWB::Path root = Tests::RepoRootOf(arena, __FILE__) / "tests" / "smoke" / "assets" / "textures";
    CookVector<ResolvedAssetRoot> roots(arena);
    roots.emplace_back(NWB::Path(arena, root), s_ProjectVirtualRoot);
    const auto single = AssetFileDiscovery::discover(roots, s_NwbExtension, arena, scratchArena);
    ASSERT_TRUE(single);
    ASSERT_FALSE(single->empty());
    roots.emplace_back(NWB::Path(arena, root), s_EngineVirtualRoot);
    const auto duplicate = AssetFileDiscovery::discover(roots, s_NwbExtension, arena, scratchArena);
    ASSERT_TRUE(duplicate);
    ASSERT_EQ(duplicate->size(), single->size());
    for(usize i = 0u; i < single->size(); ++i){
        EXPECT_EQ((*duplicate)[i].filePath, (*single)[i].filePath);
        EXPECT_EQ((*duplicate)[i].normalizedPathText, (*single)[i].normalizedPathText);
        EXPECT_EQ((*duplicate)[i].virtualRoot.view(), s_ProjectVirtualRoot);
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
    roots.emplace_back(NWB::Path(arena, root), s_ProjectVirtualRoot);
    roots.emplace_back(NWB::Path(arena, root), s_ProjectVirtualRoot);
    const auto files = AssetFileDiscovery::discover(roots, s_NwbExtension, arena, scratchArena);
    ASSERT_TRUE(files);
    ASSERT_EQ(files->size(), 2u);
    EXPECT_NE((*files)[0].filePath, (*files)[1].filePath);
    EXPECT_EQ((*files)[0].normalizedPathText, (*files)[1].normalizedPathText);
#endif
}


TEST(MetadataDiscoveryAdmission, VirtualPathDoesNotSkipAnUninspectableEarlierRoot){
    AssetArena arena(Name("tests/metadata_discovery/root_inspection"));
    Alloc::ScratchArena scratchArena(Name("tests/metadata_discovery/root_inspection_scratch"));
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger, Common::LoggerBreakPolicy::ReportOnly);
    const NWB::Path repo = Tests::RepoRootOf(arena, __FILE__);
#if defined(NWB_PLATFORM_WINDOWS)
    const NWB::Path invalidRoot = repo / "invalid|root";
#else
    TString<AssetArena> longName(arena);
    longName.resize(40000u, static_cast<tchar>('a'));
    const NWB::Path invalidRoot = repo / longName;
#endif
    ASSERT_FALSE(FileExists(invalidRoot / "CMakeLists.txt"));
    const auto validFile = FileExists(repo / "CMakeLists.txt");
    ASSERT_TRUE(validFile);
    ASSERT_TRUE(*validFile);
    CookVector<ResolvedAssetRoot> roots(arena);
    roots.emplace_back(NWB::Path(arena, invalidRoot), s_ProjectVirtualRoot);
    roots.emplace_back(NWB::Path(arena, repo), s_ProjectVirtualRoot);
    EXPECT_FALSE(ResolveVirtualAssetPath(roots, "project/CMakeLists.txt", arena, scratchArena));
    roots[0u].path = repo / "__nwb_missing_virtual_path_root_31c68263";
    const auto missing = FileExists(roots[0u].path);
    ASSERT_TRUE(missing);
    ASSERT_FALSE(*missing);
    EXPECT_TRUE(ResolveVirtualAssetPath(roots, "project/CMakeLists.txt", arena, scratchArena));
}


TEST(MetadataDiscoveryAdmission, PathsInSameHashBucketKeepDistinctOwnedPaths){
    ScratchArena scratchArena(Name("tests/metadata_discovery/colliding_paths"));
    CookMetadataDetail::DiscoveredPathSet seen(0, Hasher<AStringView>(), EqualTo<>(), scratchArena);
    seen.reserve(2u);
    ScratchString first("a/first.asset.nwb", scratchArena);
    ScratchString second(scratchArena);
    const usize bucket = Hasher<AStringView>{}(AStringView(first)) % seen.bucket_count();
    for(u32 i = 0u; i < 1024u; ++i){
        ScratchString candidate = StringFormat(scratchArena, "b/second_{}.asset.nwb", i);
        if(Hasher<AStringView>{}(AStringView(candidate)) % seen.bucket_count() == bucket){
            second = Move(candidate);
            break;
        }
    }
    ASSERT_FALSE(second.empty());
    EXPECT_TRUE(seen.insert(first).second);
    EXPECT_TRUE(seen.insert(second).second);
    first.assign("overwritten");
    ScratchString original("a/first.asset.nwb", scratchArena);
    EXPECT_FALSE(seen.insert(original).second);
    EXPECT_FALSE(seen.insert(second).second);
    EXPECT_EQ(seen.size(), 2u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

