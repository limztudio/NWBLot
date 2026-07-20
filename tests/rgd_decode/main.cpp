// Runtime smoke for the in-process Radeon GPU Detective decoder. It does not depend on an actual AMD TDR .rgd
// capture (which cannot be produced on a non-AMD host); it proves the vendored RGD library stack is
// runtime-callable and rejects invalid input gracefully — no crash, no exception crossing the
// nwb_rgd::DecodeCrashDumpToText boundary — which is the failure behaviour the crash ingest relies on.
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <nwb_rgd_decode.h>

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// A path that does not exist: ParseCrashDump must fail cleanly and return false (the rdf open throws; the
// decoder contains it).
TEST(RgdDecode, MissingFileFailsGracefully){
    std::string out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText("nwb_rgd_smoke_missing.rgd", out));
}

// A non-RDF blob: the parser must reject it without crashing or throwing past the boundary.
TEST(RgdDecode, GarbageInputFailsGracefully){
    const char* const path = "nwb_rgd_smoke_garbage.rgd";
    {
        std::ofstream f(path, std::ios::binary);
        f << "not a valid radeon gpu detective capture\n";
    }
    std::string out;
    EXPECT_FALSE(nwb_rgd::DecodeCrashDumpToText(path, out));
    std::remove(path);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

