// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/name_symbols.h>

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
static void VerifyUnchangedText(){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    Arena arena("Tests/NameSymbols/Unchanged");
    BasicString<CharT, Arena> text(arena);
    text.assign(4096u, static_cast<CharT>('z'));
    const CharT* const originalData = text.data();
    const ArenaMemoryStats before = arena.memoryStats();
    for(u32 iteration = 0u; iteration < 32u; ++iteration)
        NameSymbols::DecodeHashTokens(arena, text);
    const ArenaMemoryStats after = arena.memoryStats();

    EXPECT_EQ(text.size(), 4096u);
    EXPECT_EQ(text.data(), originalData);
    for(const CharT ch : text)
        EXPECT_EQ(ch, static_cast<CharT>('z'));
    EXPECT_EQ(after.allocationCount, before.allocationCount);
    EXPECT_EQ(after.reallocationCount, before.reallocationCount);
    EXPECT_EQ(after.deallocationCount, before.deallocationCount);
}

template<typename CharT>
static void VerifyUnknownAndBoundaryTokens(){
    NameSymbols::InstallRuntimeRegistry();
    NameSymbols::ClearRuntimeSymbols();
    Arena arena("Tests/NameSymbols/Boundaries");
    const Name known(AStringView("decode/known"));
    char knownToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    char unknownToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(known.identityHash(), knownToken, sizeof(knownToken));
    NameDetail::HashToDebugString(ComputeNameHash("decode/unknown"), unknownToken, sizeof(unknownToken));
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
    const Name longName(AStringView(expanded.data(), expanded.size()));
    char shortToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    char longToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    char unknownToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(shortName.identityHash(), shortToken, sizeof(shortToken));
    NameDetail::HashToDebugString(longName.identityHash(), longToken, sizeof(longToken));
    NameDetail::HashToDebugString(ComputeNameHash("decode/unresolved"), unknownToken, sizeof(unknownToken));
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


TEST(NameSymbolDecodeTests, PlainNarrowTextDoesNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnchangedText<char>();
}

#if defined(GLB_UNICODE)
TEST(NameSymbolDecodeTests, PlainWideTextDoesNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnchangedText<wchar>();
}
#endif

TEST(NameSymbolDecodeTests, UnknownAndBoundaryNarrowTokensDoNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnknownAndBoundaryTokens<char>();
}

#if defined(GLB_UNICODE)
TEST(NameSymbolDecodeTests, UnknownAndBoundaryWideTokensDoNotAllocate){
    __hidden_name_symbol_decode_tests::VerifyUnknownAndBoundaryTokens<wchar>();
}
#endif

TEST(NameSymbolDecodeTests, ResolvesNarrowSpansWithoutChangingUnresolvedText){
    __hidden_name_symbol_decode_tests::VerifyResolvedSpans<char>();
}

#if defined(GLB_UNICODE)
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
    const NameHash incomingHash = ComputeNameHash("admission/incoming");
    char incomingToken[NameSymbols::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(incomingHash, incomingToken, sizeof(incomingToken));
    AString<NWB::Core::Alloc::GlobalArena> record(arena);
    StringAppendFormat(record, "{}\truntime\tAdmission/Incoming\n", incomingToken);
    const usize retainedCount = NameSymbols::EntryCount();
    constexpr Array<AStringView, 6u> s_RejectedHeaders = {
        "",
        "nwb_namesym_v0\tproducer=runtime\n",
        "nwb_namesym_v2\tproducer=runtime\n",
        "nwb_namesym_v10\tproducer=runtime\n",
        "nwb_namesym_v1_suffix\tproducer=runtime\n",
        "\n",
    };
    char resolvedText[NameSymbols::s_MaxResolvedTextLength] = {};
    for(const AStringView header : s_RejectedHeaders){
        AString<NWB::Core::Alloc::GlobalArena> document(arena);
        document.append(header).append(record);
        EXPECT_FALSE(NameSymbols::LoadFromMemory(AStringView(document.data(), document.size())));
        EXPECT_EQ(NameSymbols::EntryCount(), retainedCount);
        EXPECT_TRUE(NameSymbols::Resolve(retained.hash(), resolvedText, sizeof(resolvedText)));
        EXPECT_EQ(AStringView(resolvedText), "admission/retained");
        EXPECT_FALSE(NameSymbols::Resolve(incomingHash, resolvedText, sizeof(resolvedText)));
    }

    AString<NWB::Core::Alloc::GlobalArena> currentDocument(arena);
    currentDocument.append(NameSymbols::s_FileHeader).append("\tproducer=runtime\r\n").append(record);
    ASSERT_TRUE(NameSymbols::LoadFromMemory(AStringView(currentDocument.data(), currentDocument.size())));
    EXPECT_TRUE(NameSymbols::Resolve(retained.hash(), resolvedText, sizeof(resolvedText)));
    ASSERT_TRUE(NameSymbols::Resolve(incomingHash, resolvedText, sizeof(resolvedText)));
    EXPECT_EQ(AStringView(resolvedText), "admission/incoming");
    NameSymbols::ClearRuntimeSymbols();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

