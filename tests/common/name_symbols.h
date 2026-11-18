// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <core/common/name_symbols.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

template<usize Capacity>
class SymbolBuffer{
public:
    void format(const NameHash& hash)noexcept{ NameHashTextCodec::Format(m_chars.data(), m_chars.size(), hash); }
    [[nodiscard]] b8 resolve(const NameHash& hash){ return Core::Common::NameSymbols::Resolve(m_chars.data(), m_chars.size(), hash); }
    [[nodiscard]] AStringView view()const noexcept{ return AStringView(m_chars.data()); }
    [[nodiscard]] b8 resolveCallback(const NameHash& hash){ return NameDetail::ResolveNameSymbolText(m_chars.data(), m_chars.size(), hash); }
    void setByte(const usize index, const u8 value)noexcept{ m_chars[index] = static_cast<char>(value); }

private:
    Array<char, Capacity> m_chars{};
};

#if defined(NWB_BUILD_SYMBOLS)
inline constexpr b8 s_SymbolsEnabled = true;
#else
inline constexpr b8 s_SymbolsEnabled = false;
#endif

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

