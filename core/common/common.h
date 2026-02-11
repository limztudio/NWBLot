// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"
#include <core/alloc/general.h>

#include <regex>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common{
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
            : m_init(Forward(init), Forward(fin))
        {}


    public:
        inline bool initialize()override{ return m_init ? m_init() : true; }
        inline void finalize()override{ if(m_fin) m_fin(); }


    private:
        Function<bool()> m_init;
        Function<void()> m_fin;
    };
};
class Initializerable : public __hidden_common::BaseInitializerable{
public:
    Initializerable();
};
class Initializer{
public:
    static Initializer& instance();


private:
    Initializer()
        : m_cursor(m_items.before_begin())
    {}
    ~Initializer(){
        for(auto& cur : m_items){
            if(cur.second())
                delete cur.first();
        }
    }


public:
    inline bool initialize(){
        for(auto& cur : m_items){
            if(!cur.first()->initialize())
                return false;
        }
        return true;
    }
    inline void finalize(){
        for(auto& cur : m_items)
            cur.first()->finalize();
    }

public:
    inline void enqueue(Initializerable* item){ m_cursor = m_items.emplace_after(m_cursor, item, false); }
    template<typename INITIALIZE, typename FINALIZE>
    inline void enqueue(INITIALIZE&& initialize, FINALIZE&& finalize){ m_cursor = m_items.emplace_after(m_cursor, new __hidden_common::FunctionalInitializerable(Forward(initialize), Forward(finalize)), true); }


private:
    ForwardList<Pair<__hidden_common::BaseInitializerable*, bool>> m_items;
    decltype(m_items)::iterator m_cursor;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_common{
    union FrameParam{
        void* ptr[3];
        u64 u64[3];
        u32 u32[6];
        u16 u16[12];
        u8 u8[24];
    };
};
class FrameData{
public:
    inline FrameData() : m_data{ nullptr, }{}


public:
    inline u16& width(){ return m_data.u16[0]; }
    inline const u16& width()const{ return m_data.u16[0]; }

    inline u16& height(){ return m_data.u16[1]; }
    inline const u16& height()const{ return m_data.u16[2]; }


protected:
    __hidden_common::FrameParam m_data;
};
#if defined(NWB_PLATFORM_WINDOWS)
#include <windows.h>
class WinFrame : public FrameData{
public:
    inline bool& isActive(){ return reinterpret_cast<bool&>(m_data.u8[4]); }
    inline const bool& isActive()const{ return reinterpret_cast<const bool&>(m_data.u8[4]); }

    inline HINSTANCE& instance(){ return reinterpret_cast<HINSTANCE&>(m_data.ptr[1]); }
    inline const HINSTANCE& instance()const{ return reinterpret_cast<const HINSTANCE&>(m_data.ptr[1]); }

    inline HWND& hwnd(){ return reinterpret_cast<HWND&>(m_data.ptr[2]); }
    inline const HWND& hwnd()const{ return reinterpret_cast<const HWND&>(m_data.ptr[2]); }
};
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
inline HashMap<BasicString<T>, BasicString<T>> parseCommandLine(BasicStringView<T> input){
    std::basic_regex<T> regex(
        IsSame_V<T, wchar>
        ? L"(\\w+)\\s*=\\s*(?:\"([^\"]*)\"|(\\S+))"
        :  "(\\w+)\\s*=\\s*(?:\"([^\"]*)\"|(\\S+))"
    );

    HashMap<BasicString<T>, BasicString<T>> output;
    std::match_results<typename BasicString<T>::const_iterator> match;

    typename BasicString<T>::const_iterator itrSearch(input.cbegin());
    while(std::regex_search(itrSearch, input.cend(), match, regex)){
        output[match[1].str()] = match[2].matched ? match[2].str() : match[3].str();
        itrSearch = match.suffix().first;
    }

    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

