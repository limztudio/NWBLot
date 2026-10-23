// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/name_symbols.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>
#include <global/filesystem/utility.h>
#include <global/scope_exit.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name_symbol_decode_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameSymbols = NWB::Core::Common::NameSymbols;
using Arena = NWB::Core::Alloc::GlobalArena;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] static BasicString<CharT, Arena> CopyAsciiText(Arena& arena, const AStringView source){
    BasicString<CharT, Arena> text(arena);
    text.reserve(source.size());
    for(const char ch : source)
        text.push_back(static_cast<CharT>(ch));
    return text;
}


template<typename CharT>
static void VerifyUnknownAndBoundaryTokens(){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    Arena arena("Tests/NameSymbols/Boundaries");
    const Name known(AStringView("decode/known"));
    char knownToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    char unknownToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(knownToken, sizeof(knownToken), known.identityHash());
    NameDetail::HashToDebugString(unknownToken, sizeof(unknownToken), ComputeNameHash(MakeNotNull("decode/unknown")));
    AString<Arena> source(arena);
    source.append(unknownToken).append(" !").append(knownToken).append("_ !a").append(knownToken).append(" !");
    knownToken[0u] = 'g';
    source.append(knownToken);
    const BasicString<CharT, Arena> expected = CopyAsciiText<CharT>(arena, AStringView(source.data(), source.size()));
    BasicString<CharT, Arena> text(expected);
    const CharT* const originalData = text.data();
    const ArenaMemoryStats before = arena.memoryStats();
    NameSymbols::DecodeHashTokens(arena, text);
    const ArenaMemoryStats after = arena.memoryStats();

    EXPECT_EQ(text, expected);
    EXPECT_EQ(text.data(), originalData);
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
}

template<typename CharT>
static void VerifyResolvedSpans(){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    Arena arena("Tests/NameSymbols/Resolved");
    AString<Arena> expanded(arena);
    expanded.assign(300u, 'z');
    const Name shortName(AStringView("decode/short"));
    const auto longNameResult = Name::FromText(AStringView(expanded.data(), expanded.size()));
    ASSERT_TRUE(longNameResult);
    const Name longName = *longNameResult;
    char shortToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    char longToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    char unknownToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(shortToken, sizeof(shortToken), shortName.identityHash());
    NameDetail::HashToDebugString(longToken, sizeof(longToken), longName.identityHash());
    NameDetail::HashToDebugString(unknownToken, sizeof(unknownToken), ComputeNameHash(MakeNotNull("decode/unresolved")));
    AString<Arena> source(arena);
    source.append(shortToken).append(" !").append(unknownToken).append(" !").append(longToken).append(" !").append(shortToken);
    AString<Arena> expectedSource(arena);
    expectedSource.append("decode/short !").append(unknownToken).append(" !").append(expanded).append(" !decode/short");
    BasicString<CharT, Arena> text = CopyAsciiText<CharT>(arena, AStringView(source.data(), source.size()));
    const BasicString<CharT, Arena> expected = CopyAsciiText<CharT>(arena, AStringView(expectedSource.data(), expectedSource.size()));
    NameSymbols::DecodeHashTokens(arena, text);
    EXPECT_EQ(text, expected);

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NameSymbolDecodeTests, UnknownAndBoundaryNarrowTokensDoNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnknownAndBoundaryTokens<char>();
}

#if defined(NWB_UNICODE)
TEST(NameSymbolDecodeTests, UnknownAndBoundaryWideTokensDoNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnknownAndBoundaryTokens<wchar>();
}
#endif

TEST(NameSymbolDecodeTests, ResolvesNarrowSpansWithoutChangingUnresolvedText){
    __hidden_name_symbol_decode_tests::VerifyResolvedSpans<char>();
}

#if defined(NWB_UNICODE)
TEST(NameSymbolDecodeTests, ResolvesWideSpansWithoutChangingUnresolvedText){
    __hidden_name_symbol_decode_tests::VerifyResolvedSpans<wchar>();
}
#endif


