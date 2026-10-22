// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>

#include <gtest/gtest.h>

#include <core/common/name_symbols.h>

#include <global/diagnostics.h>
#include <global/limit.h>
#include <global/text_utils.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name_identity_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_RecursiveInnerName = "identity/recursive_inner";

#if defined(NWB_BUILD_SYMBOLS)
constexpr u32 s_ExpectedDualCount = 2u;
#endif
#if !defined(NWB_DEBUG)
static constexpr AStringView s_RUNTIME_GENERATED = "runtime/generated";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif
using AString = NWB::Tests::TestAString;


struct NameSymbolCallbackProbe{
    NameDetail::NameSymbolRecorderState previousRecorder = NameDetail::SymbolRecorderState();
    NameDetail::NameSymbolResolverState previousResolver = NameDetail::SymbolResolverState();
    u32 recordCount = 0u;
    u32 resolveCount = 0u;


public:
    NameSymbolCallbackProbe()noexcept{
        SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context)noexcept{
            ++static_cast<NameSymbolCallbackProbe*>(context)->recordCount;
        }, this
        );
        SetNameSymbolResolveCallback([](const NameHash&, char*, usize, void* context)noexcept{
            ++static_cast<NameSymbolCallbackProbe*>(context)->resolveCount;
            return false;
        }, this
        );
    }
    ~NameSymbolCallbackProbe()noexcept{
        SetNameSymbolRecordCallback(previousRecorder.callback, previousRecorder.userData);
        SetNameSymbolResolveCallback(previousResolver.callback, previousResolver.userData);
    }
};


TEST(Global, DerivedNamesReportInvalidInputsAndPreserveCanonicalBinaryIdentity){
    const Name base("tests/derived_name/identity");
    constexpr AStringView s_Suffix = "suffix";
    constexpr AStringView s_UpperSuffix = "SUFFIX";
    constexpr char s_EmbeddedNull[]{ 'a', '\0', 'b' };
    EXPECT_FALSE(DeriveName(s_NameNone, s_Suffix));
    EXPECT_FALSE(DeriveName(base, AStringView{}));
    EXPECT_FALSE(DeriveName(base, AStringView(s_EmbeddedNull, LengthOf(s_EmbeddedNull))));
    EXPECT_FALSE(FinishDerivedNameHash(NameHash{}));
    const auto lower = DeriveName(base, s_Suffix);
    const auto upper = DeriveName(base, s_UpperSuffix);
    ASSERT_TRUE(lower);
    ASSERT_TRUE(upper);
    EXPECT_TRUE(*lower);
    EXPECT_EQ(*lower, *upper);
}

TEST(Global, ConstexprNameViewsPreserveBoundedIdentityAndSymbolCallbacks){
    constexpr AStringView s_Source = "Identity\\Bounded.trailing";
    constexpr AStringView s_Bounded = s_Source.substr(0u, 16u);
    constexpr Name s_ViewName{s_Bounded};
    constexpr Name s_LiteralName{"identity/bounded"};
    constexpr char s_EmbeddedNull[]{ 'a', '\0', 'b' };
    constexpr auto s_InvalidName = Name::FromText(AStringView(s_EmbeddedNull, LengthOf(s_EmbeddedNull)));
    static_assert(s_ViewName == s_LiteralName);
    static_assert(!s_InvalidName && s_InvalidName.error() == NameTextError::EmbeddedNull);
    static_assert(Name{AStringView{}} != s_NameNone);
    static_assert(Name{static_cast<const char*>(nullptr)} == s_NameNone);

    NameSymbolCallbackProbe probe;
    EXPECT_EQ(s_ViewName.identityHash(), s_LiteralName.identityHash());
    EXPECT_EQ(probe.recordCount, 0u);
    const Name runtimeView{s_Bounded};
    EXPECT_EQ(runtimeView, s_LiteralName);
    EXPECT_EQ(probe.recordCount, 1u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, BinaryIdentityLanesDoNotRecordNameSymbols){
    constexpr Name s_First{"Identity\\First"};
    NWB::Core::Common::NameSymbols::InstallRuntimeRegistry();
    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();

    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane){
        NameHash changed = s_First.identityHash();
        changed.qwords[lane] ^= 1u;
        const Name binary{changed};
        EXPECT_EQ(binary.identityHash(), changed);
        EXPECT_NE(binary.identityHash(), s_First.identityHash());
    }
    EXPECT_EQ(NWB::Core::Common::NameSymbols::EntryCount(), 0u);

#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_EQ(s_First.hash(), ComputeNameHash(MakeNotNull("identity/first")));
    EXPECT_EQ(NWB::Core::Common::NameSymbols::EntryCount(), 1u);
#endif
}

