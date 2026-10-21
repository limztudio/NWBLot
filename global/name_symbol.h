// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "name_hash.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using NameSymbolRecordCallback = void(*)(const NameHash& hash, AStringView text, void* userData);
using NameSymbolResolveCallback = bool(*)(const NameHash& hash, char* outText, usize outTextSize, void* userData);


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


[[nodiscard]] inline NameSymbolRecorderState& SymbolRecorderState()noexcept{
    static NameSymbolRecorderState s_state;
    return s_state;
}

[[nodiscard]] inline NameSymbolResolverState& SymbolResolverState()noexcept{
    static NameSymbolResolverState s_state;
    return s_state;
}

[[nodiscard]] inline bool& SymbolRecordInProgress()noexcept{
    thread_local static bool s_inProgress = false;
    return s_inProgress;
}

[[nodiscard]] inline bool& SymbolResolveInProgress()noexcept{
    thread_local static bool s_inProgress = false;
    return s_inProgress;
}

class ScopedSymbolCallbackFlag final{
public:
    explicit ScopedSymbolCallbackFlag(bool& flag)noexcept
        : m_flag(flag)
    {
        m_flag = true;
    }
    ~ScopedSymbolCallbackFlag()noexcept{
        m_flag = false;
    }
    ScopedSymbolCallbackFlag(const ScopedSymbolCallbackFlag&) = delete;


public:
    ScopedSymbolCallbackFlag& operator=(const ScopedSymbolCallbackFlag&) = delete;


private:
    bool& m_flag;
};

// Rotating thread-local scratch for Name text in opt/fin; ring avoids aliasing in one expression.
inline constexpr usize s_SymbolTextBufferLength = 1024u; // keep == NameSymbols::s_MaxResolvedTextLength
inline constexpr usize s_SymbolTextBufferCount = 8u;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline auto NextSymbolTextBuffer()noexcept -> char(&)[s_SymbolTextBufferLength]{
    thread_local static char s_buffers[s_SymbolTextBufferCount][s_SymbolTextBufferLength];
    thread_local static usize s_next = 0u;
    auto& buffer = s_buffers[s_next];
    s_next = (s_next + 1u) % s_SymbolTextBufferCount;
    return buffer;
}

inline void SetNameSymbolRecordCallback(const NameSymbolRecordCallback callback, void* const userData)noexcept{
    NameSymbolRecorderState& state = SymbolRecorderState();
    state.callback = callback;
    state.userData = userData;
}

inline void SetNameSymbolResolveCallback(const NameSymbolResolveCallback callback, void* const userData)noexcept{
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
inline void RecordNameSymbolText(const NameHash& hash, const BasicStringView<CharT> text){
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
        // Wide->narrow by byte truncation, not UTF-8, so recorded text round-trips to the hash.
        char narrowText[s_SymbolTextBufferLength];
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

#if defined(NWB_BUILD_SYMBOLS)
inline void RecordStoredNameSymbolText(const NameHash& hash, const AStringView text, const bool hasText){
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

