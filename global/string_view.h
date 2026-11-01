// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <string_view>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename T>
using BasicStringView = std::basic_string_view<T>;
using StringView = BasicStringView<char>;
using AStringView = StringView;

template<typename StringT>
concept StringViewConvertible = requires(const StringT& text){
    typename StringT::value_type;
    BasicStringView<typename StringT::value_type>(text.data(), text.size());
};

template<StringViewConvertible StringT>
[[nodiscard]] inline constexpr BasicStringView<typename StringT::value_type> AsBasicStringView(const StringT& text)noexcept(noexcept(BasicStringView<typename StringT::value_type>(text.data(), text.size()))){
    return BasicStringView<typename StringT::value_type>(text.data(), text.size());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

