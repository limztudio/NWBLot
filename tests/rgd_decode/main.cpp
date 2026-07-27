// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Runtime smoke for the in-process Radeon GPU Detective decoder. It does not depend on an actual AMD TDR .rgd
// capture (which cannot be produced on a non-AMD host); it proves the vendored RGD library stack is
// runtime-callable and rejects invalid input gracefully — no crash, no exception crossing the
// nwb_rgd::DecodeCrashDumpToText boundary — which is the failure behaviour the crash ingest relies on.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/test_context.h>

#include <nwb_rgd_decode.h>

#include <global/filesystem.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A path that does not exist: ParseCrashDump must fail cleanly and return false (the rdf open throws; the
// decoder contains it).
TEST(RgdDecode, MissingFileFailsGracefully){
    AInteropString out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText("nwb_rgd_smoke_missing.rgd", out));
}

// A non-RDF blob: the parser must reject it without crashing or throwing past the boundary.
TEST(RgdDecode, GarbageInputFailsGracefully){
    const char* const path = "nwb_rgd_smoke_garbage.rgd";
    {
        OutputFileStream f(path, s_FileOpenBinary);
        ASSERT_TRUE(f.is_open());
        f << "not a valid radeon gpu detective capture\n";
    }
    AInteropString out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText(path, out));

    NWB::Tests::TestArena<> testArena;
    Path<NWB::Core::Alloc::GlobalArena> inputPath(testArena.arena, path);
    ErrorCode error;
    EXPECT_TRUE(RemoveFile(inputPath, error));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

