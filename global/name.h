// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "hash_utils.h"
#include <functional>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameHash{
    u64 qwords[8];
};
static_assert(sizeof(NameHash) == 64, "NameHash size must stay stable");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameSymbolRecordCallback = void(*)(const NameHash& hash, AStringView text, void* userData);
using NameSymbolResolveCallback = bool(*)(const NameHash& hash, char* outText, usize outTextSize, void* userData);


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


inline constexpr void InitializeNameHash(NameHash& hash){
    for(u32 i = 0; i < s_HashLaneCount; ++i)
        hash.qwords[i] = LANE_SEEDS[i];
}

template<typename CharT>
inline constexpr void UpdateCanonicalNameHashLanes(NameHash& hash, const CharT ch){
    const u64 byte = static_cast<u64>(static_cast<u8>(Canonicalize(ch)));
    for(u32 i = 0; i < s_HashLaneCount; ++i){
        hash.qwords[i] ^= byte;
        hash.qwords[i] *= FNV64_PRIME;
    }
}

template<typename CharT>
[[nodiscard]] inline bool UpdateCanonicalNameHashText(NameHash& hash, const BasicStringView<CharT> text){
    for(const CharT ch : text){
        if(ch == CharT{})
            return false;
        UpdateCanonicalNameHashLanes(hash, ch);
    }

    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr NameHash ComputeNameHash(const CharT* str){
    if(str == nullptr)
        return {};

    NameHash hash = {};
    NameDetail::InitializeNameHash(hash);
    for(; *str != CharT{}; ++str)
        NameDetail::UpdateCanonicalNameHashLanes(hash, *str);
    return hash;
}

template<typename CharT>
inline NameHash ComputeNameHash(const BasicStringView<CharT> text){
    NameHash hash = {};
    NameDetail::InitializeNameHash(hash);
    if(!NameDetail::UpdateCanonicalNameHashText(hash, text))
        return {};
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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameSymbolRecorderState{
    NameSymbolRecordCallback callback = nullptr;
    void* userData = nullptr;
};

struct NameSymbolResolverState{
    NameSymbolResolveCallback callback = nullptr;
    void* userData = nullptr;
};


[[nodiscard]] inline NameSymbolRecorderState& SymbolRecorderState(){
    static NameSymbolRecorderState state;
    return state;
}

[[nodiscard]] inline NameSymbolResolverState& SymbolResolverState(){
    static NameSymbolResolverState state;
    return state;
}

[[nodiscard]] inline bool& SymbolRecordInProgress(){
    thread_local static bool inProgress = false;
    return inProgress;
}

[[nodiscard]] inline bool& SymbolResolveInProgress(){
    thread_local static bool inProgress = false;
    return inProgress;
}

class ScopedSymbolCallbackFlag final{
public:
    explicit ScopedSymbolCallbackFlag(bool& flag)
        : m_flag(flag)
    {
        m_flag = true;
    }
    ~ScopedSymbolCallbackFlag(){
        m_flag = false;
    }

    ScopedSymbolCallbackFlag(const ScopedSymbolCallbackFlag&) = delete;
    ScopedSymbolCallbackFlag& operator=(const ScopedSymbolCallbackFlag&) = delete;


private:
    bool& m_flag;
};

inline void SetNameSymbolRecordCallback(const NameSymbolRecordCallback callback, void* const userData){
    NameSymbolRecorderState& state = SymbolRecorderState();
    state.callback = callback;
    state.userData = userData;
}

inline void SetNameSymbolResolveCallback(const NameSymbolResolveCallback callback, void* const userData){
    NameSymbolResolverState& state = SymbolResolverState();
    state.callback = callback;
    state.userData = userData;
}

[[nodiscard]] inline bool ResolveNameSymbolText(const NameHash& hash, char* const outText, const usize outTextSize){
    if(IsZeroHash(hash) || outText == nullptr || outTextSize == 0u)
        return false;

    NameSymbolResolverState& state = SymbolResolverState();
    if(!state.callback)
        return false;

    bool& inProgress = SymbolResolveInProgress();
    if(inProgress)
        return false;
    ScopedSymbolCallbackFlag resolveFlag(inProgress);

    return state.callback(hash, outText, outTextSize, state.userData);
}

template<typename CharT>
inline void RecordNameSymbolText(
    const NameHash& hash,
    const BasicStringView<CharT> text
){
    if(IsZeroHash(hash))
        return;

    NameSymbolRecorderState& state = SymbolRecorderState();
    if(!state.callback)
        return;

    bool& inProgress = SymbolRecordInProgress();
    if(inProgress)
        return;
    ScopedSymbolCallbackFlag recordFlag(inProgress);

    if constexpr(IsSame_V<CharT, char>){
        if(HasEmbeddedNull(text))
            return;
        state.callback(hash, AStringView(text.data(), text.size()), state.userData);
    }
    else{
        char narrowText[1024] = {};
        usize copiedCount = 0u;
        for(const CharT ch : text){
            if(ch == CharT{})
                return;
            if((copiedCount + 1u) < sizeof(narrowText)){
                narrowText[copiedCount] = static_cast<char>(ch);
                ++copiedCount;
            }
        }
        state.callback(hash, AStringView(narrowText, copiedCount), state.userData);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


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
        if(i > 0){
            *writeCursor = static_cast<CharT>('_');
            ++writeCursor;
        }
        const u64 value = hash.qwords[i];
        for(i32 bitShift = 60; bitShift >= 0; bitShift -= 4){
            *writeCursor = static_cast<CharT>(s_Hex[(value >> bitShift) & 0xF]);
            ++writeCursor;
        }
    }
    *writeCursor = CharT{};
}

#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
template<typename CharT>
inline void CopyDebugName(const BasicStringView<CharT> text, char* dst, const usize dstSize){
    if(dstSize == 0)
        return;

    const usize copyMax = dstSize - 1;
    usize copyCount = 0;
    for(usize i = 0; i < text.size(); ++i){
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

#if defined(NWB_BUILDMODE)
inline void RecordStoredNameSymbolText(
    const NameHash& hash,
    const char* const text,
    const usize textCapacity,
    const bool hasText
){
    if(!hasText || text == nullptr || textCapacity == 0u || IsZeroHash(hash))
        return;

    usize textSize = 0u;
    while(textSize < textCapacity && text[textSize] != '\0')
        ++textSize;
    if(textSize == textCapacity)
        return;

    RecordNameSymbolText(hash, AStringView(text, textSize));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetNameSymbolRecordCallback(const NameSymbolRecordCallback callback, void* const userData = nullptr){
    NameDetail::SetNameSymbolRecordCallback(callback, userData);
}

inline void SetNameSymbolResolveCallback(const NameSymbolResolveCallback callback, void* const userData = nullptr){
    NameDetail::SetNameSymbolResolveCallback(callback, userData);
}

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
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(false)
#endif
    {}
    constexpr Name(std::nullptr_t)
        : m_hash{}
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(false)
#endif
    {}
    constexpr Name(const char* str)
        : m_hash(ComputeNameHash(str))
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        CopyCanonical(m_debugName, 256, str);
#endif
    }
    constexpr Name(const wchar* str)
        : m_hash(ComputeNameHash(str))
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(str != nullptr)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        CopyCanonical(m_debugName, 256, str);
#endif
    }
    explicit Name(const NameHash& hash)
        : m_hash(hash)
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(false)
#endif
    {
#if defined(NWB_DEBUG)
        NameDetail::HashToDebugString(m_hash, m_debugName, 256);
#endif
    }
    explicit Name(const AStringView text)
        : m_hash(ComputeNameHash(text))
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(true)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        NameDetail::CopyDebugName(text, m_debugName, 256);
#endif
        NameDetail::RecordNameSymbolText(m_hash, text);
    }
    explicit Name(const WStringView text)
        : m_hash(ComputeNameHash(text))
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        , m_debugName{}
#endif
#if defined(NWB_BUILDMODE)
        , m_hasSymbolText(true)
#endif
    {
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        NameDetail::CopyDebugName(text, m_debugName, 256);
#endif
        NameDetail::RecordNameSymbolText(m_hash, text);
    }


public:
    [[nodiscard]] constexpr explicit operator bool()const{
        return !NameDetail::IsZeroHash(hash());
    }
    [[nodiscard]] constexpr const NameHash& hash()const{
#if defined(NWB_BUILDMODE)
        recordBuildModeSymbolText();
#endif
        return m_hash;
    }

    [[nodiscard]] const char* c_str()const{
#if defined(NWB_BUILDMODE)
        recordBuildModeSymbolText();
#endif
#if defined(NWB_DEBUG)
        return m_debugName;
#else
        thread_local static char buf[1024];
        if(NameDetail::ResolveNameSymbolText(m_hash, buf, sizeof(buf)))
            return buf;

        NameDetail::HashToDebugString(m_hash, buf, sizeof(buf));
        return buf;
#endif
    }


private:
#if defined(NWB_BUILDMODE)
    constexpr void recordBuildModeSymbolText()const{
        if(!std::is_constant_evaluated())
            NameDetail::RecordStoredNameSymbolText(m_hash, m_debugName, sizeof(m_debugName), m_hasSymbolText);
    }
#endif


private:
    NameHash m_hash;
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
    char m_debugName[256];
#endif
#if defined(NWB_BUILDMODE)
    bool m_hasSymbolText;
#endif
};


inline constexpr bool operator==(const Name& a, const Name& b){
    return a.hash() == b.hash();
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
template<typename CharT, typename ArenaT>
[[nodiscard]] inline Name ToName(const BasicString<CharT, ArenaT>& text){
    return ToName(BasicStringView<CharT>(text));
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.data();
        text.size();
    }
[[nodiscard]] inline Name ToName(const StringT& text){
    using CharT = typename StringT::value_type;
    return ToName(BasicStringView<CharT>(text.data(), text.size()));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline u64 UpdateFnv64U64(u64 hash, const u64 value){
    for(u32 byteIndex = 0; byteIndex < sizeof(value); ++byteIndex){
        hash ^= static_cast<u8>((value >> (byteIndex * 8u)) & 0xFFu);
        hash *= FNV64_PRIME;
    }
    return hash;
}

inline constexpr char s_DerivePrefix[] = "nwb/name/derive";
inline constexpr u64 s_DerivePrefixHash = FNV1a64(s_DerivePrefix, FNV64_OFFSET_BASIS);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline bool BeginDerivedNameHash(const Name& baseName, NameHash& outDerivedHash){
    outDerivedHash = {};
    if(!baseName)
        return false;

    const NameHash& baseHash = baseName.hash();
    for(u32 lane = 0; lane < NameDetail::s_HashLaneCount; ++lane)
        outDerivedHash.qwords[lane] = NameDetail::UpdateFnv64U64(NameDetail::s_DerivePrefixHash, baseHash.qwords[lane]);

    return true;
}

template<typename CharT>
[[nodiscard]] inline bool UpdateDerivedNameHashText(NameHash& inOutDerivedHash, const BasicStringView<CharT> text){
    return NameDetail::UpdateCanonicalNameHashText(inOutDerivedHash, text);
}

[[nodiscard]] inline Name FinishDerivedNameHash(const NameHash& derivedHash){
    return Name(derivedHash);
}

template<typename CharT>
[[nodiscard]] inline Name DeriveName(const Name& baseName, const BasicStringView<CharT> suffix){
    if(!baseName || suffix.empty())
        return NAME_NONE;

    NameHash derivedHash = {};
    if(!BeginDerivedNameHash(baseName, derivedHash))
        return NAME_NONE;
    if(!UpdateDerivedNameHashText(derivedHash, suffix))
        return NAME_NONE;

    return FinishDerivedNameHash(derivedHash);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline Name DeriveName(const Name& baseName, const BasicString<CharT, ArenaT>& suffix){
    return DeriveName(baseName, BasicStringView<CharT>(suffix));
}


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> EncodeNameHash(ArenaT& arena, const Name& name){
    static constexpr u32 s_NameHashLaneCount = 8;
    static constexpr usize s_NameHashHexLength = s_NameHashLaneCount * 16;

    BasicString<CharT, ArenaT> encoded{arena};
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
template<typename CharT, typename ArenaT>
[[nodiscard]] inline bool DecodeNameHash(const BasicString<CharT, ArenaT>& encodedHash, Name& outName){
    return DecodeNameHash(BasicStringView<CharT>(encodedHash), outName);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<>
struct hash<Name>{
    usize operator()(const Name& name)const{
        return NameDetail::HashValue(name.hash());
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

