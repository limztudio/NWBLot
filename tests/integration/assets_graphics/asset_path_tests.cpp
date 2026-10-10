// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/assets/paths.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>

#include <tests/common/test_context.h>
#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_path_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_PROJECT = "project";
static constexpr AStringView s_STALE_OUTPUT = "stale/output";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Core;
using namespace NWB::Core::Assets;


struct PathFixture{
    AssetArena arena{ Name("tests/asset_path/inputs") };
    const NWB::Path assetRoot{ arena, "tests/asset_path/assets" };
    AString<AssetArena> relativeText{ arena };
    AString<AssetArena> expectedRelative{ arena };
    NWB::Path relativePath{ arena };
    NWB::Path sourcePath{ arena };
    AString<AssetArena> expectedDerived{ arena };
};


struct RelativeCase{
    AStringView input;
    AStringView expected;
    bool accepted;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void PrepareWorkload(
    PathFixture& fixture,
    const usize directoryCount,
    const AStringView directoryToken,
    const usize tokenRepeats
){
    const usize directoryBytes = directoryToken.size() * tokenRepeats;
    fixture.relativeText.reserve(directoryCount * (directoryBytes + 1u) + 32u);
    for(usize directory = 0u; directory < directoryCount; ++directory){
        for(usize token = 0u; token < tokenRepeats; ++token)
            fixture.relativeText.append(directoryToken.data(), directoryToken.size());
        fixture.relativeText += '/';
    }
    fixture.relativeText += "Model.LOD0.NWB";
    fixture.relativePath = NWB::Path(fixture.arena, AStringView(fixture.relativeText));
    fixture.sourcePath = fixture.assetRoot / fixture.relativePath;
    fixture.expectedRelative = fixture.relativeText;
    CanonicalizeTextInPlace(fixture.expectedRelative);
    fixture.expectedDerived.reserve(8u + fixture.expectedRelative.size());
    fixture.expectedDerived = "project/";
    fixture.expectedDerived.append(fixture.expectedRelative.data(), fixture.expectedRelative.size() - 4u);
}

[[nodiscard]] static bool BuildAndVerifyDerivedPath(const PathFixture& fixture, Alloc::ScratchArena& scratchArena){
    const auto output = BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView("project"), fixture.sourcePath);
    return output && AStringView(*output) == AStringView(fixture.expectedDerived);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetPaths, RelativeComponentsPreserveDotsAndCanonicalizeOnlyAsciiText){
    AssetArena inputArena(Name("tests/asset_path/relative_inputs"));
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/relative_scratch"));
    constexpr RelativeCase s_Cases[]{
        { "Models//./Hero.LOD0.NWB///", "models/hero.lod0.nwb", true },
        { "./.Hidden/../Ignored", ".hidden", false },
        { "Dir.Name/.../File.", "dir.name/.../file.", true },
        { "", "", false },
        { ".", "", false },
        { "././", "", false },
        { "../After", "", false },
        { "Valid/Part/../After", "valid/part", false },
        { "Valid/./../After", "valid", false },
        { "/Rooted/After", "", false },
    };
    AString<Alloc::ScratchArena> output(scratchArena);
    for(const RelativeCase& testCase : s_Cases){
        SCOPED_TRACE(testCase.input);
        output = s_STALE_OUTPUT;
        const NWB::Path path(inputArena, testCase.input);
        EXPECT_EQ(AssetPathsDetail::BuildRelativeAssetPathText(path, output), testCase.accepted);
        EXPECT_EQ(AStringView(output), testCase.expected);
    }
}

TEST(AssetPaths, RelativeBackslashesFollowHostComponentRulesBeforeCanonicalization){
    AssetArena inputArena(Name("tests/asset_path/separator_inputs"));
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/separator_scratch"));
    AString<Alloc::ScratchArena> output(scratchArena);
    const NWB::Path path(inputArena, "Prefix/Upper\\Name//./File");
#if defined(NWB_PLATFORM_WINDOWS)
    ASSERT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(path, output));
    EXPECT_EQ(AStringView(output), "prefix/upper/name/file");
    const NWB::Path driveRoot(inputArena, "C:\\Root\\File");
    EXPECT_FALSE(AssetPathsDetail::BuildRelativeAssetPathText(driveRoot, output));
    EXPECT_TRUE(output.empty());
    const NWB::Path driveOnly(inputArena, "C:");
    EXPECT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(driveOnly, output));
    EXPECT_EQ(AStringView(output), "c:");
#else
    EXPECT_FALSE(AssetPathsDetail::BuildRelativeAssetPathText(path, output));
    EXPECT_EQ(AStringView(output), "prefix");
#endif
}


