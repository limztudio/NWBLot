// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "expected.h"
#include "name_hash.h"
#include "name_hash_text.h"
#include "name_symbol.h"
#include "optional.h"

#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
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
    friend constexpr b8 operator==(const Name& a, const Name& b)noexcept;
    friend constexpr b8 operator<(const Name& a, const Name& b)noexcept;


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
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
        , m_hasSymbolText(true)
#endif
    {
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        NameDetail::CopyDebugName(m_debugName.data(), NameDetail::s_DebugNameCapacity, text);
#endif
        if(!IsConstantEvaluated())
            NameDetail::RecordNameSymbolText(m_hash, text);
    }


public:
    constexpr Name()noexcept
        : m_hash{}
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
        , m_hasSymbolText(false)
#endif
    {}
    constexpr Name(Nullptr)noexcept
        : Name()
    {}
    constexpr Name(const char* str)noexcept
        : m_hash(str ? ComputeNameHash(MakeNotNull(str)) : NameHash{})
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        CopyCanonical(m_debugName.data(), NameDetail::s_DebugNameCapacity, str);
#endif
    }
    constexpr Name(const wchar* str)noexcept
        : m_hash(str ? ComputeNameHash(MakeNotNull(str)) : NameHash{})
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        CopyCanonical(m_debugName.data(), NameDetail::s_DebugNameCapacity, str);
#endif
    }
    explicit Name(const NameHash& hash)noexcept
        : m_hash(hash)
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        , m_debugName{}
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
        , m_hasSymbolText(false)
#endif
    {
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        NameHashTextCodec::Format(m_debugName.data(), NameDetail::s_DebugNameCapacity, m_hash);
#endif
    }
    explicit constexpr Name(const NameText<char> text)
        : Name(StringViewConstruction{}, ComputeNameHash(text), text.m_text)
    {}
    explicit constexpr Name(const NameText<wchar> text)
        : Name(StringViewConstruction{}, ComputeNameHash(text), text.m_text)
    {}


public:
    [[nodiscard]] constexpr explicit operator b8()const noexcept{
        return !IsZeroHash(m_hash);
    }


public:
    // Binary identity access never records or resolves symbol text, including cooker builds.
    [[nodiscard]] constexpr const NameHash& identityHash()const noexcept{ return m_hash; }

    [[nodiscard]] constexpr const NameHash& hash()const{
#if defined(FOUNDATION_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
        return m_hash;
    }

    // In opt/fin, the view remains valid until its rotating thread-local symbol buffer is reused.
    [[nodiscard]] AStringView resolvedText()const{
#if defined(FOUNDATION_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
#if defined(FOUNDATION_DEBUG)
        const AStringView text(m_debugName.data(), m_debugName.size());
        return text.substr(0u, text.find('\0'));
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        if(NameDetail::ResolveNameSymbolText(buf.data(), NameDetail::s_SymbolTextBufferLength, m_hash)){
            buf[NameDetail::s_SymbolTextBufferLength - 1u] = '\0';
            const AStringView text(buf.data(), NameDetail::s_SymbolTextBufferLength);
            return text.substr(0u, text.find('\0'));
        }

        NameHashTextCodec::Format(buf.data(), NameDetail::s_SymbolTextBufferLength, m_hash);
        return AStringView(buf.data(), NameHashTextCodec::s_TextLength);
#endif
    }

    [[nodiscard]] const char* c_str()const{
#if defined(FOUNDATION_BUILD_SYMBOLS)
        recordStoredSymbolText();
#endif
#if defined(FOUNDATION_DEBUG)
        return m_debugName.data();
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        if(NameDetail::ResolveNameSymbolText(buf.data(), NameDetail::s_SymbolTextBufferLength, m_hash)){
            buf[NameDetail::s_SymbolTextBufferLength - 1u] = '\0';
            return buf.data();
        }

        NameHashTextCodec::Format(buf.data(), NameDetail::s_SymbolTextBufferLength, m_hash);
        return buf.data();
#endif
    }

    // Non-resolving text for labels/breadcrumbs: readable name in dbg, else hash hex.
    [[nodiscard]] AStringView logText()const noexcept{
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
        return AStringView(m_debugName.data());
#else
        auto& buf = NameDetail::NextSymbolTextBuffer();
        NameHashTextCodec::Format(buf.data(), NameDetail::s_SymbolTextBufferLength, m_hash);
        return AStringView(buf.data(), NameHashTextCodec::s_TextLength);
#endif
    }


private:
#if defined(FOUNDATION_BUILD_SYMBOLS)
    constexpr void recordStoredSymbolText()const{
        if(!IsConstantEvaluated())
            NameDetail::RecordStoredNameSymbolText(m_hash, AStringView(m_debugName.data(), m_debugName.size()), m_hasSymbolText);
    }
#endif


private:
    NameHash m_hash;
#if defined(FOUNDATION_DEBUG) || defined(FOUNDATION_BUILD_SYMBOLS)
    Array<char, NameDetail::s_DebugNameCapacity> m_debugName;
#endif
#if defined(FOUNDATION_BUILD_SYMBOLS)
    b8 m_hasSymbolText;
#endif
};


inline constexpr b8 operator==(const Name& a, const Name& b)noexcept{
    return a.m_hash == b.m_hash;
}
inline constexpr b8 operator!=(const Name& a, const Name& b)noexcept{
    return !(a == b);
}
inline constexpr b8 operator<(const Name& a, const Name& b)noexcept{
    return LessNameHash(a.m_hash, b.m_hash);
}

inline constexpr Name s_NameNone = {};
static_assert(Name(nullptr) == s_NameNone, "Name(nullptr) must produce s_NameNone");
static_assert(!static_cast<b8>(Name(nullptr)), "Name(nullptr) must be invalid");
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
template<StringViewConvertible StringT>
[[nodiscard]] inline Expected<Optional<Name>, NameTextError::Enum> ToName(const StringT& text){
    return ToName(AsBasicStringView(text));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
    for(u32 lane = 0u; lane < s_NameHashLaneCount; ++lane)
        derivedHash.qwords[lane] = UpdateFnv64U64LittleEndian(NameDetail::s_DerivePrefixHash, baseHash.qwords[lane]);

    return derivedHash;
}

template<typename CharT>
[[nodiscard]] inline b8 UpdateDerivedNameHashText(NameHash& inOutDerivedHash, const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return NameDetail::UpdateCanonicalNameHashText(inOutDerivedHash, text);
}

[[nodiscard]] inline constexpr b8 UpdateDerivedNameHashDecimal(NameHash& inOutDerivedHash, const u64 value)noexcept{
    Array<char, 20u> buffer{};
    usize index = buffer.size();
    u64 remaining = value;
    do{
        --index;
        buffer[index] = static_cast<char>('0' + static_cast<char>(remaining % 10u));
        remaining /= 10u;
    }while(remaining != 0u);
    return NameDetail::UpdateCanonicalNameHashText(inOutDerivedHash, AStringView(buffer.data() + index, buffer.size() - index));
}

[[nodiscard]] inline constexpr Expected<Name> ParseNameText(const AStringView text){
    const auto name = Name::FromText<char>(text);
    if(!name || !*name)
        return MakeUnexpected(Failure{});
    return *name;
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

