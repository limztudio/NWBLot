// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <pipeline/asset_gatherer/gather.h>
#include <core/assets/volume/built_asset.h>
#include <core/filesystem/factory.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gather_input_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Assets = NWB::Core::Assets;
namespace Pack = Assets::AssetsVolumeCookDetail;
namespace Gatherer = NWB::Pipeline::AssetGatherer;
namespace Filesystem = NWB::Core::Filesystem;

static usize s_MergeCalls = 0u;

[[nodiscard]] static bool MergeBytes(const Name&, Assets::AssetBytes& existing, const void* incoming, const usize size){
    ++s_MergeCalls;
    const auto* bytes = static_cast<const u8*>(incoming);
    existing.insert(existing.end(), bytes, bytes + size);
    return true;
}

[[nodiscard]] static Name AssetName(const usize index){
    char text[32u] = {};
    return DeriveName(Name("project/gather/"), FormatDecimal(index, text));
}

class GatherInputs : public testing::Test{
protected:
    NWB::Tests::TestArena<> m_testArena;
    NWB::Tests::CapturingLogger m_logger;
    NWB::Core::Common::LoggerRegistrationGuard m_loggerGuard{ m_logger, NWB::Core::Common::LoggerBreakPolicy::BreakOnFatal };
    NWB::Path m_root{ m_testArena.arena };
    Gatherer::AssetGatherOptions m_options{ m_testArena.arena };

    virtual void SetUp()override{
        Assets::AssetString caseKey(m_testArena.arena);
        AppendHexU32(static_cast<u32>(ComputeFnv64Text(AStringView(testing::UnitTest::GetInstance()->current_test_info()->name()))), caseKey);
        m_root = NWB::Path(m_testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path()
            / "__build_obj" / "g" / caseKey;
        m_options.outputDirectory = PathToString(m_testArena.arena, m_root / "out");
        m_options.configuration = "tests";
        s_MergeCalls = 0u;
    }

    void writeInputs(AStringView directory, const usize count, const usize payloadSize, const u8 seed){
        Pack::AssetVolumePackManifest manifest(m_testArena.arena);
        ASSERT_TRUE(Pack::ReserveAssetVolumePackManifest(manifest, count));
        Assets::AssetBytes bytes(payloadSize, seed, m_testArena.arena);
        for(usize i = 0u; i < count; ++i){
            if(!bytes.empty())
                bytes.front() = static_cast<u8>(seed + i);
            ASSERT_TRUE(Pack::AppendPayloadBytesToManifest(manifest, AssetName(i), bytes));
        }
        ASSERT_TRUE(Assets::BuiltAssetDetail::WriteBuiltAssets(m_root / directory, manifest));
        m_options.inputs.emplace_back(PathToString(m_testArena.arena, m_root / directory));
    }

    void verifyPayloads(const usize count, const usize payloadSize, const u8 seed){
        Filesystem::VolumeMountDesc desc(m_testArena.arena);
        desc.volumeName = "graphics";
        desc.mountDirectory = m_root / "out";
        auto filesystem = Filesystem::CreateFilesystem(m_testArena.arena, desc);
        ASSERT_TRUE(filesystem);
        ASSERT_TRUE(filesystem->mounted());
        EXPECT_EQ(filesystem->fileCount(), count);
        Assets::AssetBytes bytes(m_testArena.arena);
        for(usize i = 0u; i < count; ++i){
            ASSERT_TRUE(filesystem->readFile(AssetName(i), bytes));
            ASSERT_EQ(bytes.size(), payloadSize);
            if(!bytes.empty())
                EXPECT_EQ(bytes.front(), static_cast<u8>(seed + i));
            if(bytes.size() > 1u)
                EXPECT_EQ(bytes.back(), seed);
        }
        EXPECT_TRUE(filesystem->unmount());
    }
};

TEST_F(GatherInputs, IdenticalDuplicatesKeepExactPayloadsWithoutMergeCalls){
    writeInputs("a", 64u, 257u, 7u);
    writeInputs("b", 64u, 257u, 7u);
    m_options.inputs.push_back(m_options.inputs.front());
    m_options.mergePayloads = &MergeBytes;
    ASSERT_TRUE(Gatherer::GatherAssets(m_options));
    EXPECT_EQ(s_MergeCalls, 0u);
    verifyPayloads(64u, 257u, 7u);
}

TEST_F(GatherInputs, ConflictingDuplicatesPreservePublishedVolume){
    writeInputs("a", 4u, 8u, 3u);
    ASSERT_TRUE(Gatherer::GatherAssets(m_options));
    writeInputs("b", 4u, 8u, 9u);
    EXPECT_FALSE(Gatherer::GatherAssets(m_options));
    EXPECT_GT(m_logger.errorCount(), 0u);
    verifyPayloads(4u, 8u, 3u);
}

TEST_F(GatherInputs, MergedPayloadIdentitySurvivesLaterIdenticalDuplicate){
    writeInputs("a", 1u, 2u, 5u);
    writeInputs("b", 1u, 2u, 5u);
    writeInputs("c", 1u, 4u, 5u);
    // The first two equal payloads collapse without merging; the third is appended exactly once.
    m_options.mergePayloads = &MergeBytes;
    ASSERT_TRUE(Gatherer::GatherAssets(m_options));
    EXPECT_EQ(s_MergeCalls, 1u);
    verifyPayloads(1u, 6u, 5u);
    writeInputs("d", 1u, 6u, 5u);
    s_MergeCalls = 0u;
    ASSERT_TRUE(Gatherer::GatherAssets(m_options));
    EXPECT_EQ(s_MergeCalls, 1u);
    verifyPayloads(1u, 6u, 5u);
}

TEST_F(GatherInputs, DISABLED_BenchmarkDuplicatePayloads){
    constexpr usize s_AssetCount = 128u;
    constexpr usize s_PayloadBytes = 65536u;
    writeInputs("a", s_AssetCount, s_PayloadBytes, 1u);
    writeInputs("b", s_AssetCount, s_PayloadBytes, 1u);
    writeInputs("c", s_AssetCount, s_PayloadBytes, 1u);
    const Timer begin = TimerNow();
    const bool gathered = Gatherer::GatherAssets(m_options);
    const u64 elapsed = DurationInNS<u64>(TimerNow(), begin);
    ASSERT_TRUE(gathered);
    verifyPayloads(s_AssetCount, s_PayloadBytes, 1u);
    char timingText[32u] = {};
    RecordProperty("gather_ns", FormatDecimal(elapsed, timingText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