TEST(Global, NameBinaryIdentityNeverInvokesInstalledSymbolCallbacks){
    constexpr Name s_Literal{"identity/callback_probe"};
    const Name binary{ComputeNameHash(MakeNotNull("identity/binary_probe"))};
    NameSymbolCallbackProbe probe;
    EXPECT_EQ(s_Literal.identityHash(), ComputeNameHash(MakeNotNull("identity/callback_probe")));
    EXPECT_EQ(binary.identityHash(), ComputeNameHash(MakeNotNull("identity/binary_probe")));
    EXPECT_EQ(s_NameNone.identityHash(), NameHash{});
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);

    const Name recorded{AStringView("identity/recorded_probe")};
    EXPECT_EQ(probe.recordCount, 1u);
    char resolved[32u] = {};
    EXPECT_FALSE(NameDetail::ResolveNameSymbolText(recorded.identityHash(), resolved, LengthOf(resolved)));
    EXPECT_EQ(probe.recordCount, 1u);
    EXPECT_EQ(probe.resolveCount, 1u);
#if defined(NWB_BUILD_SYMBOLS)
    EXPECT_EQ(s_Literal.hash(), s_Literal.identityHash());
    EXPECT_EQ(probe.recordCount, s_ExpectedDualCount);
#endif
}

TEST(Global, NameResolvedTextPreservesSymbolLookupAndHashFallback){
    NWB::Core::Common::NameSymbols::InstallRuntimeRegistry();
    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();

    const Name runtimeName{AStringView("Runtime\\Generated")};
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(runtimeName.identityHash(), hashText, sizeof(hashText));
    const AStringView hashView(hashText, NameDetail::s_DebugHashTextLength);
    const Name binaryName{runtimeName.identityHash()};
    const DiagnosticEventText formattedName = MakeDiagnosticEventText("{}", binaryName);
#if defined(NWB_DEBUG)
    EXPECT_EQ(binaryName.resolvedText(), hashView);
    EXPECT_EQ(formattedName.view(), hashView);
#else
    EXPECT_EQ(binaryName.resolvedText(), s_RUNTIME_GENERATED);
    EXPECT_EQ(formattedName.view(), hashView);
    EXPECT_NE(binaryName.resolvedText(), hashView);
    EXPECT_NE(binaryName.resolvedText(), binaryName.logText());
#endif

    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();
    EXPECT_EQ(binaryName.resolvedText(), hashView);
    EXPECT_STREQ(binaryName.c_str(), hashText);
}

TEST(Global, DiagnosticNameTextNeverInvokesForeignSymbolCallbacks){
    constexpr Name s_Authored{"Diagnostic\\Authored"};
    const Name binary{ComputeNameHash(MakeNotNull("diagnostic/binary"))};
    NameSymbolCallbackProbe probe;
    SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context){
        ++static_cast<NameSymbolCallbackProbe*>(context)->recordCount;
        throw 17u;
    }, &probe
    );
    SetNameSymbolResolveCallback([](const NameHash&, char*, usize, void* context)->bool{
        ++static_cast<NameSymbolCallbackProbe*>(context)->resolveCount;
        throw 29u;
    }, &probe
    );

    EXPECT_EQ(MakeDiagnosticEventText(AStringView("{}"), s_Authored).view(), s_Authored.logText());
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(binary.identityHash(), hashText, sizeof(hashText));
    EXPECT_EQ(MakeDiagnosticEventText(AStringView("{}"), binary).view(), AStringView(hashText));
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, NameResolvedTextIsBoundedByStoredAndResolverBuffers){
    NameSymbolCallbackProbe probe;
    char source[NameDetail::s_DebugNameCapacity + 1u];
    for(char& character : source)
        character = 'x';
    const auto nameResult = Name::FromText(AStringView(source, sizeof(source)));
    ASSERT_TRUE(nameResult);
    const Name name = *nameResult;
#if defined(NWB_DEBUG)
    EXPECT_EQ(name.resolvedText().size(), NameDetail::s_DebugNameCapacity - 1u);
#else
    SetNameSymbolResolveCallback([](const NameHash&, char* outText, const usize outTextSize, void*)noexcept{
        for(usize i = 0u; i < outTextSize; ++i)
            outText[i] = 'x';
        return true;
    }
    );
    const AStringView resolved = name.resolvedText();
    EXPECT_EQ(resolved.size(), NameDetail::s_SymbolTextBufferLength - 1u);
    EXPECT_EQ(resolved.back(), 'x');
    EXPECT_EQ(resolved.data()[resolved.size()], '\0');
#endif
}

