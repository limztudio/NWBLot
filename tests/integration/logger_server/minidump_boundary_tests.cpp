// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/capturing_logger.h>
#include <tests/common/test_context.h>

#include <core/alloc/scratch.h>
#include <core/crash/package_names.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/utility.h>

#include <logger/server/crash_symbolicate.h>

#include <gtest/gtest.h>

#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
#include <dbghelp.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_minidump_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using DumpBytes = Vector<u8, Core::Alloc::GlobalArena>;

template<typename T>
void StoreDumpRecord(DumpBytes& bytes, const usize offset, const T& record)noexcept{
    NWB_ASSERT(offset <= bytes.size() && sizeof(T) <= bytes.size() - offset);
    NWB_MEMCPY(bytes.data() + offset, bytes.size() - offset, &record, sizeof(T));
}

[[nodiscard]] DumpBytes ExceptionDump(Core::Alloc::GlobalArena& arena, const MINIDUMP_STREAM_TYPE secondStream, const ULONG secondStreamBytes){
    constexpr usize directoryCount = 2u;
    constexpr usize exceptionOffset = sizeof(MINIDUMP_HEADER) + directoryCount * sizeof(MINIDUMP_DIRECTORY);
    constexpr usize contextOffset = exceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM);
    constexpr usize secondStreamOffset = contextOffset + sizeof(CONTEXT);
    DumpBytes bytes(arena);
    bytes.resize(secondStreamOffset + secondStreamBytes, 0u);
    MINIDUMP_HEADER header{};
    header.Signature = MINIDUMP_SIGNATURE;
    header.Version = MINIDUMP_VERSION;
    header.NumberOfStreams = directoryCount;
    header.StreamDirectoryRva = sizeof(MINIDUMP_HEADER);
    StoreDumpRecord(bytes, 0u, header);
    MINIDUMP_DIRECTORY exceptionDirectory{};
    exceptionDirectory.StreamType = ExceptionStream;
    exceptionDirectory.Location.DataSize = sizeof(MINIDUMP_EXCEPTION_STREAM);
    exceptionDirectory.Location.Rva = exceptionOffset;
    StoreDumpRecord(bytes, sizeof(MINIDUMP_HEADER), exceptionDirectory);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT);
    exception.ThreadContext.Rva = contextOffset;
    StoreDumpRecord(bytes, exceptionOffset, exception);
    MINIDUMP_DIRECTORY secondDirectory{};
    secondDirectory.StreamType = secondStream;
    secondDirectory.Location.DataSize = secondStreamBytes;
    secondDirectory.Location.Rva = secondStreamOffset;
    StoreDumpRecord(bytes, sizeof(MINIDUMP_HEADER) + sizeof(MINIDUMP_DIRECTORY), secondDirectory);
    return bytes;
}

class MinidumpBoundaryTests : public testing::Test{
public:
    MinidumpBoundaryTests()
        : m_directory(m_testArena.arena, "logserver_minidump_boundary_tests")
        , m_loggerGuard(m_logger, Core::Common::LoggerBreakPolicy::ReportOnly)
    {}


protected:
    virtual void SetUp()override{
        ASSERT_TRUE(EnsureEmptyDirectory(m_directory));
    }
    virtual void TearDown()override{
        EXPECT_TRUE(RemoveAllIfExists(m_directory));
    }
    [[nodiscard]] Expected<Log::CrashReportText> report(const DumpBytes& bytes){
        if(!WriteBinaryFile(m_directory / Core::Crash::PackageNames::s_ProcessDumpFileName, bytes))
            return MakeUnexpected(Failure{});
        Log::CrashPackageSummary summary(m_testArena.arena);
        summary.platform = "windows";
        summary.threadId = 1u;
        Log::CrashSymbolicationConfig config(m_testArena.arena);
        return Log::BuildCrashSymbolicationReport(m_testArena.arena, m_directory, summary, config, m_scratchArena);
    }


protected:
    Tests::TestArena<> m_testArena;
    Core::Alloc::ScratchArena m_scratchArena{ Name("tests/minidump_boundary/scratch") };
    ::Path<Core::Alloc::GlobalArena> m_directory;
    Tests::CapturingLogger m_logger;
    Core::Common::LoggerRegistrationGuard m_loggerGuard;
};

