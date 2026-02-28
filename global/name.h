// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type.h"
#include <functional>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_name{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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

inline constexpr char ToLower(char c){
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : c;
}

inline constexpr u64 FNV1a64(const char* str, u64 seed){
    u64 hash = seed;
    for(; *str != '\0'; ++str){
        hash ^= static_cast<u64>(static_cast<u8>(ToLower(*str)));
        hash *= FNV64_PRIME;
    }
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


union NameHash{
    u8 bytes[64];
    u16 words[32];
    u32 dwords[16];
    u64 qwords[8];
};


inline constexpr NameHash ComputeNameHash(const char* str){
    NameHash hash = {};
    for(u32 i = 0; i < 8; ++i)
        hash.qwords[i] = __hidden_name::FNV1a64(str, __hidden_name::LANE_SEEDS[i]);
    return hash;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class NamePool;

class Name{
    friend class NamePool;
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
        if(str){
            const usize len = NWB_STRLEN(str);
            const usize copyLen = (len < 255) ? len : 255;
            for(usize i = 0; i < copyLen; ++i)
                m_debugName[i] = str[i];
            m_debugName[copyLen] = '\0';
        }
#endif
    }


public:
    [[nodiscard]] constexpr explicit operator bool()const{
        for(u32 i = 0; i < 8; ++i){
            if(m_hash.qwords[i] != 0)
                return true;
        }
        return false;
    }

    [[nodiscard]] const char* c_str()const{
#if defined(NWB_DEBUG)
        return m_debugName;
#else
        thread_local static char buf[128 + 7 + 1];
        static constexpr char hex[] = "0123456789abcdef";
        char* p = buf;
        for(u32 i = 0; i < 8; ++i){
            if(i > 0)
                *p++ = '_';
            u64 v = m_hash.qwords[i];
            for(int j = 60; j >= 0; j -= 4)
                *p++ = hex[(v >> j) & 0xF];
        }
        *p = '\0';
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
    return
        a.m_hash.qwords[0] == b.m_hash.qwords[0]
        && a.m_hash.qwords[1] == b.m_hash.qwords[1]
        && a.m_hash.qwords[2] == b.m_hash.qwords[2]
        && a.m_hash.qwords[3] == b.m_hash.qwords[3]
        && a.m_hash.qwords[4] == b.m_hash.qwords[4]
        && a.m_hash.qwords[5] == b.m_hash.qwords[5]
        && a.m_hash.qwords[6] == b.m_hash.qwords[6]
        && a.m_hash.qwords[7] == b.m_hash.qwords[7]
        ;
}
inline constexpr bool operator!=(const Name& a, const Name& b){
    return !(a == b);
}

inline constexpr Name NAME_NONE = {};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct std::hash<Name>{
    usize operator()(const Name& name)const{
        const auto& h = name.m_hash;
        usize seed = static_cast<usize>(h.qwords[0]);
        for(u32 i = 1; i < 8; ++i)
            seed ^= static_cast<usize>(h.qwords[i]) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }
};

template<>
struct std::hash<NameHash>{
    usize operator()(const NameHash& h)const{
        usize seed = static_cast<usize>(h.qwords[0]);
        for(u32 i = 1; i < 8; ++i)
            seed ^= static_cast<usize>(h.qwords[i]) + 0x9e3779b97f4a7c15ull + (seed << 6) + (seed >> 2);
        return seed;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

