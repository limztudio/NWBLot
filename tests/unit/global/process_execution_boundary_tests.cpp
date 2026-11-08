// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/environment.h>
#include <global/filesystem/staged_file_writer.h>
#include <global/filesystem/retention.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path_identity.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/limit.h>
#include <global/process_execution.h>
#include <global/process_capture.h>
#include <global/shared_library.h>
#include <global/text_utils.h>

#if defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#include <sys/wait.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_process_execution_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_CombinedOutputFilename = "combined output.txt";
using AString = NWB::Tests::TestAString;
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_BinSh = "/bin/sh";
static constexpr AStringView s_C = "-c";
#endif
#if defined(NWB_PLATFORM_WINDOWS)
static constexpr AStringView s_TrailingSuffix = ".trailing";
#endif


TEST(Global, ProcessArgumentValidationPreservesExistingOutput){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/invalid_process_arguments");
    const auto outputPath = root / "output.txt";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(outputPath, AStringView("retained")));
    const auto outputPathText = PathToString<char>(testArena.arena, outputPath);

    const Array<char, 3u> invalidArgument = { 'a', '\0', 'b' };
    const Array<AStringView, 2u> arguments = { "unused_program", AStringView(invalidArgument.data(), invalidArgument.size()) };
    EXPECT_FALSE(RunProcessRedirectedToFile(testArena.arena, arguments, AStringView(outputPathText)));
    EXPECT_FALSE(RunProcessRedirectedToFile(testArena.arena, {}, AStringView(outputPathText)));
    AString output(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_EQ(output, "retained");
    EXPECT_TRUE(RemoveAllIfExists(root));
}

