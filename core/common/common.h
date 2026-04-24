// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include <global/frame_data.h>
#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CommonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class BaseInitializerable{
public:
    virtual ~BaseInitializerable() = default;


public:
    virtual bool initialize() = 0;
    virtual void finalize() = 0;
};
class FunctionalInitializerable : public BaseInitializerable{
public:
    template<typename INIT, typename FIN>
    FunctionalInitializerable(INIT&& init, FIN&& fin)
        : m_init(Forward<INIT>(init))
        , m_fin(Forward<FIN>(fin))
    {}


public:
    inline bool initialize()override{ return m_init ? m_init() : true; }
    inline void finalize()override{ if(m_fin) m_fin(); }


private:
    Function<bool()> m_init;
    Function<void()> m_fin;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Initializerable : public CommonDetail::BaseInitializerable{
public:
    Initializerable();
};
class Initializer{
public:
    static Initializer& instance();


private:
    struct InitializerItem{
        CommonDetail::BaseInitializerable* item = nullptr;
        UniquePtr<CommonDetail::BaseInitializerable> ownedItem;

        explicit InitializerItem(CommonDetail::BaseInitializerable* value)
            : item(value)
        {}
        template<typename T>
        explicit InitializerItem(UniquePtr<T>&& value)
            : item(value.get())
            , ownedItem(Move(value))
        {}
    };


private:
    Initializer()
        : m_cursor(m_items.before_begin())
    {}


public:
    inline bool initialize(){
        for(auto& cur : m_items){
            if(!cur.item->initialize())
                return false;
        }
        return true;
    }
    inline void finalize(){
        for(auto& cur : m_items)
            cur.item->finalize();
    }
    [[nodiscard]] inline bool acquire(){
        if(m_activeCount > 0u){
            ++m_activeCount;
            return true;
        }

        if(!initialize())
            return false;

        m_activeCount = 1u;
        return true;
    }
    inline void release(){
        NWB_ASSERT(m_activeCount > 0u);
        if(m_activeCount == 0u)
            return;

        --m_activeCount;
        if(m_activeCount == 0u)
            finalize();
    }

public:
    inline void enqueue(Initializerable* item){ m_cursor = m_items.emplace_after(m_cursor, item); }
    template<typename INITIALIZE, typename FINALIZE>
    inline void enqueue(INITIALIZE&& initialize, FINALIZE&& finalize){
        auto item = MakeUnique<CommonDetail::FunctionalInitializerable>(Forward<INITIALIZE>(initialize), Forward<FINALIZE>(finalize));
        m_cursor = m_items.emplace_after(m_cursor, Move(item));
    }


private:
    ForwardList<InitializerItem> m_items;
    decltype(m_items)::iterator m_cursor;
    usize m_activeCount = 0;
};

class InitializerGuard : NoCopy{
public:
    ~InitializerGuard(){ finalize(); }


public:
    [[nodiscard]] bool initialize(){
        if(m_active)
            return true;

        if(!Initializer::instance().acquire())
            return false;

        m_active = true;
        return true;
    }
    void finalize(){
        if(!m_active)
            return;

        Initializer::instance().release();
        m_active = false;
    }


private:
    bool m_active = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using FrameParam = FrameParamStorage<4>;
class FrameData : public BasicFrameData<4>{
public:
    using BasicFrameData<4>::BasicFrameData;

    inline u16& width(){ return m_data.u16[0]; }
    inline const u16& width()const{ return m_data.u16[0]; }

    inline u16& height(){ return m_data.u16[1]; }
    inline const u16& height()const{ return m_data.u16[1]; }

};
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
class WinFrame : public FrameData{
public:
    inline bool isActive()const{ return m_data.u8[4] != 0; }
    inline void setActive(bool value){ m_data.u8[4] = value ? 1u : 0u; }

    inline HINSTANCE instance()const{ return static_cast<HINSTANCE>(m_data.ptr[1]); }
    inline void setInstance(HINSTANCE value){ m_data.ptr[1] = value; }

    inline HWND hwnd()const{ return static_cast<HWND>(m_data.ptr[2]); }
    inline void setHwnd(HWND value){ m_data.ptr[2] = value; }
};
#elif defined(NWB_PLATFORM_LINUX)
namespace LinuxFrameBackend{
    enum Enum : u8{
        None = 0,
        X11,
        Wayland,
    };
};
class LinuxFrame : public FrameData{
public:
    inline bool isActive()const{ return m_data.u8[4] != 0; }
    inline void setActive(bool value){ m_data.u8[4] = value ? 1u : 0u; }

