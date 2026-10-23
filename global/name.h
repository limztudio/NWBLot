// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "name_hash.h"
#include "name_symbol.h"
#include "optional.h"

#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
template<typename CharT>
inline constexpr void CopyDebugName(char* dst, const usize dstSize, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    if(dstSize == 0)
        return;

    const usize copyMax = dstSize - 1;
    usize copyCount = 0;
    for(usize i = 0u; i < text.size(); ++i){
        if(text[i] == CharT{}){
            dst[0] = '\0';
            return;
        }
        if(copyCount < copyMax){
            dst[copyCount] = static_cast<char>(Canonicalize(text[i]));
            ++copyCount;
        }
    }
    dst[copyCount] = '\0';
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Name{
    friend struct std::hash<Name>;
    friend constexpr bool operator==(const Name& a, const Name& b)noexcept;
    friend constexpr bool operator<(const Name& a, const Name& b)noexcept;


private:
    struct StringViewConstruction{};


public:
    template<typename CharT>
    [[nodiscard]] static constexpr Expected<Name, NameTextError::Enum> FromText(const BasicStringView<CharT> text){
        const auto hash = ComputeNameHash(text);
        if(!hash)
            return MakeUnexpected(hash.error());
        return Name(StringViewConstruction{}, *hash, text);
    }


private:
    template<typename CharT>
    constexpr Name(StringViewConstruction, const NameHash& hash, const BasicStringView<CharT> text)
        : m_hash(hash)
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(NWB_BUILD_SYMBOLS)
        , m_hasSymbolText(true)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        NameDetail::CopyDebugName(m_debugName, NameDetail::s_DebugNameCapacity, text);
#endif
        if(!IsConstantEvaluated())
            NameDetail::RecordNameSymbolText(m_hash, text);
    }
public:
    constexpr Name()noexcept
        : m_hash{}
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(NWB_BUILD_SYMBOLS)
        , m_hasSymbolText(false)
#endif
    {}
    constexpr Name(std::nullptr_t)noexcept
        : Name()
    {}
    constexpr Name(const char* str)noexcept
        : m_hash(str ? ComputeNameHash(MakeNotNull(str)) : NameHash{})
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(NWB_BUILD_SYMBOLS)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        CopyCanonical(m_debugName, NameDetail::s_DebugNameCapacity, str);
#endif
    }
    constexpr Name(const wchar* str)noexcept
        : m_hash(str ? ComputeNameHash(MakeNotNull(str)) : NameHash{})
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(NWB_BUILD_SYMBOLS)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        CopyCanonical(m_debugName, NameDetail::s_DebugNameCapacity, str);
#endif
    }
    explicit Name(const NameHash& hash)noexcept
        : m_hash(hash)
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(NWB_BUILD_SYMBOLS)
        , m_hasSymbolText(false)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        NameDetail::HashToDebugString(m_debugName, NameDetail::s_DebugNameCapacity, m_hash);
#endif
    }
    explicit constexpr Name(const NameText<char> text)
        : Name(StringViewConstruction{}, ComputeNameHash(text), text.m_text)
    {}
    explicit constexpr Name(const NameText<wchar> text)
        : Name(StringViewConstruction{}, ComputeNameHash(text), text.m_text)
    {}


public:
    [[nodiscard]] constexpr explicit operator bool()const noexcept{
        return !NameDetail::IsZeroHash(m_hash);
    }
    // Binary identity access never records or resolves symbol text, including cooker builds.


public:
    [[nodiscard]] constexpr const NameHash& identityHash()const noexcept{ return m_hash; }

    [[nodiscard]] constexpr const NameHash& hash()const{
#if defined(NWB_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
        return m_hash;
    }

    // In opt/fin, the view remains valid until its rotating thread-local symbol buffer is reused.
    [[nodiscard]] AStringView resolvedText()const{
#if defined(NWB_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
#if defined(NWB_DEBUG)
        const AStringView text(m_debugName, sizeof(m_debugName));
        return text.substr(0u, text.find('\0'));
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        if(NameDetail::ResolveNameSymbolText(buf, NameDetail::s_SymbolTextBufferLength, m_hash)){
            buf[NameDetail::s_SymbolTextBufferLength - 1u] = '\0';
            const AStringView text(buf, NameDetail::s_SymbolTextBufferLength);
            return text.substr(0u, text.find('\0'));
        }

        NameDetail::HashToDebugString(buf, NameDetail::s_SymbolTextBufferLength, m_hash);
        return AStringView(buf, NameDetail::s_DebugHashTextLength);
#endif
    }

    [[nodiscard]] const char* c_str()const{
#if defined(NWB_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
#if defined(NWB_DEBUG)
        return m_debugName;
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        if(NameDetail::ResolveNameSymbolText(buf, NameDetail::s_SymbolTextBufferLength, m_hash)){
            buf[NameDetail::s_SymbolTextBufferLength - 1u] = '\0';
            return buf;
        }

        NameDetail::HashToDebugString(buf, NameDetail::s_SymbolTextBufferLength, m_hash);
        return buf;
#endif
    }

    // Non-resolving text for labels/breadcrumbs: readable name in dbg, else hash hex.
    [[nodiscard]] AStringView logText()const noexcept{
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
        return AStringView(m_debugName);
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        NameDetail::HashToDebugString(buf, NameDetail::s_SymbolTextBufferLength, m_hash);
        return AStringView(buf, NameDetail::s_DebugHashTextLength);
#endif
    }


