// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/filesystem_helpers.h>
#include <tests/common/test_context.h>

#include <core/crash/module.h>
#include <core/crash/package_internal.h>
#include <core/crash/package_names.h>

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

inline constexpr Name s_FixtureArena("tests/integration/crash/archive_upload");
inline constexpr Name s_OperationScratch("tests/integration/crash/archive_upload_scratch");

static u32 g_UploadDiagnostics = 0u;
static char g_LastUploadOperation[64u] = {};
static char g_LastUploadDiagnostic[192u] = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CrashArchiveUpload : public ::testing::Test{
public:
    CrashArchiveUpload()
        : m_arena(s_FixtureArena)
        , m_root(m_arena)
    {}


protected:
    virtual void SetUp()override{
        const auto executableDirectory = GetExecutableDirectory(m_arena);
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
    bool m_rootReady = false;
};

static void ObserveUploadDiagnostic(const DiagnosticEventRecord& record)noexcept{
    if(record.category != AStringView("crash_package_upload"))
        return;
    ++g_UploadDiagnostics;
    CopyFixedBuffer(g_LastUploadOperation, record.expression);
    CopyFixedBuffer(g_LastUploadDiagnostic, record.message);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CrashArchiveUpload, PreservesNativeMissingDirectoryError){
    const TestPath missing = m_root / "missing";
    const auto nativeDirectory = DirectoryIterator<Arena>::Create(missing);
    ASSERT_FALSE(nativeDirectory);
    ScratchArena scratchArena(s_OperationScratch);
    const auto archive = NWB::Core::Crash::Detail::BuildPackageArchive(m_arena, scratchArena, missing);
    ASSERT_FALSE(archive);
    EXPECT_EQ(archive.error(), nativeDirectory.error());
}

TEST_F(CrashArchiveUpload, ResultOwnsBytesAfterWorkingScratchExpires){
    ASSERT_TRUE(WriteTextFile(m_root / "payload.txt", AStringView("payload survives scratch")));
    Optional<NWB::Core::Crash::Detail::CrashBytesT<Arena>> archive;
    {
        ScratchArena scratchArena(s_OperationScratch);
        auto produced = NWB::Core::Crash::Detail::BuildPackageArchive(m_arena, scratchArena, m_root);
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
    NWB::Core::Crash::Detail::CrashUploadSnapshot snapshot;
    ScratchArena scratchArena(s_OperationScratch);
    const AString<ScratchArena> spoolText = PathToString<char>(scratchArena, spoolDirectory);
    CopyFixedBuffer(snapshot.spoolDirectory, AStringView(spoolText));
    CopyFixedBuffer(snapshot.logServerUrl, "nwb-unsupported://local");
    NWB::Core::Crash::UninstallCrashHandler();
    const auto previousCallback = DiagnosticDetail::g_EventCallback.exchange(ObserveUploadDiagnostic, MemoryOrder::acq_rel);
    ScopeExit restoreCallback([previousCallback]()noexcept{ SetDiagnosticEventCallback(previousCallback); });
    g_UploadDiagnostics = 0u;
    EXPECT_FALSE(NWB::Core::Crash::Detail::FlushPendingCrashReportsImpl(m_arena, snapshot));
    EXPECT_EQ(g_UploadDiagnostics, 1u);
    EXPECT_EQ(AStringView(g_LastUploadOperation), AStringView("perform_upload"));
    EXPECT_NE(AStringView(g_LastUploadDiagnostic).find("curl code="), AStringView::npos);
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

