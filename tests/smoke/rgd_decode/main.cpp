// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <nwb_rgd_decode.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Missing files fail explicit RDF admission before the throwing vendor C++ parser.
TEST(RgdDecode, MissingFileFailsGracefully){
    const auto result = nwb_rgd::DecodeCrashDumpToText("nwb_rgd_smoke_missing.rgd");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), StringView("rdf_open_failed"));
}

TEST(RgdDecode, GarbageInputFailsGracefully){
    constexpr AStringView s_Path = "nwb_rgd_smoke_garbage.rgd";
    {
        OutputFileStream f(s_Path.data(), s_FileOpenBinary);
        ASSERT_TRUE(f.is_open());
        f << "not a valid radeon gpu detective capture\n";
    }
    const auto result = nwb_rgd::DecodeCrashDumpToText(s_Path.data());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), StringView("parse_failed"));

    NWB::Tests::TestArena<> testArena;
    Path<NWB::Core::Alloc::GlobalArena> inputPath(testArena.arena, s_Path);
    const auto removed = RemoveFile(inputPath);
    ASSERT_TRUE(removed);
    EXPECT_TRUE(*removed);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

