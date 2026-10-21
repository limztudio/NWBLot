// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <global/environment.h>
#include <global/filesystem/retention.h>
#include <global/filesystem/directory_iterator.h>
#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/limit.h>
#include <global/process_execution.h>
#include <global/shared_library.h>
#include <global/text_utils.h>

#if defined(NWB_PLATFORM_LINUX)
#include <unistd.h>
#include <sys/wait.h>
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_process_filesystem_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_PathEnvironment = "PATH";
constexpr AStringView s_CombinedOutputFilename = "combined output.txt";
constexpr AStringView s_SourceFilename = "source.txt";

using AString = NWB::Tests::TestAString;
#if defined(NWB_PLATFORM_LINUX)
constexpr u32 s_ExpectedDualCount = 2u;
#endif
#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
static constexpr AStringView s_BIN_SH = "/bin/sh";
static constexpr AStringView s_C = "-c";
#endif
#if defined(NWB_PLATFORM_WINDOWS)
static constexpr AStringView s_TRAILING_SUFFIX = ".trailing";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


struct OversizedBinarySource{
    static constexpr u8 s_Byte = 0u;


public:
    [[nodiscard]] u64 size()const noexcept{ return static_cast<u64>(Limit<StreamSize>::s_Max) + 1u; }

    [[nodiscard]] bool empty()const noexcept{ return false; }

    [[nodiscard]] const u8* data()const noexcept{ return &s_Byte; }
};

TEST(Global, EnvironmentVariableNamesSupportSlicesAndRejectEmbeddedNull){
    NWB::Tests::TestArena<> testArena;
    const auto expected = ReadEnvironmentVariable(testArena.arena, AStringView(s_PathEnvironment));
    const auto actual = ReadEnvironmentVariable(testArena.arena, AStringView("PATH.trailing").substr(0u, 4u));
    ASSERT_TRUE(expected);
    ASSERT_TRUE(actual);
    ASSERT_TRUE(*expected);
    EXPECT_EQ(*actual, *expected);
    auto different = **expected;
    different += 'x';
    const auto matches = EnvironmentVariableEquals(testArena.arena, s_PathEnvironment, AStringView(**expected));
    ASSERT_TRUE(matches);
    EXPECT_TRUE(*matches);
    const auto differs = EnvironmentVariableEquals(testArena.arena, s_PathEnvironment, AStringView(different));
    ASSERT_TRUE(differs);
    EXPECT_FALSE(*differs);
    const auto hasValue = HasEnvironmentValue(testArena.arena, s_PathEnvironment);
    ASSERT_TRUE(hasValue);
    EXPECT_EQ(*hasValue, !(**expected).empty());
    const char invalidName[]{ 'P', 'A', 'T', 'H', '\0', 'X' };
    const auto invalid = ReadEnvironmentVariable(testArena.arena, AStringView(invalidName, LengthOf(invalidName)));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), MakeErrorCode(ErrorCondition::invalid_argument));
    const auto invalidEquals = EnvironmentVariableEquals(testArena.arena, AStringView(invalidName, LengthOf(invalidName)), "unused");
    ASSERT_FALSE(invalidEquals);
    EXPECT_EQ(invalidEquals.error(), invalid.error());
    const auto invalidHasValue = HasEnvironmentValue(testArena.arena, AStringView(invalidName, LengthOf(invalidName)));
    ASSERT_FALSE(invalidHasValue);
    EXPECT_EQ(invalidHasValue.error(), invalid.error());
}

TEST(Global, DirectoryRetentionPreservesPredicateErrorAndRetainsUnadmittedEntries){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/retention_predicate_error");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(root / "a", "retained"));
    ASSERT_TRUE(WriteTextFile(root / "b", "retained"));
    const ErrorCode expectedError = MakeErrorCode(ErrorCondition::permission_denied);
    const auto retained = ApplyDirectoryRetention(testArena.arena, root, 1u, [&](const auto&)noexcept->Expected<bool, ErrorCode>{ return MakeUnexpected(expectedError); });
    ASSERT_FALSE(retained);
    EXPECT_EQ(retained.error(), expectedError);
    const auto first = FileExists(root / "a");
    const auto second = FileExists(root / "b");
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_TRUE(*first);
    EXPECT_TRUE(*second);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(Global, FileWriteSizeRejectionPreservesExistingOutput){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/oversized_file_write");
    const auto outputPath = root / "output.bin";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(outputPath, "retained"));

    EXPECT_FALSE(WriteBinaryFile(outputPath, OversizedBinarySource{}));
    AString retained(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, retained));
    EXPECT_EQ(retained, "retained");
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(Global, DirectoryCreationRejectsExistingFileWithoutChangingIt){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/directory_creation_file_collision");
    const auto blocker = root / "existing.txt";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(blocker, AStringView("retained")));

    const auto unchangedDirectory = CreateDirectories(root);
    ASSERT_TRUE(unchangedDirectory);
    EXPECT_FALSE(*unchangedDirectory);
    const auto created = CreateDirectories(blocker);
    EXPECT_FALSE(created);
    if(!created)
        EXPECT_NE(created.error().value(), 0);
    EXPECT_FALSE(EnsureDirectories(blocker));
    AString retained(testArena.arena);
    ASSERT_TRUE(ReadTextFile(blocker, retained));
    EXPECT_EQ(retained, "retained");
    EXPECT_TRUE(RemoveAllIfExists(root));
}

