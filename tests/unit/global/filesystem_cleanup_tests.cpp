// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <global/filesystem/cleanup.h>
#include <global/filesystem/utility.h>
#include <global/termination.h>

#include <tests/common/test_context.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_filesystem_cleanup_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_ChildDirectory = "child";
constexpr AStringView s_RetainedDirectory = "retained";

using TestPath = Path<NWB::Core::Alloc::GlobalArena>;
using TestString = NWB::Tests::TestAString;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(GlobalFilesystemCleanup, UnwindRetiresPopulatedTreeAndRepeatedMissingCleanupSucceeds){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_unwind");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(EnsureDirectories(root / "a" / "b"));
    ASSERT_TRUE(WriteTextFile(root / "a" / "b" / "payload", "nested"));
    ASSERT_TRUE(WriteTextFile(root / ".hidden", "hidden"));
    ASSERT_TRUE(WriteTextFile(root / "sibling", "sibling"));
    static_assert(noexcept(RemoveAllForCleanup(root)));
    struct Marker final{};
    bool caught = false;
    ErrorCode cleanupError;
    try{
        ScopeExit cleanup([&]()noexcept{
            const auto removed = RemoveAllForCleanup(root);
            if(!removed)
                cleanupError = removed.error();
        });
        throw Marker{};
    }
    catch(const Marker&){
        caught = true;
    }
    EXPECT_TRUE(caught);
    EXPECT_FALSE(cleanupError);
    const auto exists = FileExists(root);
    ASSERT_TRUE(exists);
    EXPECT_FALSE(*exists);
    EXPECT_TRUE(RemoveAllForCleanup(root));
}

TEST(GlobalFilesystemCleanup, DotAndDotDotLeavesAreRejectedWithoutRemovingTheirContents){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_dot_rejection");
    const TestPath child = root / s_ChildDirectory;
    const TestPath retained = root / s_RetainedDirectory;
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(EnsureDirectories(child));
    ASSERT_TRUE(WriteTextFile(retained, "retained"));
    const auto dot = RemoveAllForCleanup(child / ".");
    const auto dotDot = RemoveAllForCleanup(child / "..");
    ASSERT_FALSE(dot);
    ASSERT_FALSE(dotDot);
    EXPECT_NE(dot.error().value(), 0);
    EXPECT_NE(dotDot.error().value(), 0);
    TestString text(arena.arena);
    ASSERT_TRUE(ReadTextFile(retained, text));
    EXPECT_EQ(text, "retained");
    EXPECT_TRUE(RemoveAllForCleanup(root));
}

TEST(GlobalFilesystemCleanup, DirectoryAndRootLinksWithTrailingSeparatorsDoNotRemoveTheirTarget){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_links");
    const TestPath tree = root / "tree";
    const TestPath outside = root / s_RetainedDirectory;
    const TestPath link = tree / "link";
    const TestPath rootLink = root / "root_link";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(EnsureDirectories(tree));
    ASSERT_TRUE(EnsureDirectories(outside));
    ASSERT_TRUE(WriteTextFile(outside / "payload", "retained"));
#if defined(NWB_PLATFORM_WINDOWS)
    const auto absoluteOutside = AbsolutePath(outside);
    ASSERT_TRUE(absoluteOutside);
    if(!CreateSymbolicLinkW(link.c_str(), absoluteOutside->c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)){
        const DWORD error = GetLastError();
        ASSERT_TRUE(RemoveAllForCleanup(root));
        if(error == ERROR_PRIVILEGE_NOT_HELD)
            GTEST_SKIP() << "Directory symlink creation requires host permission";
        FAIL() << "CreateSymbolicLinkW failed: " << error;
    }
    ASSERT_TRUE(CreateSymbolicLinkW(rootLink.c_str(), absoluteOutside->c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE));
#else
    ASSERT_EQ(::symlink("../retained", link.c_str()), 0);
    ASSERT_EQ(::symlink("retained", rootLink.c_str()), 0);
#endif
    ASSERT_TRUE(RemoveAllForCleanup(tree));
    auto trailing = PathToString(arena.arena, rootLink);
    trailing += '/';
    ASSERT_TRUE(RemoveAllForCleanup(TestPath(arena.arena, trailing)));
    TestString retained(arena.arena);
    ASSERT_TRUE(ReadTextFile(outside / "payload", retained));
    EXPECT_EQ(retained, "retained");
    EXPECT_TRUE(RemoveAllForCleanup(root));
}

