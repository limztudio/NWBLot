// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <nwb_rgd_decode.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Contain the RDF open exception at the decoder boundary.
TEST(RgdDecode, MissingFileFailsGracefully){
    AInteropString out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText("nwb_rgd_smoke_missing.rgd", out));
}

TEST(RgdDecode, GarbageInputFailsGracefully){
    constexpr AStringView s_Path = "nwb_rgd_smoke_garbage.rgd";
    {
        OutputFileStream f(s_Path.data(), s_FileOpenBinary);
        ASSERT_TRUE(f.is_open());
        f << "not a valid radeon gpu detective capture\n";
    }
    AInteropString out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText(s_Path.data(), out));

    NWB::Tests::TestArena<> testArena;
    Path<NWB::Core::Alloc::GlobalArena> inputPath(testArena.arena, s_Path);
    ErrorCode error;
    EXPECT_TRUE(RemoveFile(inputPath, error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