TEST(AssetPaths, DerivedPathRemovesOnlyFinalExtensionAndPreservesVirtualRootText){
    PathFixture fixture;
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/derived_scratch"));
    const NWB::Path source = fixture.assetRoot / "Models//./Hero.LOD0.NWB";
    const auto output = BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView("MiXeD/Root"), source);
    ASSERT_TRUE(output);
    EXPECT_EQ(AStringView(*output), "MiXeD/Root/models/hero.lod0");
    const NWB::Path hiddenSource = fixture.assetRoot / "Folder.Name/.Hidden";
    const auto hiddenOutput = BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView(s_PROJECT), hiddenSource);
    ASSERT_TRUE(hiddenOutput);
    EXPECT_EQ(AStringView(*hiddenOutput), "project/folder.name/.hidden");
    const NWB::Path extensionlessSource = fixture.assetRoot / "Folder/NoExtension";
    const auto extensionlessOutput = BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView(""), extensionlessSource);
    ASSERT_TRUE(extensionlessOutput);
    EXPECT_EQ(AStringView(*extensionlessOutput), "/folder/noextension");

}

TEST(AssetPaths, DerivedPathRejectsOutsideAndEmptyLogicalPaths){
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    PathFixture fixture;
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/rejection_scratch"));
    const NWB::Path outside = fixture.assetRoot / "../Elsewhere/Model.NWB";
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView(s_PROJECT), outside));
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView(s_PROJECT), outside, scratchArena));
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(scratchArena, fixture.assetRoot, AStringView(s_PROJECT), fixture.assetRoot));
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView(s_PROJECT), fixture.assetRoot, scratchArena));
    EXPECT_EQ(logger.errorCount(), 4u);
}

TEST(AssetPaths, LongAsciiAndUnicodePathsRemainCorrectAcrossRepeatedCallerArenaUse){
    for(const bool unicode : { false, true }){
        PathFixture fixture;
        PrepareWorkload(fixture, 8u, unicode ? AStringView("ÉTAGE_한글_東京_😀_") : AStringView("Long_COMPONENT_"), unicode ? 8u : 12u);
        Alloc::ScratchArena scratchArena(Name("tests/asset_path/repeated_scratch"), 65536u);
        auto* const sentinel = static_cast<u8*>(scratchArena.allocate(1u, 64u));
        ASSERT_NE(sentinel, nullptr);
        for(usize index = 0u; index < 64u; ++index)
            sentinel[index] = static_cast<u8>(index + 91u);
        const ArenaMemoryStats retained = scratchArena.memoryStats();
        ArenaMemoryStats warm;
        for(usize call = 0u; call < 16u; ++call){
            {
                AString<Alloc::ScratchArena> relative(scratchArena);
                ASSERT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(fixture.relativePath, relative));
                EXPECT_EQ(AStringView(relative), AStringView(fixture.expectedRelative));
                EXPECT_EQ(relative.c_str()[relative.size()], '\0');
            }
            EXPECT_TRUE(BuildAndVerifyDerivedPath(fixture, scratchArena));
            for(usize index = 0u; index < 64u; ++index)
                EXPECT_EQ(sentinel[index], static_cast<u8>(index + 91u));
            const ArenaMemoryStats after = scratchArena.memoryStats();
            if(call == 0u)
                warm = after;
            EXPECT_EQ(after.usedBytes, retained.usedBytes);
            EXPECT_EQ(after.reservedBytes, warm.reservedBytes);
        }
        scratchArena.deallocate(sentinel, 1u, 64u);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetPaths, RejectedUnicodeParentPreservesItsFullPrefixAndRetiresOutputStorage){
    AssetArena inputArena(Name("tests/asset_path/unicode_prefix_inputs"));
    AString<AssetArena> expected(inputArena);
    expected.reserve(512u);
    for(usize index = 0u; index < 8u; ++index)
        expected += "Étage_한글_東京_😀_";
    AString<AssetArena> input(expected, inputArena);
    input += "/./../Ignored/After";
    const NWB::Path path(inputArena, AStringView(input));
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/unicode_prefix_scratch"), 65536u);
    for(usize call = 0u; call < 16u; ++call){
        {
            AString<Alloc::ScratchArena> output(scratchArena);
            EXPECT_FALSE(AssetPathsDetail::BuildRelativeAssetPathText(path, output));
            EXPECT_EQ(AStringView(output), AStringView(expected));
            EXPECT_EQ(output.c_str()[output.size()], '\0');
        }
        EXPECT_EQ(scratchArena.memoryStats().usedBytes, 0u);
    }
}

TEST(AssetPaths, ConvertedPathByteCountingRejectsOverflowWithoutWrapping){
    AssetPathsDetail::ConvertedPathByteCounter counter{ Limit<usize>::s_Max };
    EXPECT_ANY_THROW(BasicStringDetail::WriteConvertedText<char>(counter, AStringView("x")));
    EXPECT_EQ(counter.byteCount, Limit<usize>::s_Max);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

