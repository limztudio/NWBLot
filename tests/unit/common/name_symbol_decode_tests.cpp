// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/name_symbols.h>

#include <global/name_hash_text.h>
#include <global/filesystem/executable_paths.h>
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
    Array<char, NameHashTextCodec::s_TextLength + 1u> knownToken = {};
    Array<char, NameHashTextCodec::s_TextLength + 1u> unknownToken = {};
    NameHashTextCodec::Format(knownToken.data(), sizeof(knownToken), known.identityHash());
    NameHashTextCodec::Format(unknownToken.data(), sizeof(unknownToken), ComputeNameHash(MakeNotNull("decode/unknown")));
    AString<Arena> source(arena);
    source.append(unknownToken.data()).append(" !").append(knownToken.data()).append("_ !a").append(knownToken.data()).append(" !");
    knownToken[0u] = 'g';
    source.append(knownToken.data());
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
    Array<char, NameHashTextCodec::s_TextLength + 1u> shortToken = {};
    Array<char, NameHashTextCodec::s_TextLength + 1u> longToken = {};
    Array<char, NameHashTextCodec::s_TextLength + 1u> unknownToken = {};
    NameHashTextCodec::Format(shortToken.data(), sizeof(shortToken), shortName.identityHash());
    NameHashTextCodec::Format(longToken.data(), sizeof(longToken), longName.identityHash());
    NameHashTextCodec::Format(unknownToken.data(), sizeof(unknownToken), ComputeNameHash(MakeNotNull("decode/unresolved")));
    AString<Arena> source(arena);
    source.append(shortToken.data()).append(" !").append(unknownToken.data()).append(" !").append(longToken.data()).append(" !").append(shortToken.data());
    AString<Arena> expectedSource(arena);
    expectedSource.append("decode/short !").append(unknownToken.data()).append(" !").append(expanded).append(" !decode/short");
    BasicString<CharT, Arena> text = CopyAsciiText<CharT>(arena, AStringView(source.data(), source.size()));
    const BasicString<CharT, Arena> expected = CopyAsciiText<CharT>(arena, AStringView(expectedSource.data(), expectedSource.size()));
    NameSymbols::DecodeHashTokens(arena, text);
    EXPECT_EQ(text, expected);

}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NameSymbolDecodeTests, UnknownAndBoundaryNarrowTokensDoNotAllocate){
    VerifyUnknownAndBoundaryTokens<char>();
}

#if defined(NWB_UNICODE)
TEST(NameSymbolDecodeTests, UnknownAndBoundaryWideTokensDoNotAllocate){
    VerifyUnknownAndBoundaryTokens<wchar>();
}
#endif

TEST(NameSymbolDecodeTests, ResolvesNarrowSpansWithoutChangingUnresolvedText){
    VerifyResolvedSpans<char>();
}

#if defined(NWB_UNICODE)
TEST(NameSymbolDecodeTests, ResolvesWideSpansWithoutChangingUnresolvedText){
    VerifyResolvedSpans<wchar>();
}
#endif


TEST(NameSymbolDecodeTests, EscapedDocumentRoundTripsAndMalformedRecordsStayUnpublished){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    ScopeExit cleanup([]()noexcept{
        NameSymbols::ClearRuntimeSymbols();
        NameSymbols::UninstallRuntimeRegistry();
    });
    Arena arena("Tests/NameSymbols/EscapedDocument");
    NWB::Core::Alloc::ScratchArena scratchArena(Name("Tests/NameSymbols/Scratch"));
    constexpr AStringView s_Original = "symbols/line\ncarriage\rtab\tend";
    const Name stored(s_Original);
    const NameHash invalidHash = ComputeNameHash(MakeNotNull("symbols/invalid_escape_record"));
    Array<char, NameHashTextCodec::s_TextLength + 1u> invalidToken = {};
    NameHashTextCodec::Format(invalidToken.data(), invalidToken.size(), invalidHash);

    AString<Arena> document("prefix:", arena);
    const usize prefixLength = document.size();
    ASSERT_TRUE(NameSymbols::Serialize(document));
    EXPECT_NE(document.find("symbols/line\\ncarriage\\rtab\\tend"), AString<Arena>::npos);
    document.append(invalidToken.data()).append("\truntime\tinvalid\\q\n");
    document.append(invalidToken.data()).append("\truntime\tdangling\\\n");
    NameSymbols::ClearRuntimeSymbols();
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(document.data() + prefixLength, document.size() - prefixLength), scratchArena));

    Array<char, NameSymbols::s_MaxResolvedTextLength> resolved = {};
    ASSERT_TRUE(NameSymbols::Resolve(resolved.data(), resolved.size(), stored.identityHash()));
    EXPECT_EQ(AStringView(resolved.data()), s_Original);
    EXPECT_FALSE(NameSymbols::Resolve(resolved.data(), resolved.size(), invalidHash));
}

