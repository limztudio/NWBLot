// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "hash_utils.h"
#include <functional>
#include <type_traits>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_NameHashLaneCount = 8u;

struct NameHash{
    u64 qwords[s_NameHashLaneCount];
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameSymbolRecordCallback = void(*)(const NameHash& hash, AStringView text, void* userData);
using NameSymbolResolveCallback = bool(*)(const NameHash& hash, char* outText, usize outTextSize, void* userData);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr u32 s_HashLaneCount = s_NameHashLaneCount;
inline constexpr usize s_DebugNameCapacity = 256u;
inline constexpr usize s_NameHashBytes = sizeof(u64) * s_HashLaneCount;
inline constexpr usize s_HexDigitsPerHashLane = sizeof(u64) * 2u;
inline constexpr usize s_EncodedNameHashLength = s_HexDigitsPerHashLane * s_HashLaneCount;
inline constexpr usize s_DebugHashTextLength = s_EncodedNameHashLength + (s_HashLaneCount - 1u);
inline constexpr usize s_NameHashCombineGoldenRatio = static_cast<usize>(0x9e3779b97f4a7c15ull);
inline constexpr usize s_NameHashCombineLeftShiftBits = 6u;
inline constexpr usize s_NameHashCombineRightShiftBits = 2u;
inline constexpr i32 s_HashHexInitialBitShift = static_cast<i32>((s_HexU64DigitCount - 1u) * s_HexNibbleBits);
inline constexpr i32 s_HashHexBitShiftStep = static_cast<i32>(s_HexNibbleBits);
inline constexpr u32 s_NameHashByteBitCount = 8u;
inline constexpr u64 s_NameHashByteMask = 0xFFu;

static_assert(sizeof(NameHash) == s_NameHashBytes, "NameHash size must stay stable");

inline constexpr u64 LANE_SEEDS[s_HashLaneCount] = {
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

template<typename CharT>
[[nodiscard]] inline bool IsNameHashTokenChar(const CharT ch){
    return (ch >= static_cast<CharT>('0') && ch <= static_cast<CharT>('9'))
        || (ch >= static_cast<CharT>('a') && ch <= static_cast<CharT>('f'))
        || (ch >= static_cast<CharT>('A') && ch <= static_cast<CharT>('F'))
        || ch == static_cast<CharT>('_')
    ;
}

template<typename CharT>
[[nodiscard]] inline bool CopyDebugHashToken(const BasicStringView<CharT> text, const usize offset, char (&outHashText)[s_DebugHashTextLength + 1u]){
    if(offset + s_DebugHashTextLength > text.size())
        return false;
    if(offset > 0u && IsNameHashTokenChar(text[offset - 1u]))
        return false;
    if(offset + s_DebugHashTextLength < text.size() && IsNameHashTokenChar(text[offset + s_DebugHashTextLength]))
        return false;

    for(usize i = 0u; i < s_DebugHashTextLength; ++i){
        const CharT ch = text[offset + i];
        if(((i + 1u) % (s_HexDigitsPerHashLane + 1u)) == 0u){
            if(ch != static_cast<CharT>('_'))
                return false;
        }
        else if(!IsNameHashTokenChar(ch) || ch == static_cast<CharT>('_')){
            return false;
        }

        outHashText[i] = static_cast<char>(ch);
    }
    outHashText[s_DebugHashTextLength] = 0;
    return true;
}

[[nodiscard]] inline bool DecodeDebugHashText(const AStringView text, NameHash& outHash){
    if(text.size() != s_DebugHashTextLength)
        return false;

    NameHash hash = {};
    usize cursor = 0u;
    for(u32 lane = 0u; lane < s_HashLaneCount; ++lane){
        if((lane > 0u && text[cursor++] != '_') || cursor + s_HexDigitsPerHashLane > text.size())
            return false;

        if(!ParseHexU64<char>(AStringView(text.data() + cursor, s_HexDigitsPerHashLane), hash.qwords[lane]))
            return false;
        cursor += s_HexDigitsPerHashLane;
    }

    outHash = hash;
    return cursor == text.size();
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

// Rotating thread_local scratch buffers backing Name::c_str()/logText() in opt/fin (where there is no per-object
// m_debugName). A single shared buffer would alias when two Name text results are live in one expression (e.g. two
// Names in one log call), silently returning two pointers to the same last-written text. Rotating across a small ring
// lets up to s_SymbolTextBufferCount results coexist on a thread. Still bounded: more simultaneously-live results
// than the ring size will alias (acceptable -- this is a diagnostic/log path, not a value carrier).
inline constexpr usize s_SymbolTextBufferLength = 1024u; // keep == NameSymbols::s_MaxResolvedTextLength
inline constexpr usize s_SymbolTextBufferCount = 8u;
[[nodiscard]] inline char* NextSymbolTextBuffer(){
    thread_local static char buffers[s_SymbolTextBufferCount][s_SymbolTextBufferLength];
    thread_local static usize next = 0u;
    char* const buffer = buffers[next];
    next = (next + 1u) % s_SymbolTextBufferCount;
    return buffer;
}

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
        // Wide -> narrow by per-code-unit byte truncation, NOT UTF-8: ComputeNameHash hashes each wchar as
        // static_cast<u8>(Canonicalize(ch)), so the recorded text must use those same truncated bytes to round-trip
        // back to the Name's hash. Emitting real UTF-8 here would make the symbol text hash to a DIFFERENT NameHash.
        // Names are effectively ASCII identifiers/paths in this engine; a non-ASCII wide Name records as truncated
        // bytes (consistent with its hash, just not human-readable) -- acceptable given no non-ASCII Names exist.
        char narrowText[s_SymbolTextBufferLength] = {};
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
            + s_NameHashCombineGoldenRatio
            + (seed << s_NameHashCombineLeftShiftBits)
            + (seed >> s_NameHashCombineRightShiftBits);
    }
    return seed;
}

template<typename CharT>
inline void HashToDebugString(const NameHash& hash, CharT* dst, const usize dstSize){
    if(dstSize == 0)
        return;

    static constexpr char s_Hex[] = "0123456789abcdef";

    if(dstSize <= s_DebugHashTextLength){
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
        for(i32 bitShift = s_HashHexInitialBitShift; bitShift >= 0; bitShift -= s_HashHexBitShiftStep){
            *writeCursor = static_cast<CharT>(s_Hex[(value >> bitShift) & s_HexNibbleMask]);
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
        CopyCanonical(m_debugName, NameDetail::s_DebugNameCapacity, str);
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
        CopyCanonical(m_debugName, NameDetail::s_DebugNameCapacity, str);
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
        NameDetail::HashToDebugString(m_hash, m_debugName, NameDetail::s_DebugNameCapacity);
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
        NameDetail::CopyDebugName(text, m_debugName, NameDetail::s_DebugNameCapacity);
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
        NameDetail::CopyDebugName(text, m_debugName, NameDetail::s_DebugNameCapacity);
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
        char* const buf = NameDetail::NextSymbolTextBuffer();
        if(NameDetail::ResolveNameSymbolText(m_hash, buf, NameDetail::s_SymbolTextBufferLength))
            return buf;

        NameDetail::HashToDebugString(m_hash, buf, NameDetail::s_SymbolTextBufferLength);
        return buf;
#endif
    }

    // Non-resolving textual form for cheap breadcrumbs (arena allocation logging, crash markers): the readable debug
    // name where one exists (dbg/buildmode), else the raw hash hex -- WITHOUT consulting the runtime symbol resolver.
    // c_str() (which DOES resolve) must never run on the allocator path: resolving allocates from a GlobalArena, and
    // that arena's allocate() logs its own name, which re-enters the resolver -- the source of the opt stack overflow.
    // Use this anywhere a Name is logged from inside the allocator.
    [[nodiscard]] const char* logText()const{
#if defined(NWB_DEBUG) || defined(NWB_BUILDMODE)
        return m_debugName;
#else
        char* const buf = NameDetail::NextSymbolTextBuffer();
        NameDetail::HashToDebugString(m_hash, buf, NameDetail::s_SymbolTextBufferLength);
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
    char m_debugName[NameDetail::s_DebugNameCapacity];
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
        hash ^= static_cast<u8>((value >> (byteIndex * s_NameHashByteBitCount)) & s_NameHashByteMask);
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
    BasicString<CharT, ArenaT> encoded{arena};
    encoded.reserve(NameDetail::s_EncodedNameHashLength);

    const NameHash& hash = name.hash();
    for(u32 lane = 0; lane < NameDetail::s_HashLaneCount; ++lane)
        AppendHexU64<CharT>(hash.qwords[lane], encoded);

    return encoded;
}


template<typename CharT>
[[nodiscard]] inline bool DecodeNameHash(const BasicStringView<CharT> encodedHash, Name& outName){
    if(encodedHash.size() != NameDetail::s_EncodedNameHashLength)
        return false;

    NameHash hash = {};
    for(u32 lane = 0; lane < NameDetail::s_HashLaneCount; ++lane){
        const usize begin = static_cast<usize>(lane) * NameDetail::s_HexDigitsPerHashLane;
        const BasicStringView<CharT> laneHex = encodedHash.substr(begin, NameDetail::s_HexDigitsPerHashLane);

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

