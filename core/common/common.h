// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <regex>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


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


template <typename T>
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

