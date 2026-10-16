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


#if defined(NWB_BUILD_SYMBOLS)
constexpr u32 s_ExpectedDualCount = 2u;
#endif
#if !defined(NWB_DEBUG)
static constexpr AStringView s_RUNTIME_GENERATED = "runtime/generated";
#endif
using AString = NWB::Tests::TestAString;


struct NameSymbolCallbackProbe{
    NameDetail::NameSymbolRecorderState previousRecorder = NameDetail::SymbolRecorderState();
    NameDetail::NameSymbolResolverState previousResolver = NameDetail::SymbolResolverState();
    u32 recordCount = 0u;
    u32 resolveCount = 0u;


public:
    NameSymbolCallbackProbe(){
        SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context){
            ++static_cast<NameSymbolCallbackProbe*>(context)->recordCount;
        }, this);
        SetNameSymbolResolveCallback([](const NameHash&, char*, usize, void* context)noexcept{
            ++static_cast<NameSymbolCallbackProbe*>(context)->resolveCount;
            return false;
        }, this);
    }
    ~NameSymbolCallbackProbe(){
        SetNameSymbolRecordCallback(previousRecorder.callback, previousRecorder.userData);
        SetNameSymbolResolveCallback(previousResolver.callback, previousResolver.userData);
    }
};


TEST(Global, ConstexprNameViewsPreserveBoundedIdentityAndSymbolCallbacks){
    constexpr AStringView s_Source = "Identity\\Bounded.trailing";
    constexpr AStringView s_Bounded = s_Source.substr(0u, 16u);
    constexpr Name s_ViewName{s_Bounded};
    constexpr Name s_LiteralName{"identity/bounded"};
    constexpr char s_EmbeddedNull[]{ 'a', '\0', 'b' };
    constexpr Name s_InvalidName{AStringView(s_EmbeddedNull, LengthOf(s_EmbeddedNull))};
    static_assert(s_ViewName == s_LiteralName);
    static_assert(s_InvalidName == s_NameNone);
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
    EXPECT_EQ(s_First.hash(), ComputeNameHash("identity/first"));
    EXPECT_EQ(NWB::Core::Common::NameSymbols::EntryCount(), 1u);
#endif
}

TEST(Global, NameBinaryIdentityNeverInvokesInstalledSymbolCallbacks){
    constexpr Name s_Literal{"identity/callback_probe"};
    const Name binary{ComputeNameHash("identity/binary_probe")};
    NameSymbolCallbackProbe probe;
    EXPECT_EQ(s_Literal.identityHash(), ComputeNameHash("identity/callback_probe"));
    EXPECT_EQ(binary.identityHash(), ComputeNameHash("identity/binary_probe"));
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
    EXPECT_EQ(formattedName.view(), s_RUNTIME_GENERATED);
    EXPECT_NE(binaryName.resolvedText(), hashView);
    EXPECT_NE(binaryName.resolvedText(), binaryName.logText());
#endif

    NWB::Core::Common::NameSymbols::ClearRuntimeSymbols();
    EXPECT_EQ(binaryName.resolvedText(), hashView);
    EXPECT_STREQ(binaryName.c_str(), hashText);
}

TEST(Global, NameResolvedTextIsBoundedByStoredAndResolverBuffers){
    NameSymbolCallbackProbe probe;
    char source[NameDetail::s_DebugNameCapacity + 1u];
    for(char& character : source)
        character = 'x';
    const Name name{AStringView(source, sizeof(source))};
#if defined(NWB_DEBUG)
    EXPECT_EQ(name.resolvedText().size(), NameDetail::s_DebugNameCapacity - 1u);
#else
    SetNameSymbolResolveCallback([](const NameHash&, char* outText, const usize outTextSize, void*){
        for(usize i = 0u; i < outTextSize; ++i)
            outText[i] = 'x';
        return true;
    });
    const AStringView resolved = name.resolvedText();
    EXPECT_EQ(resolved.size(), NameDetail::s_SymbolTextBufferLength - 1u);
    EXPECT_EQ(resolved.back(), 'x');
    EXPECT_EQ(resolved.data()[resolved.size()], '\0');
#endif
}

TEST(Global, NameHashDebugTextRejectsInvalidHex){
    const NameHash source = ComputeNameHash("global_name_hash_debug_text");
    char hashText[NameDetail::s_DebugHashTextLength + 1u] = {};
    NameDetail::HashToDebugString(source, hashText, sizeof(hashText));
    hashText[0u] = 'g';
    EXPECT_FALSE(NameDetail::DecodeDebugHashText(AStringView(hashText)));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