TEST(NameSymbolDecodeTests, RejectsNoncurrentDocumentHeaderBeforePublishingSymbols){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    NWB::Core::Alloc::GlobalArena arena("Tests/NameSymbols/DocumentAdmission");
    NWB::Core::Alloc::ScratchArena scratchArena(Name("Tests/NameSymbols/Scratch"));
    const Name retained(AStringView("admission/retained"));
    const NameHash incomingHash = ComputeNameHash(MakeNotNull("admission/incoming"));
    Array<char, NameHashTextCodec::s_TextLength + 1u> incomingToken = {};
    NameHashTextCodec::Format(incomingToken.data(), sizeof(incomingToken), incomingHash);
    AString<NWB::Core::Alloc::GlobalArena> record(arena);
    StringAppendFormat(record, "{}\truntime\tAdmission/Incoming\n", incomingToken.data());
    const usize retainedCount = NameSymbols::EntryCount();
    constexpr Array<AStringView, 6u> s_RejectedHeaders = { "", "nwb_namesym_v0\tproducer=runtime\n", "nwb_namesym_v2\tproducer=runtime\n", "nwb_namesym_v10\tproducer=runtime\n", "nwb_namesym_v1_suffix\tproducer=runtime\n", "\n", };
    Array<char, NameSymbols::s_MaxResolvedTextLength> resolvedText = {};
    for(const AStringView header : s_RejectedHeaders){
        AString<NWB::Core::Alloc::GlobalArena> document(arena);
        document.append(header).append(record);
        EXPECT_FALSE(NameSymbols::LoadFromMemory(AStringView(document.data(), document.size()), scratchArena));
        EXPECT_EQ(NameSymbols::EntryCount(), retainedCount);
        EXPECT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), retained.hash()));
        EXPECT_EQ(AStringView(resolvedText.data()), "admission/retained");
        EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), incomingHash));
    }

    AString<NWB::Core::Alloc::GlobalArena> currentDocument(arena);
    currentDocument.append(NameSymbols::s_FileHeader).append("\tproducer=runtime\r\n").append(record);
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(currentDocument.data(), currentDocument.size()), scratchArena));
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), retained.hash()));
    ASSERT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), incomingHash));
    EXPECT_EQ(AStringView(resolvedText.data()), "admission/incoming");
    NameSymbols::ClearRuntimeSymbols();
}


TEST(NameSymbolDecodeTests, FileLoadSeparatesHeaderAnswerFromNativeReadFailure){
    NWB::Core::Alloc::GlobalArena arena("Tests/NameSymbols/FileAdmission");
    NWB::Core::Alloc::ScratchArena scratchArena(Name("Tests/NameSymbols/Scratch"));
    const ::Path<NWB::Core::Alloc::GlobalArena> path(arena, "name_symbol_expected_file_unit.namesym");
    ASSERT_TRUE(RemoveAllIfExists(path));
    ScopeExit cleanup([&]()noexcept{
        if(const auto removed = RemoveFile(path); !removed)
            CaptureDiagnosticEvent("name_symbol_file_admission", "Failed to remove unit name-symbol fixture", __FILE__, __LINE__);
    });

    ASSERT_TRUE(WriteTextFile(path, "nwb_namesym_v1\n"));
    const auto accepted = NameSymbols::LoadFile(path, scratchArena);
    ASSERT_TRUE(accepted);
    EXPECT_TRUE(*accepted);

    ASSERT_TRUE(WriteTextFile(path, "nwb_namesym_v2\n"));
    const auto rejectedHeader = NameSymbols::LoadFile(path, scratchArena);
    ASSERT_TRUE(rejectedHeader);
    EXPECT_FALSE(*rejectedHeader);

    const auto removed = RemoveFile(path);
    ASSERT_TRUE(removed);
    ASSERT_TRUE(*removed);
    const auto missingFileSize = FileSize(path);
    ASSERT_FALSE(missingFileSize);
    const auto missing = NameSymbols::LoadFile(path, scratchArena);
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error(), missingFileSize.error());
    EXPECT_EQ(missing.error().value(), missingFileSize.error().value());
    EXPECT_EQ(&missing.error().category(), &missingFileSize.error().category());
    EXPECT_TRUE(RemoveFile(path));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameSymbolTestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