#if defined(NWB_PLATFORM_LINUX) && !defined(NWB_PLATFORM_ANDROID)
TEST(Global, FileWritesRejectBufferedDeviceFailure){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> fullDevice(testArena.arena, "/dev/full");
    const u8 payload[]{ 1u, 2u, 3u };
    EXPECT_FALSE(WriteTextFile(fullDevice, "buffered output"));
    EXPECT_FALSE(WriteBinaryFile(fullDevice, Span<const u8>(payload)));
}
#endif

TEST(Global, ProcessArgumentValidationPreservesExistingOutput){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/invalid_process_arguments");
    const auto outputPath = root / "output.txt";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(outputPath, AStringView("retained")));
    const auto outputPathText = PathToString<char>(testArena.arena, outputPath);

    const char invalidArgument[]{ 'a', '\0', 'b' };
    const AStringView arguments[]{ "unused_program", AStringView(invalidArgument, LengthOf(invalidArgument)) };
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
    ::AString<NWB::Core::Alloc::GlobalArena> searchPathText = PathToString<char>(testArena.arena, executablePath.parentPath());
    ::AString<NWB::Core::Alloc::GlobalArena> executableNameText = PathToString<char>(testArena.arena, executablePath.filename());
    const usize searchPathLength = searchPathText.size();
    const usize executableNameLength = executableNameText.size();
    searchPathText += s_TRAILING_SUFFIX;
    executableNameText += s_TRAILING_SUFFIX;
    const auto executableAvailable = ExecutableAvailableInPath(testArena.arena, AStringView(searchPathText).substr(0u, searchPathLength), AStringView(executableNameText).substr(0u, executableNameLength));
    ASSERT_TRUE(executableAvailable);
    EXPECT_TRUE(*executableAvailable);

    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/bounded_process");
    const auto outputPath = root / s_CombinedOutputFilename;
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ::AString<NWB::Core::Alloc::GlobalArena> outputPathText = PathToString<char>(testArena.arena, outputPath);
    const usize outputPathLength = outputPathText.size();
    const usize executableLength = executableText.size();
    outputPathText += s_TRAILING_SUFFIX;
    executableText += s_TRAILING_SUFFIX;
    const AStringView arguments[]{ AStringView(executableText).substr(0u, executableLength), "/c", "echo stdout&echo stderr 1>&2&exit /b 23" };
    const auto exitCode = RunProcessRedirectedToFile(testArena.arena, arguments, AStringView(outputPathText).substr(0u, outputPathLength));
    ASSERT_TRUE(exitCode);
    EXPECT_EQ(*exitCode, 23);
    ::AString<NWB::Core::Alloc::GlobalArena> output(testArena.arena);
    ASSERT_TRUE(ReadTextFile(outputPath, output));
    EXPECT_NE(AStringView(output).find("stdout"), AStringView::npos);
    EXPECT_NE(AStringView(output).find("stderr"), AStringView::npos);
    EXPECT_TRUE(RemoveAllIfExists(root));
}

TEST(Global, SharedLibraryAcceptsBoundedNamesAndRejectsInvalidSymbols){
    NWB::Tests::TestArena<> testArena;
    SharedLibrary library;
    constexpr TStringView s_LibraryName = NWB_TEXT("kernel32.dll.trailing");
    ASSERT_TRUE(library.open(testArena.arena, s_LibraryName.substr(0u, 12u)));
    using CurrentProcessIdFn = DWORD(WINAPI*)();
    const auto currentProcessId = library.resolve<CurrentProcessIdFn>(testArena.arena, AStringView("GetCurrentProcessId.trailing").substr(0u, 19u));
    ASSERT_TRUE(currentProcessId);
    EXPECT_EQ((*currentProcessId)(), ::GetCurrentProcessId());

    const char invalidSymbol[]{ 'G', '\0', 'e' };
    const auto invalid = library.resolve<CurrentProcessIdFn>(testArena.arena, AStringView(invalidSymbol, LengthOf(invalidSymbol)));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, MakeErrorCode(ErrorCondition::invalid_argument));
    const auto missing = library.resolve<CurrentProcessIdFn>(testArena.arena, "nwb_missing_symbol_bec2119d19ec4c48");
    ASSERT_FALSE(missing);
    EXPECT_NE(missing.error().code.value(), 0);
    EXPECT_TRUE(library.isOpen());
}
#endif

