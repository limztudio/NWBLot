// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <tests/unit/global/name_case_types.adhd.h>

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


namespace __hidden_process_filesystem_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_PathEnvironment = "PATH";
constexpr AStringView s_SourceFilename = "source.txt";

using AString = NWB::Tests::TestAString;


struct OversizedBinarySource{
    static constexpr u8 s_Byte = 0u;


public:
    [[nodiscard]] u64 size()const noexcept{ return static_cast<u64>(Limit<StreamSize>::s_Max) + 1u; }

    [[nodiscard]] b8 empty()const noexcept{ return false; }

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
    const Array<char, 6u> invalidName = { 'P', 'A', 'T', 'H', '\0', 'X' };
    const auto invalid = ReadEnvironmentVariable(testArena.arena, AStringView(invalidName.data(), invalidName.size()));
    ASSERT_FALSE(invalid);
    EXPECT_EQ(invalid.error(), MakeErrorCode(ErrorCondition::invalid_argument));
    const auto invalidEquals = EnvironmentVariableEquals(testArena.arena, AStringView(invalidName.data(), invalidName.size()), "unused");
    ASSERT_FALSE(invalidEquals);
    EXPECT_EQ(invalidEquals.error(), invalid.error());
    const auto invalidHasValue = HasEnvironmentValue(testArena.arena, AStringView(invalidName.data(), invalidName.size()));
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
    const auto retained = ApplyDirectoryRetention(testArena.arena, root, 1u, [&](const auto&)noexcept->Expected<b8, ErrorCode>{ return MakeUnexpected(expectedError); });
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
    const Array<u8, 3u> payload = { 1u, 2u, 3u };
    EXPECT_FALSE(WriteTextFile(fullDevice, "buffered output"));
    EXPECT_FALSE(WriteBinaryFile(fullDevice, Span<const u8>(payload)));
}
#endif

