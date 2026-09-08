// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <pipeline/asset_builder/build_inputs.h>
#include <core/task/cpu_task.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_build_input_tests{


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
        m_root = NWB::Path(m_testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path()
            / "__build_obj" / "build_input_tests";
        ErrorCode error;
        ASSERT_TRUE(EnsureDirectories(m_root / "assets" / "empty", error));
        m_paths.repoRoot = m_root;
        m_paths.assetRoots.emplace_back(m_root / "assets", ACompactString("project"));
    }

    virtual void TearDown()override{
        m_cpuScheduler.wait();
    }

    void addFile(AStringView relativePath){
        const NWB::Path path = m_root / relativePath;
        ErrorCode error;
        ASSERT_TRUE(EnsureDirectories(path.parent_path(), error));
        GlobalFilesystemDetail::OutputFileStream stream(path, GlobalFilesystemDetail::OutputFileStream::binary);
        ASSERT_TRUE(stream);
        Assets::AssetString normalized = PathToString(m_testArena.arena, path.lexically_normal());
        CanonicalizeTextInPlace(normalized);
        m_files.emplace_back(m_testArena.arena, m_root / "assets", path, normalized, ACompactString("project"));
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

TEST_F(BuildInputSelection, ExactInputsPreserveDiscoveredOrderAndDuplicateRecords){
    addFile("assets/z.nwb");
    addFile("assets/a.nwb");
    addFile("assets/excluded.nwb");
    addFile("assets/a.nwb");
    addInput("assets/./a.nwb");
    addInput("assets/z.nwb");
    addInput("assets/a.nwb");
    ASSERT_TRUE(select());
    ASSERT_EQ(m_files.size(), 3u);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), "z.nwb");
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[1].filePath.filename()), "a.nwb");
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[2].filePath.filename()), "a.nwb");
}

TEST_F(BuildInputSelection, DirectoryInputsRespectBoundariesAndAllowEmptyRoots){
    addFile("assets/part/a.nwb");
    addFile("assets/partial/b.nwb");
    addFile("assets/part/nested/c.nwb");
    addInput("assets/part/../part");
    addInput("assets/part/nested");
    addInput("assets/empty");
    ASSERT_TRUE(select());
    ASSERT_EQ(m_files.size(), 2u);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), "a.nwb");
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[1].filePath.filename()), "c.nwb");
}

TEST_F(BuildInputSelection, RejectedInputsLeaveDiscoveredFilesIntact){
    addFile("assets/a.nwb");
    addFile("outside/b.nwb");
    addInput("assets/a.nwb");
    addInput("outside");
    EXPECT_FALSE(select());
    EXPECT_EQ(m_files.size(), 2u);
    m_options.inputs.clear();
    addInput("assets/missing.nwb");
    EXPECT_FALSE(select());
    EXPECT_EQ(m_files.size(), 2u);
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
    EXPECT_EQ(m_files.size(), 2u);
#else
    ASSERT_EQ(m_files.size(), 1u);
    EXPECT_EQ(PathToString(m_testArena.arena, m_files[0].filePath.filename()), "a.nwb");
#endif
}

TEST_F(BuildInputSelection, DISABLED_BenchmarkExplicitAndDirectoryInputs){
    constexpr usize s_FileCount = 2048u;
    for(usize i = 0u; i < s_FileCount; ++i){
        const Assets::AssetString relativePath = StringFormat(m_testArena.arena, "assets/bulk/{:04}.nwb", i);
        addFile(relativePath);
        addInput(relativePath);
    }
    const Timer explicitBegin = TimerNow();
    ASSERT_TRUE(select());
    const u64 explicitNanoseconds = DurationInNS<u64>(TimerNow(), explicitBegin);
    ASSERT_EQ(m_files.size(), s_FileCount);
    m_options.inputs.clear();
    addInput("assets/bulk");
    const Timer directoryBegin = TimerNow();
    ASSERT_TRUE(select());
    const u64 directoryNanoseconds = DurationInNS<u64>(TimerNow(), directoryBegin);
    ASSERT_EQ(m_files.size(), s_FileCount);
    char explicitText[32u] = {};
    char directoryText[32u] = {};
    RecordProperty("explicit_inputs_ns", FormatDecimal(explicitNanoseconds, explicitText).data());
    RecordProperty("directory_inputs_ns", FormatDecimal(directoryNanoseconds, directoryText).data());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