TEST(Global, NameHashTextDistinguishesNullEmptyAndEmbeddedNull){
    constexpr auto s_NullNarrow = ComputeNameHash(static_cast<const char*>(nullptr));
    constexpr auto s_NullWide = ComputeNameHash(static_cast<const wchar*>(nullptr));
    static_assert(!s_NullNarrow && s_NullNarrow.error() == NameTextError::NullText);
    static_assert(!s_NullWide && s_NullWide.error() == NameTextError::NullText);
    constexpr auto s_EmptyPointer = ComputeNameHash("");
    constexpr auto s_EmptyWidePointer = ComputeNameHash(L"");
    constexpr auto s_EmptyView = ComputeNameHash(AStringView{});
    constexpr auto s_EmptyWideView = ComputeNameHash(WStringView{});
    static_assert(s_EmptyPointer && s_EmptyWidePointer && s_EmptyView && s_EmptyWideView);
    static_assert(*s_EmptyPointer == *s_EmptyView && *s_EmptyWidePointer == *s_EmptyWideView);
    static_assert(*s_EmptyView == ComputeNameHash(MakeNotNull("")));
    static_assert(*s_EmptyView != NameHash{});
    constexpr char s_Narrow[]{ 'a', '\0', 'b', '\0' };
    constexpr wchar s_Wide[]{ L'a', L'\0', L'b', L'\0' };
    constexpr auto s_NarrowPointer = ComputeNameHash(s_Narrow);
    constexpr auto s_WidePointer = ComputeNameHash(s_Wide);
    constexpr auto s_NarrowBounded = ComputeNameHash(AStringView(s_Narrow, 3u));
    constexpr auto s_WideBounded = ComputeNameHash(WStringView(s_Wide, 3u));
    static_assert(s_NarrowPointer && s_WidePointer);
    static_assert(*s_NarrowPointer == ComputeNameHash(MakeNotNull("a")));
    static_assert(*s_WidePointer == ComputeNameHash(MakeNotNull(L"a")));
    static_assert(!s_NarrowBounded && s_NarrowBounded.error() == NameTextError::EmbeddedNull);
    static_assert(!s_WideBounded && s_WideBounded.error() == NameTextError::EmbeddedNull);
    constexpr auto s_Validated = ComputeNameHash(NameText<char>{AStringView("Identity\\Text")});
    constexpr auto s_ValidatedWide = ComputeNameHash(NameText<wchar>{WStringView(L"identity/text")});
    static_assert(s_Validated == s_ValidatedWide);
    static_assert(s_Validated == ComputeNameHash(MakeNotNull("identity/text")));

    NameSymbolCallbackProbe probe;
    const auto nullHash = ComputeNameHash(static_cast<const char*>(nullptr));
    ASSERT_FALSE(nullHash);
    EXPECT_EQ(nullHash.error(), NameTextError::NullText);
    const auto boundedHash = ComputeNameHash(AStringView(s_Narrow, 3u));
    ASSERT_FALSE(boundedHash);
    EXPECT_EQ(boundedHash.error(), NameTextError::EmbeddedNull);
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, RuntimeNameTextAdmissionPreservesIdentityAndCallbackBoundaries){
    static_assert(!noexcept(Name::FromText(AStringView{})));
    static_assert(Name{static_cast<const wchar*>(nullptr)} == s_NameNone);
    constexpr char s_InvalidNarrow[]{ 'a', '\0', 'b' };
    constexpr wchar s_InvalidWide[]{ L'a', L'\0', L'b' };
    constexpr auto s_InvalidWideName = Name::FromText(WStringView(s_InvalidWide, LengthOf(s_InvalidWide)));
    static_assert(!s_InvalidWideName && s_InvalidWideName.error() == NameTextError::EmbeddedNull);
    NameSymbolCallbackProbe probe;
    const auto invalidNarrow = Name::FromText(AStringView(s_InvalidNarrow, LengthOf(s_InvalidNarrow)));
    ASSERT_FALSE(invalidNarrow);
    EXPECT_EQ(invalidNarrow.error(), NameTextError::EmbeddedNull);
    const auto invalidWide = Name::FromText(WStringView(s_InvalidWide, LengthOf(s_InvalidWide)));
    ASSERT_FALSE(invalidWide);
    EXPECT_EQ(invalidWide.error(), NameTextError::EmbeddedNull);
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);

    const auto empty = Name::FromText(AStringView{});
    ASSERT_TRUE(empty);
    EXPECT_NE(*empty, s_NameNone);
    EXPECT_EQ(empty->identityHash(), ComputeNameHash(MakeNotNull("")));
    EXPECT_EQ(probe.recordCount, 1u);
    const auto narrow = Name::FromText(AStringView("Identity\\Runtime"));
    ASSERT_TRUE(narrow);
    EXPECT_EQ(narrow->identityHash(), ComputeNameHash(MakeNotNull("identity/runtime")));
    EXPECT_EQ(probe.recordCount, 2u);
    const auto wide = Name::FromText(WStringView(L"identity/runtime"));
    ASSERT_TRUE(wide);
    EXPECT_EQ(wide->identityHash(), narrow->identityHash());
    EXPECT_EQ(probe.recordCount, 3u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, OptionalNameTextPreservesAbsenceAndOwnedContainerErrors){
    constexpr char s_Invalid[]{ 'a', '\0', 'b' };
    NWB::Tests::TestArena<> testArena;
    BasicString<char, NWB::Core::Alloc::GlobalArena> owned(testArena.arena);
    owned.assign(s_Invalid, LengthOf(s_Invalid));
    AInteropString generic(s_Invalid, LengthOf(s_Invalid));
    NameSymbolCallbackProbe probe;
    const auto emptyNarrow = ToName(AStringView{});
    ASSERT_TRUE(emptyNarrow);
    EXPECT_FALSE(*emptyNarrow);
    const auto emptyWide = ToName(WStringView{});
    ASSERT_TRUE(emptyWide);
    EXPECT_FALSE(*emptyWide);
    const auto invalidView = ToName(AStringView(s_Invalid, LengthOf(s_Invalid)));
    ASSERT_FALSE(invalidView);
    EXPECT_EQ(invalidView.error(), NameTextError::EmbeddedNull);
    const auto invalidOwned = ToName<char, NWB::Core::Alloc::GlobalArena>(owned);
    ASSERT_FALSE(invalidOwned);
    EXPECT_EQ(invalidOwned.error(), NameTextError::EmbeddedNull);
    const auto invalidGeneric = ToName(generic);
    ASSERT_FALSE(invalidGeneric);
    EXPECT_EQ(invalidGeneric.error(), NameTextError::EmbeddedNull);
    EXPECT_EQ(probe.recordCount, 0u);
    EXPECT_EQ(probe.resolveCount, 0u);

    const auto valid = ToName(AStringView("Identity\\Optional"));
    ASSERT_TRUE(valid);
    ASSERT_TRUE(*valid);
    EXPECT_EQ((**valid).identityHash(), ComputeNameHash(MakeNotNull("identity/optional")));
    EXPECT_EQ(probe.recordCount, 1u);
    EXPECT_EQ(probe.resolveCount, 0u);
}