#if defined(NWB_PLATFORM_WINDOWS)
TEST(Global, WindowsProcessInputsSupportBoundedViews){
    NWB::Tests::TestArena<> testArena;
    auto executableResult = ReadEnvironmentVariable(testArena.arena, "ComSpec");
    ASSERT_TRUE(executableResult);
    ASSERT_TRUE(*executableResult);
    auto executableText = Move(**executableResult);
    const Path<NWB::Core::Alloc::GlobalArena> executablePath(testArena.arena, AStringView(executableText));
    ::AString<NWB::Core::Alloc::GlobalArena> searchPathText = PathToString<char>(testArena.arena, executablePath.parentPathView());
    ::AString<NWB::Core::Alloc::GlobalArena> executableNameText = PathToString<char>(testArena.arena, executablePath.filenameView());
    const usize searchPathLength = searchPathText.size();
    const usize executableNameLength = executableNameText.size();
    searchPathText += s_TrailingSuffix;
    executableNameText += s_TrailingSuffix;
    const auto executableAvailable = ExecutableAvailableInPath(testArena.arena, AStringView(searchPathText).substr(0u, searchPathLength), AStringView(executableNameText).substr(0u, executableNameLength));
    ASSERT_TRUE(executableAvailable);
    EXPECT_TRUE(*executableAvailable);

    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/bounded_process");
    const auto outputPath = root / s_CombinedOutputFilename;
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ::AString<NWB::Core::Alloc::GlobalArena> outputPathText = PathToString<char>(testArena.arena, outputPath);
    const usize outputPathLength = outputPathText.size();
    const usize executableLength = executableText.size();
    outputPathText += s_TrailingSuffix;
    executableText += s_TrailingSuffix;
    const Array<AStringView, 3u> arguments = { AStringView(executableText).substr(0u, executableLength), "/c", "echo stdout&echo stderr 1>&2&exit /b 23" };
    const auto exitCode = RunProcessRedirectedToFile(testArena.arena, arguments, AStringView(outputPathText).substr(0u, outputPathLength));
    ASSERT_TRUE(exitCode);
    EXPECT_EQ(*exitCode, 23);
    ::AString<NWB::Core::Alloc::GlobalArena> output(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_NE(AStringView(output).find("stdout"), AStringView::npos);
    EXPECT_NE(AStringView(output).find("stderr"), AStringView::npos);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

#endif

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
struct ThrowingCaptureOutput{
    Array<char, 64u> bytes{};
    usize byteCount = 0u;
    pid_t childPid = 0;


public:
    void clear()noexcept{
        byteCount = 0u;
        childPid = 0;
    }

    [[nodiscard]] usize size()const noexcept{ return byteCount; }

    [[nodiscard]] b8 empty()const noexcept{ return byteCount == 0u; }

    void append(const char* data, const usize length){
        if(length > bytes.size() - byteCount)
            throw RuntimeException("child PID output exceeded the bounded fixture buffer");
        NWB_MEMCPY(bytes.data() + byteCount, bytes.size() - byteCount, data, length);
        byteCount += length;
        const AStringView output(bytes.data(), byteCount);
        const usize lineEnd = output.find('\n');
        if(lineEnd == AStringView::npos)
            return;
        const auto parsedPid = ParseU64(output.substr(0u, lineEnd));
        if(!parsedPid || *parsedPid == 0u || *parsedPid > static_cast<u64>(Limit<pid_t>::s_Max))
            throw RuntimeException("child PID output was invalid");
        childPid = static_cast<pid_t>(*parsedPid);
        throw RuntimeException("fixture output append failure");
    }
};

TEST(Global, CaptureProcessOutputAdmitsEmptySuccessAndPreservesExitFailure){
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    const auto invalid = CaptureProcessOutput(output, testArena.arena, {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().kind, ProcessCaptureFailureKind::InvalidArguments);
    EXPECT_EQ(invalid.error().nativeError, ErrorCondition::invalid_argument);
    const Array<AStringView, 3u> emptyArguments = { s_BinSh, s_C, "exit 0" };
    ASSERT_TRUE(CaptureProcessOutput(output, testArena.arena, emptyArguments));
    EXPECT_TRUE(output.empty());
    const Array<AStringView, 3u> failedArguments = { s_BinSh, s_C, "printf retained; exit 23" };
    const auto failed = CaptureProcessOutput(output, testArena.arena, failedArguments);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().kind, ProcessCaptureFailureKind::ChildExitedUnsuccessfully);
    EXPECT_EQ(failed.error().exitCode, 23);
    EXPECT_EQ(AStringView(output), "retained");
}

TEST(Global, CaptureProcessOutputAppendFailureClosesPipeAndReapsChildBeforeUnwinding){
    const Array<AStringView, 3u> argv = { s_BinSh, s_C, "printf '%s\\n' \"$$\"; exec 1>&-; while :; do :; done" };
    NWB::Tests::TestArena<> testArena;
    ThrowingCaptureOutput output;
    constexpr u32 s_CaptureTimeoutMS = 250u;
    EXPECT_THROW(EXPECT_FALSE(CaptureProcessOutput(output, testArena.arena, argv, 64u, 64u, s_CaptureTimeoutMS)), RuntimeException);
    ASSERT_GT(output.childPid, 0);
    int status = 0;
    errno = 0;
    const pid_t waitResult = ::waitpid(output.childPid, &status, WNOHANG);
    const int waitError = errno;
    if(waitResult == 0)
        ProcessExecutionDetail::KillAndReapProcess(output.childPid);
    EXPECT_EQ(waitResult, -1);
    EXPECT_EQ(waitError, ECHILD);
}

TEST(Global, CaptureProcessOutputReapsTruncatedChild){
    const Array<AStringView, 3u> argv = { s_BinSh, s_C, "printf '%s\\nxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' \"$$\"; exec 1>&-; while :; do :; done" };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    constexpr u32 s_CaptureTimeoutMS = 250u;
    const auto captured = CaptureProcessOutput(output, testArena.arena, argv, 16u, 64u, s_CaptureTimeoutMS);
    ASSERT_FALSE(captured);
    EXPECT_TRUE(captured.error().outputTruncated);

    const usize lineEnd = output.find('\n');
    ASSERT_NE(lineEnd, AString::npos);

    const auto childPidValue = ParseU64(AStringView(output.data(), lineEnd));
    ASSERT_TRUE(childPidValue);
    ASSERT_GT(*childPidValue, 0u);
    ASSERT_LE(*childPidValue, static_cast<u64>(Limit<pid_t>::s_Max));

    const pid_t childPid = static_cast<pid_t>(*childPidValue);
    int status = 0;
    errno = 0;
    const pid_t waitResult = ::waitpid(childPid, &status, WNOHANG);
    const int waitError = errno;
    if(waitResult == 0)
        ProcessExecutionDetail::KillAndReapProcess(childPid);

    EXPECT_EQ(waitResult, -1);
    EXPECT_EQ(waitError, ECHILD);
}

TEST(Global, CaptureProcessOutputTimesOutWithContinuousOutput){
    const Array<AStringView, 3u> argv = { s_BinSh, s_C, "while :; do printf x; done" };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    const auto startMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    ASSERT_TRUE(startMilliseconds);
    constexpr u32 s_ContinuousOutputTimeoutMS = 100u;
    const auto captured = CaptureProcessOutput(output, testArena.arena, argv, 64u, 64u, s_ContinuousOutputTimeoutMS);
    ASSERT_FALSE(captured);
    EXPECT_EQ(captured.error().kind, ProcessCaptureFailureKind::TimedOut);
    EXPECT_EQ(captured.error().nativeError, ErrorCondition::timed_out);
    const auto finishMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    ASSERT_TRUE(finishMilliseconds);
    ASSERT_GE(*finishMilliseconds, *startMilliseconds);
    const u64 elapsedMilliseconds = *finishMilliseconds - *startMilliseconds;
    constexpr u32 s_MaximumCaptureElapsedMS = 1000u;
    EXPECT_LT(elapsedMilliseconds, s_MaximumCaptureElapsedMS);
}

TEST(Global, RedirectedNonzeroExitAndInvalidArgumentsPreserveCapturedStreams){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/redirected_process");
    const Path<NWB::Core::Alloc::GlobalArena> outputPath = root / s_CombinedOutputFilename;
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(outputPath, AStringView("stale")));

    const Array<AStringView, 3u> argv = { s_BinSh, s_C, AStringView("printf stdout; printf stderr >&2; exit 23; printf ignored").substr(0u, 41u) };
    const auto outputPathText = PathToString<char>(testArena.arena, outputPath);
    const auto exitCode = RunProcessRedirectedToFile(testArena.arena, argv, AStringView(outputPathText));
    ASSERT_TRUE(exitCode);
    EXPECT_EQ(*exitCode, 23);
    EXPECT_FALSE(RunProcessRedirectedToFile(testArena.arena, {}, AStringView(outputPathText)));

    AString output;
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_EQ(AStringView(output.data(), output.size()), AStringView("stdoutstderr"));
    EXPECT_TRUE(RemoveAllIfExists(root));
}
#endif




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