TEST(Global, PathNativeComponentsMatchOwningIterationAndRemainValidAfterAdvance){
    NWB::Tests::TestArena<> testArena;
#if defined(NWB_PLATFORM_WINDOWS)
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "C:\\Root//./í•œê¸€/File.TXT/");
    constexpr Array<TStringView, 5u> s_Expected = { NWB_TEXT("C:\\"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
#else
    const Path<NWB::Core::Alloc::GlobalArena> path(testArena.arena, "/Root//./í•œê¸€/File.TXT/");
    constexpr Array<TStringView, 5u> s_Expected = { NWB_TEXT("/"), NWB_TEXT("Root"), NWB_TEXT("."), NWB_TEXT("í•œê¸€"), NWB_TEXT("File.TXT") };
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
    NWB::Core::Alloc::GlobalArena arena(Name{NWB::Tests::GlobalCases::s_GlobalCaseTestsPathNativeComponentViews});
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

TEST(Global, PathViewAccessorsAndSelfAliasingMutationsPreserveBoundariesWithoutExtraAllocations){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> emptyPath(testArena.arena);
    const Path<NWB::Core::Alloc::GlobalArena> rootSlash(testArena.arena, NWB_TEXT("///"));
    const Path<NWB::Core::Alloc::GlobalArena> hiddenPath(testArena.arena, NWB_TEXT("dir/.hidden///"));
    const Path<NWB::Core::Alloc::GlobalArena> multiDotPath(testArena.arena, NWB_TEXT("alpha/beta/archive.tar.NWB"));
    const usize baselineUsedBytes = testArena.arena.memoryStats().usedBytes;

    EXPECT_TRUE(emptyPath.parentPathView().empty());
    EXPECT_TRUE(emptyPath.filenameView().empty());
    EXPECT_TRUE(emptyPath.stemView().empty());
    EXPECT_TRUE(emptyPath.extensionView().empty());
    EXPECT_EQ(rootSlash.parentPathView(), TStringView(NWB_TEXT("/")));
    EXPECT_TRUE(rootSlash.filenameView().empty());
    EXPECT_EQ(hiddenPath.parentPathView(), TStringView(NWB_TEXT("dir")));
    EXPECT_EQ(hiddenPath.filenameView(), TStringView(NWB_TEXT(".hidden")));
    EXPECT_EQ(hiddenPath.stemView(), TStringView(NWB_TEXT(".hidden")));
    EXPECT_TRUE(hiddenPath.extensionView().empty());
    EXPECT_EQ(multiDotPath.parentPathView(), TStringView(NWB_TEXT("alpha/beta")));
    EXPECT_EQ(multiDotPath.filenameView(), TStringView(NWB_TEXT("archive.tar.NWB")));
    EXPECT_EQ(multiDotPath.stemView(), TStringView(NWB_TEXT("archive.tar")));
    EXPECT_EQ(multiDotPath.extensionView(), TStringView(NWB_TEXT(".NWB")));

    constexpr Array<AStringView, 2u> s_AllowedExtensions = { ".nwb", ".slang" };
    EXPECT_TRUE(PathHasListedExtension(multiDotPath, s_AllowedExtensions));
    EXPECT_FALSE(PathHasListedExtension(hiddenPath, s_AllowedExtensions));
    EXPECT_EQ(testArena.arena.memoryStats().usedBytes, baselineUsedBytes);

    Path<NWB::Core::Alloc::GlobalArena> aliased(
        testArena.arena,
        NWB_TEXT("alpha_segment_0123456789/beta_segment_0123456789/gamma_segment_0123456789/delta.ext")
    );
    aliased += aliased.extensionView();
    EXPECT_EQ(aliased.filenameView(), TStringView(NWB_TEXT("delta.ext.ext")));
    aliased.replaceExtension(aliased.stemView());
    EXPECT_EQ(aliased.filenameView(), TStringView(NWB_TEXT("delta.ext.delta.ext")));
    aliased /= aliased.parentPathView();
    EXPECT_EQ(aliased.filenameView(), TStringView(NWB_TEXT("gamma_segment_0123456789")));

    Path<NWB::Core::Alloc::GlobalArena> extension(testArena.arena, NWB_TEXT("name.ext"));
    extension.replaceExtension(extension.extensionView());
    EXPECT_EQ(extension.native(), TStringView(NWB_TEXT("name.ext")));
    Path<NWB::Core::Alloc::GlobalArena> filename(testArena.arena, NWB_TEXT("name.ext"));
    filename.replaceExtension(filename.filenameView());
    EXPECT_EQ(filename.native(), TStringView(NWB_TEXT("name.name.ext")));
    Path<NWB::Core::Alloc::GlobalArena> self(testArena.arena, NWB_TEXT("name.ext"));
    self.replaceExtension(self);
    EXPECT_EQ(self.native(), TStringView(NWB_TEXT("name.name.ext")));
}

TEST(Global, AreSameExistingPathAndAtomicWriteHandleHardLinksAndRejectDirectoryTargets){
    NWB::Tests::TestArena<> testArena;
    const Path<NWB::Core::Alloc::GlobalArena> root(testArena.arena, "global_test_artifacts/same_existing_and_atomic_write");
    const Path<NWB::Core::Alloc::GlobalArena> firstFile = root / "first.txt";
    const Path<NWB::Core::Alloc::GlobalArena> secondFile = root / "second.txt";
    const Path<NWB::Core::Alloc::GlobalArena> hardLinkFile = root / "first_hardlink.txt";
    const Path<NWB::Core::Alloc::GlobalArena> missingFile = root / "missing.txt";
    const Path<NWB::Core::Alloc::GlobalArena> directoryTarget = root / "dir_target";

    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(StagedFileWriter(firstFile).writeTextAtomic(AStringView("initial")));
    ASSERT_TRUE(StagedFileWriter(secondFile).writeTextAtomic(AStringView("distinct")));
    ASSERT_TRUE(EnsureDirectories(directoryTarget));

    const auto sameSelf = PathIdentity(firstFile, firstFile).sameExisting();
    const auto sameDistinct = PathIdentity(firstFile, secondFile).sameExisting();
    const auto sameMissing = PathIdentity(firstFile, missingFile).sameExisting();
    ASSERT_TRUE(sameSelf);
    ASSERT_TRUE(sameDistinct);
    ASSERT_TRUE(sameMissing);
    EXPECT_TRUE(*sameSelf);
    EXPECT_FALSE(*sameDistinct);
    EXPECT_FALSE(*sameMissing);

#if defined(NWB_PLATFORM_WINDOWS)
    if(CreateHardLink(hardLinkFile.c_str(), firstFile.c_str(), nullptr) != 0){
        const auto sameHardLink = PathIdentity(firstFile, hardLinkFile).sameExisting();
        ASSERT_TRUE(sameHardLink);
        EXPECT_TRUE(*sameHardLink);
    }
#else
    if(::link(firstFile.c_str(), hardLinkFile.c_str()) == 0){
        const auto sameHardLink = PathIdentity(firstFile, hardLinkFile).sameExisting();
        ASSERT_TRUE(sameHardLink);
        EXPECT_TRUE(*sameHardLink);
    }
#endif

    EXPECT_FALSE(StagedFileWriter(directoryTarget).writeTextAtomic(AStringView("must_fail")));
    const auto dirStillExists = IsDirectory(directoryTarget);
    ASSERT_TRUE(dirStillExists);
    EXPECT_TRUE(*dirStillExists);

    ASSERT_TRUE(StagedFileWriter(firstFile).writeTextAtomic(AStringView("updated_atomically")));
    ::AString<NWB::Core::Alloc::GlobalArena> readBack(testArena.arena);
    ASSERT_TRUE(ReadTextFile(firstFile, readBack));
    EXPECT_EQ(AStringView(readBack.data(), readBack.size()), AStringView("updated_atomically"));

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
    EXPECT_EQ(entryCount, 2u);

    EXPECT_TRUE(RemoveAllIfExists(root));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

