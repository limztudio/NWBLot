// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/assets/paths.h>

#include <tests/common/capturing_logger.h>

#include <global/arena_memory.h>
#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_asset_path_tests{


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

struct PathSample{
    u64 elapsed = 0u;
    ArenaMemoryStats memory;
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
    const usize tokenRepeats){
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
    AString<Alloc::ScratchArena> output(scratchArena);
    return BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), fixture.sourcePath, output)
        && AStringView(output) == AStringView(fixture.expectedDerived);
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkDerivedPath(
    const usize directoryCount,
    const AStringView directoryToken,
    const usize tokenRepeats){
    PathFixture fixture;
    PrepareWorkload(fixture, directoryCount, directoryToken, tokenRepeats);
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/benchmark_scratch"), 65536u);
    auto* const sentinel = static_cast<u8*>(scratchArena.allocate(1u, 64u));
    ASSERT_NE(sentinel, nullptr);
    for(usize index = 0u; index < 64u; ++index)
        sentinel[index] = static_cast<u8>(index + 37u);

    constexpr Array<usize, 5u> s_CallCounts{ 1u, 2u, 4u, 8u, 16u };
    constexpr Array<NotNull<const char*>, 5u> s_UsedKeys{
        MakeNotNull("scratch_used_after_1"), MakeNotNull("scratch_used_after_2"), MakeNotNull("scratch_used_after_4"),
        MakeNotNull("scratch_used_after_8"), MakeNotNull("scratch_used_after_16"),
    };
    constexpr Array<NotNull<const char*>, 5u> s_ReservedKeys{
        MakeNotNull("scratch_reserved_after_1"), MakeNotNull("scratch_reserved_after_2"), MakeNotNull("scratch_reserved_after_4"),
        MakeNotNull("scratch_reserved_after_8"), MakeNotNull("scratch_reserved_after_16"),
    };
    constexpr Array<NotNull<const char*>, 5u> s_ElapsedKeys{
        MakeNotNull("path_ns_after_1"), MakeNotNull("path_ns_after_2"), MakeNotNull("path_ns_after_4"),
        MakeNotNull("path_ns_after_8"), MakeNotNull("path_ns_after_16"),
    };
    Array<PathSample, 5u> samples{};
    usize sampleIndex = 0u;
    u64 elapsed = 0u;
    bool valid = true;
    for(usize call = 1u; call <= s_CallCounts.back(); ++call){
        const Timer begin = TimerNow();
        valid &= BuildAndVerifyDerivedPath(fixture, scratchArena);
        elapsed += DurationInNS<u64>(TimerNow(), begin);
        if(call == s_CallCounts[sampleIndex]){
            samples[sampleIndex] = PathSample{ elapsed, scratchArena.memoryStats() };
            ++sampleIndex;
        }
    }
    EXPECT_TRUE(valid);
    for(usize index = 0u; index < 64u; ++index)
        EXPECT_EQ(sentinel[index], static_cast<u8>(index + 37u));
    for(usize index = 0u; index < samples.size(); ++index){
        RecordUnsignedProperty(s_UsedKeys[index], samples[index].memory.usedBytes);
        RecordUnsignedProperty(s_ReservedKeys[index], samples[index].memory.reservedBytes);
        RecordUnsignedProperty(s_ElapsedKeys[index], samples[index].elapsed);
    }
    RecordUnsignedProperty(MakeNotNull("path_build_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("path_build_calls"), s_CallCounts.back());
    RecordUnsignedProperty(MakeNotNull("path_output_bytes"), fixture.expectedDerived.size());
    RecordUnsignedProperty(MakeNotNull("scratch_peak_bytes"), samples.back().memory.peakUsedBytes);
    scratchArena.deallocate(sentinel, 1u, 64u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(AssetPaths, RelativeComponentsPreserveDotsAndCanonicalizeOnlyAsciiText){
    AssetArena inputArena(Name("tests/asset_path/relative_inputs"));
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/relative_scratch"));
    constexpr RelativeCase s_Cases[]{
        { "Models//./Hero.LOD0.NWB///", "models/hero.lod0.nwb", true },
        { "./.Hidden/../Ignored", ".hidden", false },
        { "Dir.Name/.../File.", "dir.name/.../file.", true },
        { "UPPER/Keep Space/Asset_Name", "upper/keep space/asset_name", true },
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
        output = "stale/output";
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

TEST(AssetPaths, UnicodeComponentsPreserveCodePointsAndNativeWideInput){
    AssetArena inputArena(Name("tests/asset_path/unicode_inputs"));
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/unicode_scratch"));
    constexpr AStringView s_Input = "ÉTAGE/한글/東京/😀/MODEL.NWB";
    constexpr AStringView s_Expected = "Étage/한글/東京/😀/model.nwb";
    const NWB::Path utf8Path(inputArena, s_Input);
    const NWB::Path widePath(inputArena, WStringView(L"\u00c9TAGE/\ud55c\uae00/\u6771\u4eac/\U0001f600/MODEL.NWB"));
    AString<Alloc::ScratchArena> output(scratchArena);
    ASSERT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(utf8Path, output));
    EXPECT_EQ(AStringView(output), s_Expected);
    ASSERT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(widePath, output));
    EXPECT_EQ(AStringView(output), s_Expected);
}

TEST(AssetPaths, DerivedPathRemovesOnlyFinalExtensionAndPreservesVirtualRootText){
    PathFixture fixture;
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/derived_scratch"));
    AString<Alloc::ScratchArena> output(scratchArena);
    const NWB::Path source = fixture.assetRoot / "Models//./Hero.LOD0.NWB";
    ASSERT_TRUE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("MiXeD/Root"), source, output));
    EXPECT_EQ(AStringView(output), "MiXeD/Root/models/hero.lod0");
    const NWB::Path hiddenSource = fixture.assetRoot / "Folder.Name/.Hidden";
    ASSERT_TRUE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), hiddenSource, output));
    EXPECT_EQ(AStringView(output), "project/folder.name/.hidden");
    const NWB::Path extensionlessSource = fixture.assetRoot / "Folder/NoExtension";
    ASSERT_TRUE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView(""), extensionlessSource, output));
    EXPECT_EQ(AStringView(output), "/folder/noextension");
    Name identity("stale/name");
    ASSERT_TRUE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("PROJECT"), source, identity, scratchArena));
    EXPECT_EQ(identity, Name("project/models/hero.lod0"));
}