static constexpr AStringView s_TestsNamesymbolsBeforeRegistryLive = "tests/namesymbols/before_registry_live";
static constexpr AStringView s_TestsNamesymbolsBeforeRegistryRetire = "tests/namesymbols/before_registry_retired";
static constexpr AStringView s_GlobalArenaMemoryHeapBacking = "global/arena_memory/heap_backing";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NameSymbolDecodeTests, NameSymbolsCollectArenaOwnersWithoutPerformanceCapture){
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

    Array<char, 128u> resolvedText = {};
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_LiveHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_RetiredHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_HeapHash));

    ::AString<NWB::Core::Alloc::GlobalArena> namesymText(liveOwner);
    ASSERT_TRUE(NameSymbols::Serialize(namesymText));
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_LiveHash));
    EXPECT_STREQ(resolvedText.data(), s_TestsNamesymbolsBeforeRegistryLive.data());
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_RetiredHash));
    EXPECT_STREQ(resolvedText.data(), s_TestsNamesymbolsBeforeRegistryRetire.data());
    EXPECT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_HeapHash));
    EXPECT_STREQ(resolvedText.data(), s_GlobalArenaMemoryHeapBacking.data());
    EXPECT_NE(namesymText.find(s_TestsNamesymbolsBeforeRegistryLive), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_TestsNamesymbolsBeforeRegistryRetire), decltype(namesymText)::npos);
    EXPECT_NE(namesymText.find(s_GlobalArenaMemoryHeapBacking), decltype(namesymText)::npos);
#else
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_LiveHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_RetiredHash));
    EXPECT_FALSE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_HeapHash));
#endif

    NameSymbols::ClearRuntimeSymbols();
    const auto executableDirectory = ExecutablePaths(liveOwner).directory();
    const auto executableName = ExecutablePaths(liveOwner).name();
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
    NameSymbols::ClearRuntimeSymbols();
    const auto loadedDefault = NameSymbols::LoadDefaultFile();
    EXPECT_TRUE(loadedDefault ? *loadedDefault : loadedDefault.error().loadedAny);
#if defined(NWB_BUILD_SYMBOLS)
    ASSERT_TRUE(NameSymbols::Resolve(resolvedText.data(), sizeof(resolvedText), s_LiveHash));
    EXPECT_STREQ(resolvedText.data(), s_TestsNamesymbolsBeforeRegistryLive.data());
#endif
    EXPECT_TRUE(RemoveFile(namesymPath));
}

TEST(NameSymbolDecodeTests, ConcurrentRecordResolveClearAndUninstallStaySynchronized){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    ScopeExit restoreRegistry([]()noexcept{
        NameSymbols::InstallRuntimeRegistry();
        NameSymbols::ClearRuntimeSymbols();
    });

    constexpr AStringView s_TargetText = "concurrent/symbol_entry";
    const NameHash targetHash = ComputeNameHash(MakeNotNull("concurrent/symbol_entry"));
    Atomic<b8> stop{ false };
    Latch startLatch(4);

    JoiningThread recorderThread([&](){
        startLatch.arrive_and_wait();
        for(u32 i = 0u; i < 400u; ++i){
            const Name recorded(s_TargetText);
            EXPECT_EQ(recorded.identityHash(), targetHash);
        }
        stop.store(true, MemoryOrder::release);
    });

    JoiningThread resolverThread([&](){
        startLatch.arrive_and_wait();
        Array<char, NameSymbols::s_MaxResolvedTextLength> buffer = {};
        while(!stop.load(MemoryOrder::acquire)){
            if(NameSymbols::Resolve(buffer.data(), buffer.size(), targetHash))
                EXPECT_EQ(AStringView(buffer.data()), s_TargetText);
        }
    });

    JoiningThread clearThread([&](){
        startLatch.arrive_and_wait();
        while(!stop.load(MemoryOrder::acquire)){
            NameSymbols::ClearRuntimeSymbols();
            const usize count = NameSymbols::EntryCount();
            EXPECT_LE(count, 64u);
        }
    });

    JoiningThread toggleThread([&](){
        startLatch.arrive_and_wait();
        while(!stop.load(MemoryOrder::acquire)){
            NameSymbols::UninstallRuntimeRegistry();
            NameSymbols::InstallRuntimeRegistry();
        }
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