TEST(Global, RuntimeNameFactoryPreservesRecursiveRecorderGuard){
    struct RecursiveProbe{
        u32 recordCount = 0u;
        bool nestedAccepted = false;
    };
    NameSymbolCallbackProbe restoreCallbacks;
    RecursiveProbe probe;
    SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context)noexcept{
        auto& state = *static_cast<RecursiveProbe*>(context);
        ++state.recordCount;
        const auto nested = Name::FromText(AStringView(s_RecursiveInnerName));
        state.nestedAccepted = nested && nested->identityHash() == ComputeNameHash(MakeNotNull(s_RecursiveInnerName.data()));
    }, &probe
    );
    const auto first = Name::FromText(AStringView("identity/recursive_first"));
    ASSERT_TRUE(first);
    EXPECT_TRUE(probe.nestedAccepted);
    EXPECT_EQ(probe.recordCount, 1u);
    const auto second = Name::FromText(AStringView("identity/recursive_second"));
    ASSERT_TRUE(second);
    EXPECT_TRUE(probe.nestedAccepted);
    EXPECT_EQ(probe.recordCount, 2u);
    EXPECT_EQ(restoreCallbacks.recordCount, 0u);
    EXPECT_EQ(restoreCallbacks.resolveCount, 0u);
}

TEST(Global, NameHashDebugTextRejectsInvalidHex){
    const NameHash source = ComputeNameHash(MakeNotNull("global_name_hash_debug_text"));
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(source, hashText, sizeof(hashText));
    hashText[0u] = 'g';
    EXPECT_FALSE(NameDetail::DecodeDebugHashText(AStringView(hashText)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

