// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name_hash.h"
#include "sync.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameSymbolRecordCallback = void(*)(const NameHash& hash, AStringView text, void* userData);
using NameSymbolResolveCallback = b8(*)(char* outText, usize outTextSize, const NameHash& hash, void* userData);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NameDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct NameSymbolRecorderState{
    NameSymbolRecordCallback callback = nullptr;
    void* userData = nullptr;
};

struct NameSymbolResolverState{
    NameSymbolResolveCallback callback = nullptr;
    void* userData = nullptr;
};


[[nodiscard]] inline MallocMutex& SymbolRecorderMutex()noexcept{
    static MallocMutex s_Mutex;
    return s_Mutex;
}

[[nodiscard]] inline MallocMutex& SymbolResolverMutex()noexcept{
    static MallocMutex s_Mutex;
    return s_Mutex;
}

[[nodiscard]] inline Atomic<b8>& SymbolRecorderActive()noexcept{
    static Atomic<b8> s_Active{ false };
    return s_Active;
}

[[nodiscard]] inline Atomic<b8>& SymbolResolverActive()noexcept{
    static Atomic<b8> s_Active{ false };
    return s_Active;
}

[[nodiscard]] inline NameSymbolRecorderState& SymbolRecorderState()noexcept{
    static NameSymbolRecorderState s_State;
    return s_State;
}

[[nodiscard]] inline NameSymbolResolverState& SymbolResolverState()noexcept{
    static NameSymbolResolverState s_State;
    return s_State;
}

[[nodiscard]] inline b8& SymbolRecordInProgress()noexcept{
    thread_local static b8 s_InProgress = false;
    return s_InProgress;
}

[[nodiscard]] inline b8& SymbolResolveInProgress()noexcept{
    thread_local static b8 s_InProgress = false;
    return s_InProgress;
}

class ScopedSymbolCallbackFlag final : NoCopy{
public:
    explicit ScopedSymbolCallbackFlag(b8& flag)noexcept
        : m_flag(flag)
    {
        m_flag = true;
    }
    ~ScopedSymbolCallbackFlag()noexcept{
        m_flag = false;
    }


private:
    b8& m_flag;
};

// Rotating thread-local scratch for Name text in opt/fin; ring avoids aliasing in one expression.
inline constexpr usize s_SymbolTextBufferLength = 1024u; // keep == NameSymbols::s_MaxResolvedTextLength
inline constexpr usize s_SymbolTextBufferCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Array<char, s_SymbolTextBufferLength>& NextSymbolTextBuffer()noexcept{
    thread_local static Array<Array<char, s_SymbolTextBufferLength>, s_SymbolTextBufferCount> s_Buffers = {};
    thread_local static usize s_Next = 0u;
    auto& buffer = s_Buffers[s_Next];
    s_Next = (s_Next + 1u) % s_SymbolTextBufferCount;
    return buffer;
}

inline void SetNameSymbolRecordCallback(const NameSymbolRecordCallback callback, void* const userData)noexcept{
    NothrowScopedLock lock(SymbolRecorderMutex());
    NameSymbolRecorderState& state = SymbolRecorderState();
    state.callback = callback;
    state.userData = callback ? userData : nullptr;
    SymbolRecorderActive().store(callback != nullptr, MemoryOrder::release);
}

inline void SetNameSymbolResolveCallback(const NameSymbolResolveCallback callback, void* const userData)noexcept{
    NothrowScopedLock lock(SymbolResolverMutex());
    NameSymbolResolverState& state = SymbolResolverState();
    state.callback = callback;
    state.userData = callback ? userData : nullptr;
    SymbolResolverActive().store(callback != nullptr, MemoryOrder::release);
}

[[nodiscard]] inline b8 ResolveNameSymbolText(char* const outText, const usize outTextSize, const NameHash& hash){
    if(IsZeroHash(hash) || outText == nullptr || outTextSize == 0u)
        return false;

    if(!SymbolResolverActive().load(MemoryOrder::acquire))
        return false;

    b8& inProgress = SymbolResolveInProgress();
    if(inProgress)
        return false;
    ScopedSymbolCallbackFlag resolveFlag(inProgress);

    NothrowScopedLock lock(SymbolResolverMutex());
    const NameSymbolResolverState& state = SymbolResolverState();
    if(!state.callback)
        return false;
    return state.callback(outText, outTextSize, hash, state.userData);
}

template<typename CharT>
inline void RecordNameSymbolText(const NameHash& hash, const BasicStringView<CharT> text){
    if(IsZeroHash(hash))
        return;

    if(!SymbolRecorderActive().load(MemoryOrder::acquire))
        return;

    b8& inProgress = SymbolRecordInProgress();
    if(inProgress)
        return;
    ScopedSymbolCallbackFlag recordFlag(inProgress);

    if constexpr(IsSame_V<CharT, char>){
        if(HasEmbeddedNull(text))
            return;
        NothrowScopedLock lock(SymbolRecorderMutex());
        const NameSymbolRecorderState& state = SymbolRecorderState();
        if(!state.callback)
            return;
        state.callback(hash, AStringView(text.data(), text.size()), state.userData);
    }
    else{
        // Wide->narrow by byte truncation, not UTF-8, so recorded text round-trips to the hash.
        Array<char, s_SymbolTextBufferLength> narrowText = {};
        usize copiedCount = 0u;
        for(const CharT ch : text){
            if(ch == CharT{})
                return;
            if((copiedCount + 1u) < narrowText.size()){
                narrowText[copiedCount] = static_cast<char>(ch);
                ++copiedCount;
            }
        }
        NothrowScopedLock lock(SymbolRecorderMutex());
        const NameSymbolRecorderState& state = SymbolRecorderState();
        if(!state.callback)
            return;
        state.callback(hash, AStringView(narrowText.data(), copiedCount), state.userData);
    }
}

#if defined(NWB_BUILD_SYMBOLS)
inline void RecordStoredNameSymbolText(const NameHash& hash, const AStringView text, const b8 hasText){
    if(!hasText || text.empty() || IsZeroHash(hash))
        return;

    usize textSize = 0u;
    while(textSize < text.size() && text[textSize] != '\0')
        ++textSize;
    if(textSize == text.size())
        return;

    RecordNameSymbolText(hash, text.substr(0u, textSize));
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void SetNameSymbolRecordCallback(const NameSymbolRecordCallback callback, void* const userData = nullptr)noexcept{
    NameDetail::SetNameSymbolRecordCallback(callback, userData);
}

inline void SetNameSymbolResolveCallback(const NameSymbolResolveCallback callback, void* const userData = nullptr)noexcept{
    NameDetail::SetNameSymbolResolveCallback(callback, userData);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