TEST(Global, SharedLibraryInvalidNamePreservesNativeFailureStatusWithoutOpening){
    NWB::Tests::TestArena<> testArena;
    SharedLibrary library;
    const auto invalid = library.open(testArena.arena, TStringView{});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().code, MakeErrorCode(ErrorCondition::invalid_argument));
    EXPECT_FALSE(library.isOpen());
    const auto missing = library.open(testArena.arena, NWB_TEXT("nwb_contract_missing_library_6ee251a68fe246b0"));
    ASSERT_FALSE(missing);
    EXPECT_NE(missing.error().code.value(), 0);
#if !defined(NWB_PLATFORM_WINDOWS)
    EXPECT_FALSE(missing.error().detail.view().empty());
#endif
    EXPECT_FALSE(library.isOpen());
}

TEST(Global, PathNativeComponentsMatchOwningIterationAndRemainValidAfterAdvance){
    NWB::Tests::TestArena<> testArena;
#if defined(NWB_PLATFORM_WINDOWS)
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "C:\\Root//./í•œê¸€/File.TXT/");
    constexpr TStringView s_Expected[]{ NWB_TEXT("C:\\"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
#else
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "/Root//./í•œê¸€/File.TXT/");
    constexpr TStringView s_Expected[]{ NWB_TEXT("/"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
#endif
    auto componentIt = path.begin();
    const TStringView first = componentIt.nativeComponent();
    usize componentIndex = 0u;
    while(componentIt != path.end()){
        ASSERT_LT(componentIndex, LengthOf(s_Expected));
        const TStringView borrowed = componentIt.nativeComponent();
        EXPECT_EQ(borrowed, s_Expected[componentIndex]);
        const auto owned = *componentIt;
        EXPECT_EQ(owned.native(), borrowed);
        auto next = componentIt;
        ++next;
        EXPECT_EQ(componentIt.nativeComponent(), borrowed);
        componentIt = next;
        EXPECT_EQ(borrowed, s_Expected[componentIndex]);
        EXPECT_EQ(first, s_Expected[0u]);
        ++componentIndex;
    }
    EXPECT_EQ(componentIndex, LengthOf(s_Expected));
    ++componentIt;
    EXPECT_EQ(componentIt, path.end());

    const Path<NWB::Core::Alloc::GlobalArena> empty(testArena.arena);
    EXPECT_EQ(empty.begin(), empty.end());
}

TEST(Global, PathNativeComponentViewsBorrowSourceStorageWithoutAllocating){
    NWB::Core::Alloc::GlobalArena arena(Name{"tests/path/native_component_views"});
    const Path<NWB::Core::Alloc::GlobalArena> path(arena, "First_component_long_enough_to_require_owning_storage/Second_component_with_Unicode_í•œê¸€_ðŸ˜€/Third_component");
    const TStringView native = path.native();
    const ArenaMemoryStats before = arena.memoryStats();
    TStringView retained;
    {
        auto componentIt = path.begin();
        retained = componentIt.nativeComponent();
        EXPECT_EQ(retained.data(), native.data());
        for(usize repetition = 0u; repetition < 32u; ++repetition){
            usize componentCount = 0u;
            for(auto it = path.begin(); it != path.end(); ++it){
                const TStringView component = it.nativeComponent();
                EXPECT_GE(component.data(), native.data());
                EXPECT_LE(component.data() + component.size(), native.data() + native.size());
                ++componentCount;
            }
            EXPECT_EQ(componentCount, 3u);
        }
        ++componentIt;
        EXPECT_NE(componentIt.nativeComponent().data(), retained.data());
    }
    EXPECT_EQ(retained, TStringView(NWB_TEXT("First_component_long_enough_to_require_owning_storage")));
    const ArenaMemoryStats after = arena.memoryStats();
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
    EXPECT_EQ(after.usedBytes, before.usedBytes);
}


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

    [[nodiscard]] bool empty()const noexcept{ return byteCount == 0u; }

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
    const auto invalid = CaptureProcessOutput(testArena.arena, output, {});
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error().kind, ProcessCaptureFailureKind::InvalidArguments);
    EXPECT_EQ(invalid.error().nativeError, ErrorCondition::invalid_argument);
    const AStringView emptyArguments[]{ s_BIN_SH, s_C, "exit 0" };
    ASSERT_TRUE(CaptureProcessOutput(testArena.arena, output, emptyArguments));
    EXPECT_TRUE(output.empty());
    const AStringView failedArguments[]{ s_BIN_SH, s_C, "printf retained; exit 23" };
    const auto failed = CaptureProcessOutput(testArena.arena, output, failedArguments);
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().kind, ProcessCaptureFailureKind::ChildExitedUnsuccessfully);
    EXPECT_EQ(failed.error().exitCode, 23);
    EXPECT_EQ(AStringView(output), "retained");
}

