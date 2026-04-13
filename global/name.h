// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "hash_utils.h"
#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHash;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_HashLaneCount = 8;

inline constexpr u64 LANE_SEEDS[8] = {
    FNV64_OFFSET_BASIS,
    FNV64_OFFSET_BASIS ^ 0x0123456789ABCDEFull,
    FNV64_OFFSET_BASIS ^ 0xFEDCBA9876543210ull,
    FNV64_OFFSET_BASIS ^ 0x0F1E2D3C4B5A6978ull,
    FNV64_OFFSET_BASIS ^ 0x8796A5B4C3D2E1F0ull,
    FNV64_OFFSET_BASIS ^ 0xDEADBEEFCAFEBABEull,
    FNV64_OFFSET_BASIS ^ 0x1234ABCD5678EF01ull,
    FNV64_OFFSET_BASIS ^ 0xA0B1C2D3E4F50617ull,
};
static_assert(sizeof(LANE_SEEDS) / sizeof(LANE_SEEDS[0]) == s_HashLaneCount, "LANE_SEEDS count must match s_HashLaneCount");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHash{
    u64 qwords[8];
};
static_assert(sizeof(NameHash) == 64, "NameHash size must stay stable");


template<typename CharT>
inline constexpr NameHash ComputeNameHash(const CharT* str){
    if(str == nullptr)
        return {};

    NameHash hash = {};
    for(u32 i = 0; i < NameDetail::s_HashLaneCount; ++i)
        hash.qwords[i] = FNV1a64(str, NameDetail::LANE_SEEDS[i]);
    return hash;
}

template<typename CharT>
inline NameHash ComputeNameHash(const BasicStringView<CharT> text){
    if(HasEmbeddedNull(text))
        return {};

    NameHash hash = {};
    for(u32 i = 0; i < NameDetail::s_HashLaneCount; ++i)
        hash.qwords[i] = UpdateFnv64TextCanonical(NameDetail::LANE_SEEDS[i], text);
    return hash;
}


[[nodiscard]] inline constexpr bool LessNameHash(const NameHash& lhs, const NameHash& rhs){
    for(u32 i = 0; i < NameDetail::s_HashLaneCount; ++i){
        if(lhs.qwords[i] < rhs.qwords[i])
            return true;
        if(lhs.qwords[i] > rhs.qwords[i])
            return false;
    }

    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr bool EqualHash(const NameHash& a, const NameHash& b){
    for(u32 i = 0; i < s_HashLaneCount; ++i){
        if(a.qwords[i] != b.qwords[i])
            return false;
    }
    return true;
}

inline constexpr bool IsZeroHash(const NameHash& hash){
    for(u32 i = 0; i < s_HashLaneCount; ++i){
        if(hash.qwords[i] != 0)
            return false;
    }
    return true;
}

inline constexpr usize HashValue(const NameHash& hash){
    usize seed = static_cast<usize>(hash.qwords[0]);
    for(u32 i = 1; i < s_HashLaneCount; ++i){
        seed ^= static_cast<usize>(hash.qwords[i])
            + static_cast<usize>(0x9e3779b97f4a7c15ull)
            + (seed << 6)
            + (seed >> 2);
    }
    return seed;
}

template<typename CharT>
inline void HashToDebugString(const NameHash& hash, CharT* dst, const usize dstSize){
    if(dstSize == 0)
        return;

    static constexpr char s_Hex[] = "0123456789abcdef";

    const usize requiredLength = (16 * s_HashLaneCount) + (s_HashLaneCount - 1);
    if(dstSize <= requiredLength){
        dst[0] = CharT{};
        return;
    }

    CharT* writeCursor = dst;
    for(u32 i = 0; i < s_HashLaneCount; ++i){
        if(i > 0)
            *writeCursor++ = static_cast<CharT>('_');
        const u64 value = hash.qwords[i];
        for(i32 bitShift = 60; bitShift >= 0; bitShift -= 4)
            *writeCursor++ = static_cast<CharT>(s_Hex[(value >> bitShift) & 0xF]);
    }
    *writeCursor = CharT{};
}

#if defined(NWB_DEBUG)
template<typename CharT>
inline void CopyDebugName(const BasicStringView<CharT> text, char* dst, const usize dstSize){
    if(dstSize == 0)
        return;

    if(HasEmbeddedNull(text)){
        dst[0] = '\0';
        return;
    }

    const usize copyMax = dstSize - 1;
    const usize copyCount = text.size() < copyMax
        ? text.size()
        : copyMax
    ;
    for(usize i = 0; i < copyCount; ++i)
        dst[i] = static_cast<char>(Canonicalize(text[i]));
    dst[copyCount] = '\0';
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr bool operator==(const NameHash& a, const NameHash& b){
    return NameDetail::EqualHash(a, b);
}
inline constexpr bool operator!=(const NameHash& a, const NameHash& b){
    return !(a == b);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Name{
    friend struct std::hash<Name>;
    friend constexpr bool operator==(const Name& a, const Name& b);


public:
    constexpr Name()
        : m_hash{}
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {}
    constexpr Name(std::nullptr_t)
        : m_hash{}
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {}
    constexpr Name(const char* str)
        : m_hash(ComputeNameHash(str))
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        CopyCanonical(m_debugName, 256, str);
#endif
    }
    constexpr Name(const wchar* str)
        : m_hash(ComputeNameHash(str))
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        CopyCanonical(m_debugName, 256, str);
#endif
    }
    explicit Name(const NameHash& hash)
        : m_hash(hash)
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        NameDetail::HashToDebugString(m_hash, m_debugName, 256);
#endif
    }
    explicit Name(const AStringView text)
        : m_hash(ComputeNameHash(text))
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        NameDetail::CopyDebugName(text, m_debugName, 256);
#endif
    }
    explicit Name(const WStringView text)
        : m_hash(ComputeNameHash(text))
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        NameDetail::CopyDebugName(text, m_debugName, 256);
#endif
    }


