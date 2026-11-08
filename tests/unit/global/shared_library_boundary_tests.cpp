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


namespace __hidden_shared_library_boundary_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_PLATFORM_WINDOWS)
TEST(Global, SharedLibraryAcceptsBoundedNamesAndRejectsInvalidSymbols){
    NWB::Tests::TestArena<> testArena;
    SharedLibrary library;
    constexpr TStringView s_LibraryName = NWB_TEXT("kernel32.dll.trailing");
    ASSERT_TRUE(library.open(testArena.arena, s_LibraryName.substr(0u, 12u)));
    using CurrentProcessIdFn = DWORD(WINAPI*)();
    const auto currentProcessId = library.resolve<CurrentProcessIdFn>(testArena.arena, AStringView("GetCurrentProcessId.trailing").substr(0u, 19u));
    ASSERT_TRUE(currentProcessId);
    EXPECT_EQ((*currentProcessId)(), ::GetCurrentProcessId());

    const Array<char, 3u> invalidSymbol = { 'G', '\0', 'e' };
    const auto invalid = library.resolve<CurrentProcessIdFn>(testArena.arena, AStringView(invalidSymbol.data(), invalidSymbol.size()));
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




////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