#if defined(NWB_PLATFORM_WINDOWS)
TEST(GlobalFilesystemCleanup, DosDeviceDrivePathRemovesItsOwnedTree){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_device_drive");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(EnsureDirectories(root / "nested"));
    ASSERT_TRUE(WriteTextFile(root / "nested" / "payload", "owned"));
    const auto absolute = AbsolutePath(root);
    ASSERT_TRUE(absolute);
    const auto native = absolute->native();
    ASSERT_GE(native.size(), 3u);
    ASSERT_EQ(native[1u], L':');
    TestPath::string_type deviceText(L"\\\\.\\", arena.arena);
    deviceText.append(native);
    const TestPath device(arena.arena, deviceText);
    ASSERT_TRUE(RemoveAllForCleanup(device));
    const auto exists = FileExists(root);
    ASSERT_TRUE(exists);
    EXPECT_FALSE(*exists);
    EXPECT_TRUE(RemoveAllForCleanup(device));
}

TEST(GlobalFilesystemCleanup, SharingViolationIsReturnedAndCleanupCanBeRetriedAfterHandleRetirement){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_sharing_failure");
    const TestPath file = root / "held";
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(file, "retained"));
    const HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    ASSERT_NE(handle, INVALID_HANDLE_VALUE);
    ScopeExit retire([handle]()noexcept{
        if(!CloseHandle(handle))
            TerminateInvariant();
    });
    const auto removed = RemoveAllForCleanup(root);
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().value(), static_cast<i32>(ERROR_SHARING_VIOLATION));
    ASSERT_TRUE(CloseHandle(handle));
    retire.release();
    EXPECT_TRUE(RemoveAllForCleanup(root));
}
#else
TEST(GlobalFilesystemCleanup, ParentLinkLoopReturnsItsNativeErrorWithoutRemovingOtherFiles){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_parent_loop");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    ASSERT_TRUE(WriteTextFile(root / "retained", "retained"));
    ASSERT_EQ(::symlink("loop", (root / "loop").c_str()), 0);
    const auto removed = RemoveAllForCleanup(root / "loop" / s_ChildDirectory);
    ASSERT_FALSE(removed);
    EXPECT_EQ(removed.error().value(), ELOOP);
    TestString retained(arena.arena);
    ASSERT_TRUE(ReadTextFile(root / "retained", retained));
    EXPECT_EQ(retained, "retained");
    EXPECT_TRUE(RemoveAllForCleanup(root));
}

TEST(GlobalFilesystemCleanup, RootAndChildrenBeyondPathMaxUseRelativeDescriptors){
    NWB::Tests::TestArena<> arena;
    const TestPath root(arena.arena, "global_test_artifacts/cleanup_deep_path");
    ASSERT_TRUE(EnsureEmptyDirectory(root));
    int descriptor = open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    ASSERT_GE(descriptor, 0);
    ScopeExit retire([&descriptor]()noexcept{
        if(descriptor >= 0 && close(descriptor) != 0)
            TerminateInvariant();
    });
    TestPath deep = root;
    constexpr AStringView s_Component = "depth_abcdefghijklmnopqrstuvwxyz_abcdefghijklmnopqrstuvwxyz_012345";
    constexpr usize s_Depth = 80u;
    for(usize level = 0u; level < s_Depth; ++level){
        ASSERT_EQ(mkdirat(descriptor, s_Component.data(), 0700), 0);
        const int next = openat(descriptor, s_Component.data(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        ASSERT_GE(next, 0);
        const int previous = descriptor;
        descriptor = next;
        ASSERT_EQ(close(previous), 0);
        deep /= s_Component;
    }
    const int file = openat(descriptor, "payload", O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    ASSERT_GE(file, 0);
    ASSERT_EQ(close(file), 0);
    EXPECT_GT(deep.size(), static_cast<usize>(PATH_MAX));
    ASSERT_TRUE(RemoveAllForCleanup(deep));
    ASSERT_EQ(close(descriptor), 0);
    descriptor = -1;
    EXPECT_TRUE(RemoveAllForCleanup(root));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