    inline LinuxFrameBackend::Enum backend()const{ return static_cast<LinuxFrameBackend::Enum>(m_data.u8[5]); }
    inline void setBackend(LinuxFrameBackend::Enum value){ m_data.u8[5] = static_cast<u8>(value); }

    inline void*& nativeDisplay(){ return m_data.ptr[1]; }
    inline void* const& nativeDisplay()const{ return m_data.ptr[1]; }

    inline u64& nativeWindowHandle(){ return m_data.u64[2]; }
    inline const u64& nativeWindowHandle()const{ return m_data.u64[2]; }

    inline void*& nativeState(){ return m_data.ptr[3]; }
    inline void* const& nativeState()const{ return m_data.ptr[3]; }

    inline u64& nativeAuxValue(){ return m_data.u64[3]; }
    inline const u64& nativeAuxValue()const{ return m_data.u64[3]; }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CommonDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
[[nodiscard]] inline bool CommandLineIsWordChar(const T ch){
    static_assert(IsSame_V<T, char> || IsSame_V<T, wchar>, "Command line parsing supports char and wchar strings");
    return (ch >= T('a') && ch <= T('z'))
        || (ch >= T('A') && ch <= T('Z'))
        || (ch >= T('0') && ch <= T('9'))
        || ch == T('_');
}

template<typename T>
[[nodiscard]] inline bool CommandLineIsSpace(const T ch){
    static_assert(IsSame_V<T, char> || IsSame_V<T, wchar>, "Command line parsing supports char and wchar strings");
    return ch == T(' ')
        || ch == T('\t')
        || ch == T('\n')
        || ch == T('\r')
        || ch == T('\f')
        || ch == T('\v');
}

template<typename T>
[[nodiscard]] inline bool TryParseCommandLineAssignmentAt(
    const BasicStringView<T> input,
    const usize begin,
    usize& outNext,
    BasicString<T>& outKey,
    BasicString<T>& outValue
){
    usize cursor = begin;
    if(cursor >= input.size() || !CommandLineIsWordChar(input[cursor]))
        return false;

    const usize keyBegin = cursor;
    do{
        ++cursor;
    }
    while(cursor < input.size() && CommandLineIsWordChar(input[cursor]));
    const usize keyEnd = cursor;

    while(cursor < input.size() && CommandLineIsSpace(input[cursor]))
        ++cursor;
    if(cursor >= input.size() || input[cursor] != T('='))
        return false;
    ++cursor;

    while(cursor < input.size() && CommandLineIsSpace(input[cursor]))
        ++cursor;
    if(cursor >= input.size())
        return false;

    usize valueBegin = cursor;
    usize valueEnd = cursor;
    if(input[cursor] == T('"')){
        const usize quotedValueBegin = cursor + 1u;
        usize quotedValueEnd = quotedValueBegin;
        while(quotedValueEnd < input.size() && input[quotedValueEnd] != T('"'))
            ++quotedValueEnd;

        if(quotedValueEnd < input.size()){
            valueBegin = quotedValueBegin;
            valueEnd = quotedValueEnd;
            cursor = quotedValueEnd + 1u;
        }
        else{
            while(cursor < input.size() && !CommandLineIsSpace(input[cursor]))
                ++cursor;
            valueEnd = cursor;
        }
    }
    else{
        while(cursor < input.size() && !CommandLineIsSpace(input[cursor]))
            ++cursor;
        valueEnd = cursor;
    }

    outKey.assign(input.data() + keyBegin, keyEnd - keyBegin);
    outValue.assign(input.data() + valueBegin, valueEnd - valueBegin);
    outNext = cursor;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline HashMap<BasicString<T>, BasicString<T>> parseCommandLine(BasicStringView<T> input){
    HashMap<BasicString<T>, BasicString<T>> output;
    usize cursor = 0;
    while(cursor < input.size()){
        if(!CommonDetail::CommandLineIsWordChar(input[cursor])){
            ++cursor;
            continue;
        }

        usize next = cursor;
        BasicString<T> key;
        BasicString<T> value;
        if(CommonDetail::TryParseCommandLineAssignmentAt(input, cursor, next, key, value)){
            output.insert_or_assign(Move(key), Move(value));
            cursor = next;
        }
        else{
            ++cursor;
        }
    }

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