TEST(AssetPaths, DerivedPathRejectsOutsideAndEmptyLogicalPathsAndClearsBothOutputs){
    Tests::CapturingLogger logger;
    Common::LoggerRegistrationGuard registration(logger, Common::LoggerBreakPolicy::BreakOnFatal);
    PathFixture fixture;
    Alloc::ScratchArena scratchArena(Name("tests/asset_path/rejection_scratch"));
    AString<Alloc::ScratchArena> output(scratchArena);
    const NWB::Path outside = fixture.assetRoot / "../Elsewhere/Model.NWB";
    output = "stale/output";
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), outside, output));
    EXPECT_TRUE(output.empty());
    Name identity("stale/name");
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), outside, identity, scratchArena));
    EXPECT_EQ(identity, NAME_NONE);
    output = "stale/output";
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), fixture.assetRoot, output));
    EXPECT_TRUE(output.empty());
    identity = Name("stale/name");
    EXPECT_FALSE(BuildDerivedAssetVirtualPath(fixture.assetRoot, AStringView("project"), fixture.assetRoot, identity, scratchArena));
    EXPECT_EQ(identity, NAME_NONE);
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
            {
                AString<Alloc::ScratchArena> shortOutput(scratchArena);
                const NWB::Path shortPath(fixture.arena, "Small/./Asset");
                ASSERT_TRUE(AssetPathsDetail::BuildRelativeAssetPathText(shortPath, shortOutput));
                EXPECT_EQ(AStringView(shortOutput), "small/asset");
            }
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


TEST(AssetPathBenchmark, DISABLED_ShortDerivedPath){
    BenchmarkDerivedPath(0u, AStringView(""), 0u);
}

TEST(AssetPathBenchmark, DISABLED_DeepAsciiDerivedPath){
    BenchmarkDerivedPath(64u, AStringView("Directory_"), 1u);
}

TEST(AssetPathBenchmark, DISABLED_LongComponentDerivedPath){
    BenchmarkDerivedPath(8u, AStringView("Long_COMPONENT_"), 16u);
}

TEST(AssetPathBenchmark, DISABLED_UnicodeDerivedPath){
    BenchmarkDerivedPath(12u, AStringView("ÉTAGE_한글_東京_😀_"), 8u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

