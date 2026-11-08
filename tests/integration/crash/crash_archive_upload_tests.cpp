// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>
#include <tests/integration/crash/name_cases.adhd.h>

#include <core/crash/module.h>
#include <core/crash/package_internal.h>
#include <core/crash/package_archive.h>
#include <core/crash/package_names.adhd.h>

#include <global/filesystem/executable_paths.h>
#include <global/diagnostics.h>
#include <global/filesystem/directory_iterator.h>
#include <global/optional.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_crash_archive_upload_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using Arena = NWB::Core::Alloc::GlobalArena;
using ScratchArena = NWB::Core::Alloc::ScratchArena;
using TestPath = Path<Arena>;
namespace CrashNames = NWB::Core::Crash::PackageNames;

inline constexpr Name s_FixtureArena = NWB::Tests::CrashCases::s_CrashTestNameTestsIntegrationCrashArchiveUpload;
inline constexpr Name s_OperationScratch = NWB::Tests::CrashCases::s_CrashTestNameTestsIntegrationCrashArchiveUploadScratch;

static u32 s_UploadDiagnostics = 0u;
static Array<char, 64u> s_LastUploadOperation = {};
static Array<char, 192u> s_LastUploadDiagnostic = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CrashArchiveUpload : public ::testing::Test{
public:
    CrashArchiveUpload()
        : m_arena(s_FixtureArena)
        , m_root(m_arena)
    {}


protected:
    virtual void SetUp()override{
        const auto executableDirectory = ExecutablePaths(m_arena).directory();
        ASSERT_TRUE(executableDirectory);
        const AStringView testName(::testing::UnitTest::GetInstance()->current_test_info()->name());
        m_root = *executableDirectory / "crashes" / "test_storage" / testName;
        m_rootReady = true;
        ASSERT_TRUE(RemoveAllIfExists(m_root));
        ASSERT_TRUE(EnsureDirectories(m_root));
    }

    virtual void TearDown()override{
        if(m_rootReady)
            EXPECT_TRUE(RemoveAllIfExists(m_root));
    }


protected:
    Arena m_arena;
    TestPath m_root;


private:
    b8 m_rootReady = false;
};

static void ObserveUploadDiagnostic(const DiagnosticEventRecord& record)noexcept{
    if(record.category != AStringView("crash_package_upload"))
        return;
    ++s_UploadDiagnostics;
    CopyFixedBuffer(s_LastUploadOperation, record.expression);
    CopyFixedBuffer(s_LastUploadDiagnostic, record.message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CrashArchiveUpload, PreservesNativeMissingDirectoryError){
    const TestPath missing = m_root / "missing";
    const auto nativeDirectory = DirectoryIterator<Arena>::Create(missing);
    ASSERT_FALSE(nativeDirectory);
    ScratchArena scratchArena(s_OperationScratch);
    const auto archive = NWB::Core::Crash::CrashDetail::CrashPackageArchive::build(m_arena, scratchArena, missing);
    ASSERT_FALSE(archive);
    EXPECT_EQ(archive.error(), nativeDirectory.error());
}

TEST_F(CrashArchiveUpload, ResultOwnsBytesAfterWorkingScratchExpires){
    ASSERT_TRUE(WriteTextFile(m_root / "payload.txt", AStringView("payload survives scratch")));
    Optional<NWB::Core::Crash::CrashDetail::CrashBytesT<Arena>> archive;
    {
        ScratchArena scratchArena(s_OperationScratch);
        auto produced = NWB::Core::Crash::CrashDetail::CrashPackageArchive::build(m_arena, scratchArena, m_root);
        ASSERT_TRUE(produced);
        archive.emplace(Move(*produced));
    }
    ASSERT_TRUE(archive);
    const AStringView bytes(reinterpret_cast<const char*>(archive->data()), archive->size());
    EXPECT_EQ(bytes.find(CrashNames::s_ArchiveHeaderText), 0u);
    EXPECT_NE(bytes.find("payload.txt"), AStringView::npos);
    EXPECT_NE(bytes.find("payload survives scratch"), AStringView::npos);
}

TEST_F(CrashArchiveUpload, UnsupportedProtocolPreservesFailureAndPendingRetry){
    constexpr AStringView s_CrashId("crash-unsupported-protocol");
    const TestPath spoolDirectory = m_root / "spool";
    const TestPath pending = spoolDirectory / CrashNames::s_PendingDirectoryName / s_CrashId;
    ASSERT_TRUE(EnsureDirectories(pending));
    ASSERT_TRUE(WriteTextFile(pending / "marker.txt", AStringView("package")));
    NWB::Core::Crash::CrashDetail::CrashUploadSnapshot snapshot;
    ScratchArena scratchArena(s_OperationScratch);
    const AString<ScratchArena> spoolText = PathToString<char>(scratchArena, spoolDirectory);
    CopyFixedBuffer(snapshot.spoolDirectory, AStringView(spoolText));
    CopyFixedBuffer(snapshot.logServerUrl, "nwb-unsupported://local");
    NWB::Core::Crash::UninstallCrashHandler();
    const auto previousCallback = DiagnosticDetail::g_EventCallback.exchange(ObserveUploadDiagnostic, MemoryOrder::acq_rel);
    ScopeExit restoreCallback([previousCallback]()noexcept{ SetDiagnosticEventCallback(previousCallback); });
    s_UploadDiagnostics = 0u;
    EXPECT_FALSE(NWB::Core::Crash::CrashDetail::FlushPendingCrashReportsImpl(m_arena, snapshot));
    EXPECT_EQ(s_UploadDiagnostics, 1u);
    EXPECT_EQ(FixedBufferView(s_LastUploadOperation), AStringView("perform_upload"));
    EXPECT_NE(FixedBufferView(s_LastUploadDiagnostic).find("curl code="), AStringView::npos);
    const auto retained = IsDirectory(pending);
    ASSERT_TRUE(retained);
    EXPECT_TRUE(*retained);
    const auto uploading = IsDirectory(spoolDirectory / CrashNames::s_UploadingDirectoryName / s_CrashId);
    ASSERT_TRUE(uploading);
    EXPECT_FALSE(*uploading);
    EXPECT_TRUE(NWB::Tests::TextFileContains(pending / CrashNames::s_UploadAttemptFileName, CrashNames::s_UploadAttemptRetryPendingState));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