TEST(Global, CaptureProcessOutputAppendFailureClosesPipeAndReapsChildBeforeUnwinding){
    const AStringView argv[] = { s_BIN_SH, s_C, "printf '%s\\n' \"$$\"; exec 1>&-; while :; do :; done" };
    NWB::Tests::TestArena<> testArena;
    ThrowingCaptureOutput output;
    constexpr u32 s_CaptureTimeoutMS = 250u;
    EXPECT_THROW(EXPECT_FALSE(CaptureProcessOutput(testArena.arena, output, argv, 64u, 64u, s_CaptureTimeoutMS)), RuntimeException);
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
    const AStringView argv[] = { s_BIN_SH, s_C, "printf '%s\\nxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx' \"$$\"; exec 1>&-; while :; do :; done" };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    constexpr u32 s_CaptureTimeoutMS = 250u;
    const auto captured = CaptureProcessOutput(testArena.arena, output, argv, 16u, 64u, s_CaptureTimeoutMS);
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
    const AStringView argv[] = { s_BIN_SH, s_C, "while :; do printf x; done" };
    NWB::Tests::TestArena<> testArena;
    AString output(testArena.arena);
    const auto startMilliseconds = ProcessExecutionDetail::MonotonicMilliseconds();
    ASSERT_TRUE(startMilliseconds);
    constexpr u32 s_ContinuousOutputTimeoutMS = 100u;
    const auto captured = CaptureProcessOutput(testArena.arena, output, argv, 64u, 64u, s_ContinuousOutputTimeoutMS);
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

    const AStringView argv[] = { s_BIN_SH, s_C, AStringView("printf stdout; printf stderr >&2; exit 23; printf ignored").substr(0u, 41u) };
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

TEST(Global, MovePathReplacesExistingDestinationWithoutRetainingSource){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/move_path_to_directory");
    const Path<NWB::Core::Alloc::GlobalArena> source = root / s_SourceFilename;
    const Path<NWB::Core::Alloc::GlobalArena> destinationDirectory = root / "moved";
    const Path<NWB::Core::Alloc::GlobalArena> destination = destinationDirectory / s_SourceFilename;

    EXPECT_TRUE(EnsureEmptyDirectory(root));
    EXPECT_TRUE(WriteTextFile(source, AStringView("fresh")));
    EXPECT_TRUE(EnsureDirectories(destinationDirectory));
    EXPECT_TRUE(WriteTextFile(destination, AStringView("old")));

    const auto movedPath = MovePathToDirectory(source, destinationDirectory);
    ASSERT_TRUE(movedPath);
    EXPECT_EQ(*movedPath, destination);

    BasicString<char, NWB::Core::Alloc::GlobalArena> movedText{testArena.arena};
    EXPECT_TRUE(ReadTextFile(destination, movedText));
    EXPECT_EQ(AStringView(movedText.data(), movedText.size()), AStringView("fresh"));
    const auto sourceExists = FileExists(source);
    ASSERT_TRUE(sourceExists);
    EXPECT_FALSE(*sourceExists);

    EXPECT_TRUE(RemoveAllIfExists(root));
}

#if defined(NWB_PLATFORM_LINUX)
TEST(Global, RecursiveDirectoryIteratorDoesNotFollowDirectorySymlinks){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/recursive_directory_iterator_links");
    const Path<NWB::Core::Alloc::GlobalArena> regularFile = root / "regular.txt";
    const Path<NWB::Core::Alloc::GlobalArena> directoryLink = root / "self";

    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(regularFile, AStringView("regular")));
    ASSERT_EQ(::symlink(".", directoryLink.c_str()), 0);

    const auto followed = IsDirectory(directoryLink);
    const auto noFollow = IsDirectoryNoFollow(directoryLink);
    ASSERT_TRUE(followed);
    ASSERT_TRUE(noFollow);
    EXPECT_TRUE(*followed);
    EXPECT_FALSE(*noFollow);

    const auto directory = RecursiveDirectoryIterator<NWB::Core::Alloc::GlobalArena>::Create(root);
    ASSERT_TRUE(directory);

    usize entryCount = 0u;
    for(const auto& entry : *directory){
        EXPECT_FALSE(entry.path().empty());
        ++entryCount;
    }
    EXPECT_EQ(entryCount, s_ExpectedDualCount);

    EXPECT_TRUE(RemoveAllIfExists(root));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