public:
    [[nodiscard]] constexpr explicit operator bool()const{
        return !NameDetail::IsZeroHash(m_hash);
    }
    [[nodiscard]] constexpr const NameHash& hash()const{ return m_hash; }

    [[nodiscard]] const char* c_str()const{
#if defined(NWB_DEBUG)
        return m_debugName;
#else
        thread_local static char buf[(16 * NameDetail::s_HashLaneCount) + (NameDetail::s_HashLaneCount - 1) + 1];
        NameDetail::HashToDebugString(m_hash, buf, sizeof(buf));
        return buf;
#endif
    }


private:
    NameHash m_hash;
#if defined(NWB_DEBUG)
    char m_debugName[256];
#endif
};


inline constexpr bool operator==(const Name& a, const Name& b){
    return a.m_hash == b.m_hash;
}
inline constexpr bool operator!=(const Name& a, const Name& b){
    return !(a == b);
}
inline constexpr bool operator<(const Name& a, const Name& b){
    return LessNameHash(a.hash(), b.hash());
}

inline constexpr Name NAME_NONE = {};
static_assert(Name(nullptr) == NAME_NONE, "Name(nullptr) must produce NAME_NONE");
static_assert(!static_cast<bool>(Name(nullptr)), "Name(nullptr) must be invalid");
static_assert(Name("") != NAME_NONE, "Empty string hash must not be NAME_NONE");
static_assert(Name("A\\B") == Name("a/b"), "Name canonicalization must map '\\\\' to '/' and uppercase to lowercase");
static_assert(Name("PATH/TO/FILE") == Name("path/to/file"), "Name canonicalization must be case-insensitive");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline Name ToName(const BasicStringView<CharT> text){
    if(text.empty())
        return NAME_NONE;

    return Name(text);
}
template<typename CharT>
[[nodiscard]] inline Name ToName(const BasicString<CharT>& text){
    return ToName(BasicStringView<CharT>(text));
}


namespace NameDetail{


inline u64 UpdateFnv64U64(u64 hash, const u64 value){
    for(u32 byteIndex = 0; byteIndex < sizeof(value); ++byteIndex){
        hash ^= static_cast<u8>((value >> (byteIndex * 8u)) & 0xFFu);
        hash *= FNV64_PRIME;
    }
    return hash;
}


};


template<typename CharT>
[[nodiscard]] inline Name DeriveName(const Name& baseName, const BasicStringView<CharT> suffix){
    if(!baseName || suffix.empty() || HasEmbeddedNull(suffix))
        return NAME_NONE;

    NameHash derivedHash = {};
    static constexpr char s_DerivePrefix[] = "nwb/name/derive";
    for(u32 lane = 0; lane < NameDetail::s_HashLaneCount; ++lane){
        u64 laneHash = UpdateFnv64(
            FNV64_OFFSET_BASIS,
            reinterpret_cast<const u8*>(s_DerivePrefix),
            sizeof(s_DerivePrefix) - 1
        );
        laneHash = NameDetail::UpdateFnv64U64(laneHash, baseName.hash().qwords[lane]);
        laneHash = UpdateFnv64TextCanonical(laneHash, suffix);
        derivedHash.qwords[lane] = laneHash;
    }

    return Name(derivedHash);
}
template<typename CharT>
[[nodiscard]] inline Name DeriveName(const Name& baseName, const BasicString<CharT>& suffix){
    return DeriveName(baseName, BasicStringView<CharT>(suffix));
}


template<typename CharT>
[[nodiscard]] inline BasicString<CharT> EncodeNameHash(const Name& name){
    static constexpr u32 s_NameHashLaneCount = 8;
    static constexpr usize s_NameHashHexLength = s_NameHashLaneCount * 16;

    BasicString<CharT> encoded;
    encoded.reserve(s_NameHashHexLength);

    const NameHash& hash = name.hash();
    for(u32 lane = 0; lane < s_NameHashLaneCount; ++lane)
        AppendHexU64<CharT>(hash.qwords[lane], encoded);

    return encoded;
}


template<typename CharT>
[[nodiscard]] inline bool DecodeNameHash(const BasicStringView<CharT> encodedHash, Name& outName){
    static constexpr u32 s_NameHashLaneCount = 8;
    static constexpr usize s_NameHashHexLength = s_NameHashLaneCount * 16;

    if(encodedHash.size() != s_NameHashHexLength)
        return false;

    NameHash hash = {};
    for(u32 lane = 0; lane < s_NameHashLaneCount; ++lane){
        const usize begin = static_cast<usize>(lane) * 16;
        const BasicStringView<CharT> laneHex = encodedHash.substr(begin, 16);

        if(!ParseHexU64<CharT>(laneHex, hash.qwords[lane]))
            return false;
    }

    outName = Name(hash);
    return true;
}
template<typename CharT>
[[nodiscard]] inline bool DecodeNameHash(const BasicString<CharT>& encodedHash, Name& outName){
    return DecodeNameHash(BasicStringView<CharT>(encodedHash), outName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Name>{
    usize operator()(const Name& name)const{
        return NameDetail::HashValue(name.m_hash);
    }
};

template<>
struct hash<NameHash>{
    usize operator()(const NameHash& h)const{
        return NameDetail::HashValue(h);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