TEST(NameSymbolDecodeTests, RejectsNoncurrentDocumentHeaderBeforePublishingSymbols){
    namespace NameSymbols = NWB::Core::Common::NameSymbols;
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    NWB::Core::Alloc::GlobalArena arena("Tests/NameSymbols/DocumentAdmission");
    const Name retained(AStringView("admission/retained"));
    const NameHash incomingHash = ComputeNameHash(MakeNotNull("admission/incoming"));
    char incomingToken[::NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(incomingToken, sizeof(incomingToken), incomingHash);
    AString<NWB::Core::Alloc::GlobalArena> record(arena);
    StringAppendFormat(record, "{}\truntime\tAdmission/Incoming\n", incomingToken);
    const usize retainedCount = NameSymbols::EntryCount();
    constexpr Array<AStringView, 6u> s_RejectedHeaders = { "", "nwb_namesym_v0\tproducer=runtime\n", "nwb_namesym_v2\tproducer=runtime\n", "nwb_namesym_v10\tproducer=runtime\n", "nwb_namesym_v1_suffix\tproducer=runtime\n", "\n", };
    char resolvedText[NameSymbols::s_MaxResolvedTextLength] = {};
    for(const AStringView header : s_RejectedHeaders){
        AString<NWB::Core::Alloc::GlobalArena> document(arena);
        document.append(header).append(record);
        EXPECT_FALSE(NameSymbols::LoadFromMemory(AStringView(document.data(), document.size())));
        EXPECT_EQ(NameSymbols::EntryCount(), retainedCount);
        EXPECT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), retained.hash()));
        EXPECT_EQ(AStringView(resolvedText), "admission/retained");
        EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), incomingHash));
    }

    AString<NWB::Core::Alloc::GlobalArena> currentDocument(arena);
    currentDocument.append(NameSymbols::s_FileHeader).append("\tproducer=runtime\r\n").append(record);
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(currentDocument.data(), currentDocument.size())));
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), retained.hash()));
    ASSERT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), incomingHash));
    EXPECT_EQ(AStringView(resolvedText), "admission/incoming");
    NameSymbols::ClearRuntimeSymbols();
}


TEST(NameSymbolDecodeTests, FileLoadSeparatesHeaderAnswerFromNativeReadFailure){
    namespace NameSymbols = NWB::Core::Common::NameSymbols;
    NWB::Core::Alloc::GlobalArena arena("Tests/NameSymbols/FileAdmission");
    const ::Path<NWB::Core::Alloc::GlobalArena> path(arena, "name_symbol_expected_file_unit.namesym");
    ASSERT_TRUE(RemoveAllIfExists(path));
    ScopeExit cleanup([&]()noexcept{
        if(const auto removed = RemoveFile(path); !removed)
            CaptureDiagnosticEvent("name_symbol_file_admission", "Failed to remove unit name-symbol fixture", __FILE__, __LINE__);
    });

    ASSERT_TRUE(WriteTextFile(path, "nwb_namesym_v1\n"));
    const auto accepted = NameSymbols::LoadFile(path);
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(*accepted);

    ASSERT_TRUE(WriteTextFile(path, "nwb_namesym_v2\n"));
    const auto rejectedHeader = NameSymbols::LoadFile(path);
    ASSERT_TRUE(rejectedHeader);
    EXPECT_FALSE(*rejectedHeader);

    const auto removed = RemoveFile(path);
    ASSERT_TRUE(removed);
    ASSERT_TRUE(*removed);
    const auto missingFileSize = FileSize(path);
    ASSERT_FALSE(missingFileSize);
    const auto missing = NameSymbols::LoadFile(path);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), missingFileSize.error());
    EXPECT_EQ(missing.error().value(), missingFileSize.error().value());
    EXPECT_EQ(&missing.error().category(), &missingFileSize.error().category());
    EXPECT_TRUE(RemoveFile(path));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name_symbol_decode_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_TestsNamesymbolsBeforeRegistryLive = "tests/namesymbols/before_registry_live";
