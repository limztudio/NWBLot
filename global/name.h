// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHash;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_HashLaneCount = 8;
inline constexpr u64 FNV64_OFFSET_BASIS = 14695981039346656037ull;
inline constexpr u64 FNV64_PRIME = 1099511628211ull;

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

inline constexpr char Canonicalize(char c){
    if(c == '\\')
        return '/';
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline constexpr u64 FNV1a64(const char* str, u64 seed){
    u64 hash = seed;
    if(str == nullptr)
        return hash;
    for(; *str != '\0'; ++str){
        hash ^= static_cast<u64>(static_cast<u8>(Canonicalize(*str)));
        hash *= FNV64_PRIME;
    }
    return hash;
}

inline constexpr void CopyCanonical(char* dst, const usize dstSize, const char* src){
    if(dstSize == 0)
        return;

    if(src == nullptr){
        dst[0] = '\0';
        return;
    }

    usize writeIndex = 0;
    for(; writeIndex + 1 < dstSize && src[writeIndex] != '\0'; ++writeIndex)
        dst[writeIndex] = Canonicalize(src[writeIndex]);
    dst[writeIndex] = '\0';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHash{
    u64 qwords[8];
};
static_assert(sizeof(NameHash) == 64, "NameHash size must stay stable");


inline constexpr NameHash ComputeNameHash(const char* str){
    if(str == nullptr)
        return {};

    NameHash hash = {};
    for(u32 i = 0; i < __hidden_name::s_HashLaneCount; ++i)
        hash.qwords[i] = __hidden_name::FNV1a64(str, __hidden_name::LANE_SEEDS[i]);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name{


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

inline void HashToDebugString(const NameHash& hash, char* dst, const usize dstSize){
    if(dstSize == 0)
        return;

    static constexpr char s_Hex[] = "0123456789abcdef";

    const usize requiredLength = (16 * s_HashLaneCount) + (s_HashLaneCount - 1);
    if(dstSize <= requiredLength){
        dst[0] = '\0';
        return;
    }

    char* writeCursor = dst;
    for(u32 i = 0; i < s_HashLaneCount; ++i){
        if(i > 0)
            *writeCursor++ = '_';
        const u64 value = hash.qwords[i];
        for(int bitShift = 60; bitShift >= 0; bitShift -= 4)
            *writeCursor++ = s_Hex[(value >> bitShift) & 0xF];
    }
    *writeCursor = '\0';
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr bool operator==(const NameHash& a, const NameHash& b){
    return __hidden_name::EqualHash(a, b);
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
    constexpr Name(const char* str)
        : m_hash(ComputeNameHash(str))
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        __hidden_name::CopyCanonical(m_debugName, 256, str);
#endif
    }
    explicit Name(const NameHash& hash)
        : m_hash(hash)
#if defined(NWB_DEBUG)
        , m_debugName{}
#endif
    {
#if defined(NWB_DEBUG)
        __hidden_name::HashToDebugString(m_hash, m_debugName, 256);
#endif
    }


public:
    [[nodiscard]] constexpr explicit operator bool()const{
        return !__hidden_name::IsZeroHash(m_hash);
    }
    [[nodiscard]] constexpr const NameHash& hash()const{ return m_hash; }

    [[nodiscard]] const char* c_str()const{
#if defined(NWB_DEBUG)
        return m_debugName;
#else
        thread_local static char buf[(16 * __hidden_name::s_HashLaneCount) + (__hidden_name::s_HashLaneCount - 1) + 1];
        __hidden_name::HashToDebugString(m_hash, buf, sizeof(buf));
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

inline constexpr Name NAME_NONE = {};
static_assert(Name(nullptr) == NAME_NONE, "Name(nullptr) must produce NAME_NONE");
static_assert(!static_cast<bool>(Name(nullptr)), "Name(nullptr) must be invalid");
static_assert(Name("") != NAME_NONE, "Empty string hash must not be NAME_NONE");
static_assert(Name("A\\B") == Name("a/b"), "Name canonicalization must map '\\\\' to '/' and uppercase to lowercase");
static_assert(Name("PATH/TO/FILE") == Name("path/to/file"), "Name canonicalization must be case-insensitive");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Name>{
    usize operator()(const Name& name)const{
        return __hidden_name::HashValue(name.m_hash);
    }
};

template<>
struct hash<NameHash>{
    usize operator()(const NameHash& h)const{
        return __hidden_name::HashValue(h);
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

