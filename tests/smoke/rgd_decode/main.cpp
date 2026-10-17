// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <logger/rgd/decode.h>

#include <global/array.h>
#include <global/diagnostics.h>
#include <global/filesystem.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Missing files fail explicit RDF admission before the throwing vendor C++ parser.
TEST(RgdDecode, MissingFileFailsGracefully){
    const auto result = NWB::Log::Rgd::DecodeCrashDumpToText("nwb_rgd_smoke_missing.rgd");
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), StringView("rdf_open_failed"));
}

TEST(RgdDecode, GarbageInputFailsGracefully){
    constexpr AStringView s_Path = "nwb_rgd_smoke_garbage.rgd";
    NWB::Tests::TestArena<> testArena;
    Path<NWB::Core::Alloc::GlobalArena> inputPath(testArena.arena, s_Path);
    ScopeExit cleanup([&inputPath]()noexcept{
        const auto removed = RemoveFile(inputPath);
        if(!removed)
            CaptureDiagnosticEvent("rgd_decode_test_cleanup", "Failed to remove garbage-input fixture", __FILE__, __LINE__);
    });
    ASSERT_TRUE(WriteBinaryFile(inputPath, StringView("not a valid radeon gpu detective capture\n")));

    const auto result = NWB::Log::Rgd::DecodeCrashDumpToText(s_Path.data());
    const auto removed = RemoveFile(inputPath);
    ASSERT_TRUE(removed);
    EXPECT_TRUE(*removed);
    cleanup.release();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), StringView("parse_failed"));
}

TEST(RgdDecode, AdmittedContainerWithoutCrashChunksFailsGracefully){
    // Current RDF container header with an empty index; this reaches the crash parser after file/format admission.
    constexpr Array<u8, 32u> s_EmptyContainer{
        'A', 'M', 'D', '_', 'R', 'D', 'F', ' ',
        3u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        32u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
        0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    };
    constexpr AStringView s_Path = "nwb_rgd_smoke_empty_container.rgd";
    NWB::Tests::TestArena<> testArena;
    Path<NWB::Core::Alloc::GlobalArena> inputPath(testArena.arena, s_Path);
    ScopeExit cleanup([&inputPath]()noexcept{
        const auto removed = RemoveFile(inputPath);
        if(!removed)
            CaptureDiagnosticEvent("rgd_decode_test_cleanup", "Failed to remove admitted-container fixture", __FILE__, __LINE__);
    });
    ASSERT_TRUE(WriteBinaryFile(inputPath, s_EmptyContainer));

    const auto result = NWB::Log::Rgd::DecodeCrashDumpToText(AInteropString(s_Path));
    const auto removed = RemoveFile(inputPath);
    ASSERT_TRUE(removed);
    EXPECT_TRUE(*removed);
    cleanup.release();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error(), StringView("parse_failed"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