TEST_F(MinidumpBoundaryTests, TruncatedHeadersAndOutOfFileStreamDirectoriesCannotDecode){
    DumpBytes bytes(m_testArena.arena);
    bytes.resize(sizeof(MINIDUMP_HEADER), 0u);
    for(const usize size : Array<usize, 3u>{ 1u, sizeof(MINIDUMP_HEADER) - 1u, sizeof(MINIDUMP_HEADER) }){
        bytes.resize(size, 0u);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    }
    bytes.resize(sizeof(MINIDUMP_HEADER), 0u);
    MINIDUMP_HEADER header{};
    header.Signature = MINIDUMP_SIGNATURE;
    header.Version = MINIDUMP_VERSION;
    header.StreamDirectoryRva = sizeof(MINIDUMP_HEADER);
    for(const ULONG count : Array<ULONG, 2u>{ 1u, Limit<ULONG>::s_Max }){
        header.NumberOfStreams = count;
        StoreDumpRecord(bytes, 0u, header);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    }
    header.NumberOfStreams = 1u;
    header.StreamDirectoryRva = Limit<RVA>::s_Max;
    StoreDumpRecord(bytes, 0u, header);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, TruncatedThreadListCountCannotReadBeyondItsAdmittedStream){
    auto bytes = ExceptionDump(m_testArena.arena, ThreadListStream, sizeof(ULONG));
    const ULONG count = Limit<ULONG>::s_Max;
    StoreDumpRecord(bytes, bytes.size() - sizeof(ULONG), count);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, ShortExceptionContextCannotBorrowSurroundingFileBytes){
    auto bytes = ExceptionDump(m_testArena.arena, UnusedStream, 0u);
    constexpr usize exceptionOffset = sizeof(MINIDUMP_HEADER) + 2u * sizeof(MINIDUMP_DIRECTORY);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT) - 1u;
    exception.ThreadContext.Rva = exceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM);
    StoreDumpRecord(bytes, exceptionOffset, exception);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
    EXPECT_NE(actual->find("no usable thread context"), Log::CrashReportText::npos);
}

TEST_F(MinidumpBoundaryTests, Memory64OffsetsAndLengthsCannotTruncateIntoASmallFile){
    for(const bool oversizedOffset : Array<bool, 2u>{ false, true }){
        constexpr usize streamSize = offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges) + sizeof(MINIDUMP_MEMORY_DESCRIPTOR64);
        auto bytes = ExceptionDump(m_testArena.arena, Memory64ListStream, streamSize);
        const usize streamOffset = bytes.size() - streamSize;
        const ULONG64 count = 1u;
        const RVA64 baseRva = oversizedOffset ? (u64(1u) << 32u) + bytes.size() - 1u : bytes.size() - 1u;
        MINIDUMP_MEMORY_DESCRIPTOR64 memory{};
        memory.StartOfMemoryRange = 0x1000u;
        memory.DataSize = oversizedOffset ? 1u : (u64(1u) << 32u) + 1u;
        StoreDumpRecord(bytes, streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, NumberOfMemoryRanges), count);
        StoreDumpRecord(bytes, streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, BaseRva), baseRva);
        StoreDumpRecord(bytes, streamOffset + offsetof(MINIDUMP_MEMORY64_LIST, MemoryRanges), memory);
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("status=not_decoded"), Log::CrashReportText::npos);
        EXPECT_NE(actual->find("minidump contains invalid memory ranges"), Log::CrashReportText::npos);
    }
}

TEST_F(MinidumpBoundaryTests, TruncatedModuleListCannotReadBeyondItsAdmittedStream){
    auto bytes = ExceptionDump(m_testArena.arena, ModuleListStream, sizeof(ULONG));
    const ULONG count = Limit<ULONG>::s_Max;
    StoreDumpRecord(bytes, bytes.size() - sizeof(ULONG), count);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("module_load=invalid_module_list"), Log::CrashReportText::npos);
}


