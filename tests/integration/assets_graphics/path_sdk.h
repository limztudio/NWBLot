// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/assets/paths.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests::GraphicsPathSdk{

class NativeUnits{
public:
    NativeUnits(::ArenaBase& arena, Span<const u32> units) : m_text(arena){
        m_text.reserve(units.size());
        for(const auto unit : units)
            m_text.push_back(static_cast<tchar>(unit));
    }
    [[nodiscard]] TStringView view()const noexcept{ return m_text; }

private:
    TString<::ArenaBase> m_text;
};

inline void ToUtf8(TStringView input, AString<::ArenaBase>& output){
    output = PathToString<char>(output.get_allocator().arena(), input);
}

inline constexpr usize s_NativeCharSize = sizeof(tchar);
#if defined(FOUNDATION_UNICODE)
inline constexpr b8 s_WideNativeChars = true;
#else
inline constexpr b8 s_WideNativeChars = false;
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