static constexpr AStringView s_TestsNamesymbolsBeforeRegistryRetire = "tests/namesymbols/before_registry_retired";
static constexpr AStringView s_GlobalArenaMemoryHeapBacking = "global/arena_memory/heap_backing";
using NameSymbolTestPath = ::Path<NWB::Core::Alloc::GlobalArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NameSymbolDecodeTests, NameSymbolsCollectArenaOwnersWithoutPerformanceCapture){
    namespace NameSymbols = NWB::Core::Common::NameSymbols;
    constexpr Name s_LiveOwner{"Tests/NameSymbols/Before_Registry_Live"};
    constexpr Name s_RetiredOwner{"Tests/NameSymbols/Before_Registry_Retired"};
    constexpr NameHash s_LiveHash = ComputeNameHash(MakeNotNull("tests/namesymbols/before_registry_live"));
    constexpr NameHash s_RetiredHash = ComputeNameHash(MakeNotNull("tests/namesymbols/before_registry_retired"));
    constexpr NameHash s_HeapHash = ComputeNameHash(MakeNotNull("global/arena_memory/heap_backing"));
    NameSymbols::UninstallRuntimeRegistry();
    NWB::Core::Alloc::GlobalArena liveOwner(s_LiveOwner);
    {
        NWB::Core::Alloc::GlobalArena retiredOwner(s_RetiredOwner);
    }
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();

    char resolvedText[128] = {};
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_LiveHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_RetiredHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_HeapHash));

    ::AString<NWB::Core::Alloc::GlobalArena> namesymText(liveOwner);
    ASSERT_TRUE(NameSymbols::Serialize(namesymText));
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_LiveHash));
    EXPECT_STREQ(resolvedText, s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE.data());
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_RetiredHash));
    EXPECT_STREQ(resolvedText, s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE.data());
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_HeapHash));
    EXPECT_STREQ(resolvedText, s_GLOBAL_ARENA_MEMORY_HEAP_BACKING.data());
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_LIVE), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_TESTS_NAMESYMBOLS_BEFORE_REGISTRY_RETIRE), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_GLOBAL_ARENA_MEMORY_HEAP_BACKING), decltype(namesymText)::npos);
#else
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_LiveHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_RetiredHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText, sizeof(resolvedText), s_HeapHash));
#endif

    NameSymbols::ClearRuntimeSymbols();
    const auto executableDirectory = GetExecutableDirectory(liveOwner);
    const auto executableName = GetExecutableName(liveOwner);
    ASSERT_TRUE(executableDirectory);
    ASSERT_TRUE(executableName);
    NameSymbolTestPath namesymPath = *executableDirectory / *executableName;
    namesymPath.replaceExtension(NWB_TEXT(".namesym"));
    // The application exception path exports after its scoped runtime callbacks have already detached.
    NameSymbols::UninstallRuntimeRegistry();
    ASSERT_TRUE(NameSymbols::WriteDefaultFile());
    NameSymbols::InstallRuntimeRegistry();
    namesymText.clear();
    ASSERT_TRUE(ReadTextFile(namesymPath, namesymText));
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_NE(namesymText.find(s_TestsNamesymbolsBeforeRegistryLive), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_TestsNamesymbolsBeforeRegistryRetire), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_GlobalArenaMemoryHeapBacking), decltype(namesymText)::npos);
#else
    EXPECT_EQ(namesymText.find(s_TestsNamesymbolsBeforeRegistryLive), decltype(namesymText)::npos);
    EXPECT_EQ(namesymText.find(s_TestsNamesymbolsBeforeRegistryRetire), decltype(namesymText)::npos);
    EXPECT_EQ(namesymText.find(s_GlobalArenaMemoryHeapBacking), decltype(namesymText)::npos);
#endif
    EXPECT_TRUE(RemoveFile(namesymPath));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

