// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/name.h>
#include <global/name_hash_text.h>
#include <global/diagnostics.h>
#include <global/not_null.h>
#include <tests/common/name_symbols.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GlobalCases{

namespace ConstexprNameChecks{
inline constexpr AStringView s_Source = "Identity\\Bounded.trailing";
inline constexpr AStringView s_Bounded = s_Source.substr(0u, 16u);
inline constexpr Name s_ViewName{s_Bounded};
inline constexpr Name s_LiteralName{"identity/bounded"};
inline constexpr Array<char, 3u> s_EmbeddedNull = { 'a', '\0', 'b' };
inline constexpr auto s_InvalidName = Name::FromText(AStringView(s_EmbeddedNull));
static_assert(s_ViewName == s_LiteralName);
static_assert(!s_InvalidName && s_InvalidName.error() == NameTextError::EmbeddedNull);
static_assert(Name{AStringView{}} != s_NameNone);
static_assert(Name{static_cast<const char*>(nullptr)} == s_NameNone);

inline constexpr auto s_NullNarrow = ComputeNameHash(static_cast<const char*>(nullptr));
inline constexpr auto s_NullWide = ComputeNameHash(static_cast<const wchar*>(nullptr));
static_assert(!s_NullNarrow && s_NullNarrow.error() == NameTextError::NullText);
static_assert(!s_NullWide && s_NullWide.error() == NameTextError::NullText);
inline constexpr auto s_EmptyPointer = ComputeNameHash("");
inline constexpr auto s_EmptyWidePointer = ComputeNameHash(L"");
inline constexpr auto s_EmptyView = ComputeNameHash(AStringView{});
inline constexpr auto s_EmptyWideView = ComputeNameHash(WStringView{});
static_assert(s_EmptyPointer && s_EmptyWidePointer && s_EmptyView && s_EmptyWideView);
static_assert(*s_EmptyPointer == *s_EmptyView && *s_EmptyWidePointer == *s_EmptyWideView);
static_assert(*s_EmptyView == ComputeNameHash(MakeNotNull("")));
static_assert(*s_EmptyView != NameHash{});
inline constexpr Array<char, 4u> s_Narrow = { 'a', '\0', 'b', '\0' };
inline constexpr Array<wchar, 4u> s_Wide = { L'a', L'\0', L'b', L'\0' };
inline constexpr auto s_NarrowPointer = ComputeNameHash(s_Narrow.data());
inline constexpr auto s_WidePointer = ComputeNameHash(s_Wide.data());
inline constexpr auto s_NarrowBounded = ComputeNameHash(AStringView(s_Narrow.data(), 3u));
inline constexpr auto s_WideBounded = ComputeNameHash(WStringView(s_Wide.data(), 3u));
static_assert(s_NarrowPointer && s_WidePointer);
static_assert(*s_NarrowPointer == ComputeNameHash(MakeNotNull("a")));
static_assert(*s_WidePointer == ComputeNameHash(MakeNotNull(L"a")));
static_assert(!s_NarrowBounded && s_NarrowBounded.error() == NameTextError::EmbeddedNull);
static_assert(!s_WideBounded && s_WideBounded.error() == NameTextError::EmbeddedNull);
inline constexpr auto s_Validated = ComputeNameHash(NameText<char>{AStringView("Identity\\Text")});
inline constexpr auto s_ValidatedWide = ComputeNameHash(NameText<wchar>{WStringView(L"identity/text")});
static_assert(s_Validated == s_ValidatedWide);
static_assert(s_Validated == ComputeNameHash(MakeNotNull("identity/text")));

static_assert(!noexcept(Name::FromText(AStringView{})));
static_assert(Name{static_cast<const wchar*>(nullptr)} == s_NameNone);
inline constexpr Array<char, 3u> s_InvalidNarrow = { 'a', '\0', 'b' };
inline constexpr Array<wchar, 3u> s_InvalidWide = { L'a', L'\0', L'b' };
inline constexpr auto s_InvalidWideName = Name::FromText(WStringView(s_InvalidWide.data(), s_InvalidWide.size()));
static_assert(!s_InvalidWideName && s_InvalidWideName.error() == NameTextError::EmbeddedNull);
};

class MutableSymbolText{
public:
    explicit MutableSymbolText(Span<char> bytes)noexcept : m_bytes(bytes){}
    [[nodiscard]] usize size()const noexcept{ return m_bytes.size(); }
    void set(usize index, u8 byte)noexcept{ m_bytes[index] = static_cast<char>(byte); }
private:
    Span<char> m_bytes;
};

class ScopedNameCallbacks final : NoCopy{
public:
    template<typename Probe>
    explicit ScopedNameCallbacks(Probe& probe){
        SetNameSymbolRecordCallback([](const NameHash&, AStringView, void* context){
            static_cast<Probe*>(context)->record();
        }, &probe);
        SetNameSymbolResolveCallback([](char* text, usize capacity, const NameHash&, void* context){
            MutableSymbolText output(Span<char>(text, capacity));
            return static_cast<Probe*>(context)->resolve(output);
        }, &probe);
    }
    ~ScopedNameCallbacks(){
        SetNameSymbolRecordCallback(m_recorder.callback, m_recorder.userData);
        SetNameSymbolResolveCallback(m_resolver.callback, m_resolver.userData);
    }

private:
    NameDetail::NameSymbolRecorderState m_recorder = NameDetail::SymbolRecorderState();
    NameDetail::NameSymbolResolverState m_resolver = NameDetail::SymbolResolverState();
};

using InteropNameText = AInteropString;
inline constexpr WStringView s_RuntimeWideName = L"identity/runtime";
inline constexpr WStringView s_InvalidWideNameText(ConstexprNameChecks::s_Wide.data(), 3u);
#if defined(NWB_DEBUG)
inline constexpr b8 s_DebugNameBuild = true;
#else
inline constexpr b8 s_DebugNameBuild = false;
#endif
[[nodiscard]] inline u8 TerminatorByte(AStringView text)noexcept{ return static_cast<u8>(text.data()[text.size()]); }
[[nodiscard]] inline auto NullNameHash(){ return ComputeNameHash(static_cast<const char*>(nullptr)); }
[[nodiscard]] inline NameHash LiteralHash(const char* text){ return ComputeNameHash(MakeNotNull(text)); }

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