private:
#if defined(NWB_BUILD_SYMBOLS)
    constexpr void recordStoredSymbolText()const{
        if(!IsConstantEvaluated())
            NameDetail::RecordStoredNameSymbolText(m_hash, AStringView(m_debugName, sizeof(m_debugName)), m_hasSymbolText);
    }
#endif


private:
    NameHash m_hash;
#if defined(NWB_DEBUG) || defined(NWB_BUILD_SYMBOLS)
    char m_debugName[NameDetail::s_DebugNameCapacity];
#endif
#if defined(NWB_BUILD_SYMBOLS)
    bool m_hasSymbolText;
#endif
};


inline constexpr bool operator==(const Name& a, const Name& b)noexcept{
    return a.m_hash == b.m_hash;
}
inline constexpr bool operator!=(const Name& a, const Name& b)noexcept{
    return !(a == b);
}
inline constexpr bool operator<(const Name& a, const Name& b)noexcept{
    return LessNameHash(a.m_hash, b.m_hash);
}

inline constexpr Name s_NameNone = {};
static_assert(Name(nullptr) == s_NameNone, "Name(nullptr) must produce s_NameNone");
static_assert(!static_cast<bool>(Name(nullptr)), "Name(nullptr) must be invalid");
static_assert(Name("") != s_NameNone, "Empty string hash must not be s_NameNone");
static_assert(Name("A\\B") == Name("a/b"), "Name canonicalization must map '\\\\' to '/' and uppercase to lowercase");
static_assert(Name("PATH/TO/FILE") == Name("path/to/file"), "Name canonicalization must be case-insensitive");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline Expected<Optional<Name>, NameTextError::Enum> ToName(const BasicStringView<CharT> text){
    if(text.empty())
        return Optional<Name>{};

    auto name = Name::FromText(text);
    if(!name)
        return MakeUnexpected(name.error());
    return Optional<Name>{ Move(*name) };
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline Expected<Optional<Name>, NameTextError::Enum> ToName(const BasicString<CharT, ArenaT>& text){
    return ToName(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline Expected<Optional<Name>, NameTextError::Enum> ToName(const StringT& text){
    using CharT = typename StringT::value_type;
    return ToName(BasicStringView<CharT>(text.data(), text.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline u64 UpdateFnv64U64(u64 hash, const u64 value)noexcept{
    for(u32 byteIndex = 0u; byteIndex < sizeof(value); ++byteIndex){
        hash ^= static_cast<u8>((value >> (byteIndex * s_NameHashByteBitCount)) & s_NameHashByteMask);
        hash *= s_Fnv64Prime;
    }
    return hash;
}

inline constexpr AStringView s_DerivePrefix = "global/name/derive";
inline constexpr u64 s_DerivePrefixHash = UpdateFnv64TextCanonical(s_Fnv64OffsetBasis, s_DerivePrefix);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Expected<NameHash> BeginDerivedNameHash(const Name& baseName){
    NameHash derivedHash;
    if(!baseName)
        return MakeUnexpected(Failure{});

    const NameHash& baseHash = baseName.hash();
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane)
        derivedHash.qwords[lane] = NameDetail::UpdateFnv64U64(NameDetail::s_DerivePrefixHash, baseHash.qwords[lane]);

    return derivedHash;
}

template<typename CharT>
[[nodiscard]] inline bool UpdateDerivedNameHashText(NameHash& inOutDerivedHash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return NameDetail::UpdateCanonicalNameHashText(inOutDerivedHash, text);
}

[[nodiscard]] inline Expected<Name> FinishDerivedNameHash(const NameHash& derivedHash)noexcept{
    const Name name(derivedHash);
    if(!name)
        return MakeUnexpected(Failure{});
    return name;
}

template<typename CharT>
[[nodiscard]] inline Expected<Name> DeriveName(const Name& baseName, const BasicStringView<CharT> suffix){
    if(!baseName || suffix.empty())
        return MakeUnexpected(Failure{});

    auto derivedHash = BeginDerivedNameHash(baseName);
    if(!derivedHash)
        return MakeUnexpected(Failure{});
    if(!UpdateDerivedNameHashText(*derivedHash, suffix))
        return MakeUnexpected(Failure{});

    return FinishDerivedNameHash(*derivedHash);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline Expected<Name> DeriveName(const Name& baseName, const BasicString<CharT, ArenaT>& suffix){
    return DeriveName(baseName, BasicStringView<CharT>(suffix));
}

template<typename CharT>
[[nodiscard]] inline Expected<Name> DecodeNameHash(const BasicStringView<CharT> encodedHash)noexcept(IsArithmetic_V<CharT>){
    if(encodedHash.size() != NameDetail::s_EncodedNameHashLength)
        return MakeUnexpected(Failure{});

    NameHash hash;
    for(u32 lane = 0u; lane < NameDetail::s_HashLaneCount; ++lane){
        const usize begin = static_cast<usize>(lane) * NameDetail::s_HexDigitsPerHashLane;
        const BasicStringView<CharT> laneHex = encodedHash.substr(begin, NameDetail::s_HexDigitsPerHashLane);

        const auto laneHash = ParseHexU64<CharT>(laneHex);
        if(!laneHash)
            return MakeUnexpected(Failure{});
        hash.qwords[lane] = *laneHash;
    }

    return Name(hash);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline Expected<Name> DecodeNameHash(const BasicString<CharT, ArenaT>& encodedHash)noexcept(IsArithmetic_V<CharT>){
    return DecodeNameHash(BasicStringView<CharT>(encodedHash));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Name>{
    usize operator()(const Name& name)const noexcept{
        return NameDetail::HashValue(name.m_hash);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