TEST_F(MinidumpBoundaryTests, InvalidModuleNameLengthsAndTerminatorsCannotBorrowOtherDumpBytes){
    struct InvalidName{
        ULONG byteCount;
        bool terminated;
        bool includeTerminator;
    };
    for(const auto& invalid : Array<InvalidName, 4u>{ InvalidName{ 1u, true, true }, InvalidName{ Limit<ULONG>::s_Max - 1u, true, true }, InvalidName{ sizeof(wchar), false, true }, InvalidName{ sizeof(wchar), false, false } }){
        constexpr usize streamSize = offsetof(MINIDUMP_MODULE_LIST, Modules) + sizeof(MINIDUMP_MODULE);
        auto bytes = ExceptionDump(m_testArena.arena, ModuleListStream, streamSize);
        const usize streamOffset = bytes.size() - streamSize;
        const ULONG count = 1u;
        const RVA nameOffset = static_cast<RVA>(bytes.size());
        MINIDUMP_MODULE module{};
        module.ModuleNameRva = nameOffset;
        StoreDumpRecord(bytes, streamOffset, count);
        StoreDumpRecord(bytes, streamOffset + offsetof(MINIDUMP_MODULE_LIST, Modules), module);
        bytes.resize(bytes.size() + sizeof(ULONG) + sizeof(wchar) * (invalid.includeTerminator ? 2u : 1u), 0u);
        StoreDumpRecord(bytes, nameOffset, invalid.byteCount);
        const wchar character = L'a';
        StoreDumpRecord(bytes, nameOffset + sizeof(ULONG), character);
        if(invalid.includeTerminator){
            const wchar terminator = invalid.terminated ? L'\0' : L'x';
            StoreDumpRecord(bytes, nameOffset + sizeof(ULONG) + sizeof(wchar), terminator);
        }
        const auto actual = report(bytes);
        ASSERT_TRUE(actual);
        EXPECT_NE(actual->find("module_load=invalid_module_list"), Log::CrashReportText::npos);
    }
}

TEST_F(MinidumpBoundaryTests, UnalignedExceptionAndContextRecordsKeepTheirBoundedValues){
    const auto original = ExceptionDump(m_testArena.arena, UnusedStream, 0u);
    constexpr usize exceptionOffset = sizeof(MINIDUMP_HEADER) + 2u * sizeof(MINIDUMP_DIRECTORY);
    DumpBytes bytes(m_testArena.arena);
    bytes.resize(original.size() + 1u, 0u);
    NWB_MEMCPY(bytes.data(), bytes.size(), original.data(), exceptionOffset);
    NWB_MEMCPY(bytes.data() + exceptionOffset + 1u, bytes.size() - exceptionOffset - 1u, original.data() + exceptionOffset, original.size() - exceptionOffset);
    MINIDUMP_DIRECTORY directory{};
    directory.StreamType = ExceptionStream;
    directory.Location.DataSize = sizeof(MINIDUMP_EXCEPTION_STREAM);
    directory.Location.Rva = exceptionOffset + 1u;
    StoreDumpRecord(bytes, sizeof(MINIDUMP_HEADER), directory);
    MINIDUMP_EXCEPTION_STREAM exception{};
    exception.ThreadId = 1u;
    exception.ThreadContext.DataSize = sizeof(CONTEXT);
    exception.ThreadContext.Rva = exceptionOffset + sizeof(MINIDUMP_EXCEPTION_STREAM) + 1u;
    StoreDumpRecord(bytes, exceptionOffset + 1u, exception);
    const auto actual = report(bytes);
    ASSERT_TRUE(actual);
    EXPECT_NE(actual->find("status=decoded"), Log::CrashReportText::npos);
    EXPECT_EQ(actual->find("status=not_decoded"), Log::CrashReportText::npos);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

