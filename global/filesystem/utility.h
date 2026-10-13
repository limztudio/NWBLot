// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemUtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename PathT>
struct PathToStringArg{
    const PathT& path;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT = char, typename PathT>
[[nodiscard]] inline GlobalFilesystemUtilityDetail::PathToStringArg<CharT, PathT> PathToString(const PathT& path)noexcept{
    return GlobalFilesystemUtilityDetail::PathToStringArg<CharT, PathT>{ path };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RequestedCharT, typename PathT, typename FormatCharT>
struct formatter<GlobalFilesystemUtilityDetail::PathToStringArg<RequestedCharT, PathT>, FormatCharT>{
    constexpr auto parse(basic_format_parse_context<FormatCharT>& ctx)noexcept{
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const GlobalFilesystemUtilityDetail::PathToStringArg<RequestedCharT, PathT>& arg, FormatContext& ctx)const{
        auto out = ctx.out();
        BasicStringDetail::WriteConvertedText<FormatCharT>(out, arg.path.native());
        return out;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT = char, typename ArenaT, typename PathT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToString(ArenaT& arena, const PathT& path){
    if constexpr(SameAs<CharT, typename PathT::value_type>){
        const auto native = path.native();
        return BasicString<CharT, ArenaT>(native.data(), native.size(), arena);
    }
    else{
        BasicString<CharT, ArenaT> output{arena};
        auto out = std::back_inserter(output);
        BasicStringDetail::WriteConvertedText<CharT>(out, path.native());
        return output;
    }
}

template<typename CharT = char, typename ArenaT, typename PathT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToGenericString(ArenaT& arena, const PathT& path){
    BasicString<CharT, ArenaT> output = PathToString<CharT>(arena, path);
    for(CharT& ch : output){
        if(ch == static_cast<CharT>('\\'))
            ch = static_cast<CharT>('/');
    }
    return output;
}

template<typename StringT, typename PathT>
[[nodiscard]] inline StringT PathToGenericString(const PathT& path){
    using CharT = typename StringT::value_type;

    StringT output;
    auto out = std::back_inserter(output);
    BasicStringDetail::WriteConvertedText<CharT>(out, path.native());
    for(CharT& ch : output){
        if(ch == static_cast<CharT>('\\'))
            ch = static_cast<CharT>('/');
    }
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline constexpr bool IsPathPrefixText(const BasicStringView<CharT> root, const BasicStringView<CharT> file)noexcept(IsArithmetic_V<CharT>){
    if(root == file)
        return true;
    return !root.empty() && file.size() > root.size() && StartsWith(file, root) && (root.back() == static_cast<CharT>('/') || file[root.size()] == static_cast<CharT>('/'));
}

template<typename CharT>
[[nodiscard]] inline bool IsSingleLinePathText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return !text.empty() && !HasEmbeddedNull(text) && !HasLineBreak(text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemUtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr bool IsSafeCacheNameChar(CharT ch)noexcept(IsArithmetic_V<CharT>){
    const bool alphaNum = (ch >= CharT('a') && ch <= CharT('z')) || (ch >= CharT('0') && ch <= CharT('9'));
    const bool safePunctuation = ch == CharT('.') || ch == CharT('_') || ch == CharT('-');
    return alphaNum || safePunctuation;
}

template<bool Canonical, typename StringT>
inline void SanitizeSafeCacheName(StringT& text){
    using CharT = typename StringT::value_type;
    for(CharT& ch : text){
        if constexpr(Canonical)
            ch = Canonicalize(ch);

        if(!IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
}

template<typename CharT, typename ArenaT, bool Canonical>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheNameImpl(ArenaT& arena, const BasicStringView<CharT> text){
    if(text.empty())
        return BasicString<CharT, ArenaT>(arena);

    BasicString<CharT, ArenaT> output(text.data(), text.size(), arena);
    SanitizeSafeCacheName<Canonical>(output);
    return output;
}

template<bool Canonical, typename StringT>
[[nodiscard]] inline StringT BuildSafeCacheNameCopy(const StringT& text){
    if(text.empty())
        return StringT(text.get_allocator());

    StringT output(text.data(), text.size(), text.get_allocator());
    SanitizeSafeCacheName<Canonical>(output);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheName(ArenaT& arena, const BasicStringView<CharT> text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameImpl<CharT, ArenaT, false>(arena, text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildSafeCacheName(const BasicString<CharT, ArenaT>& text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameCopy<false>(text);
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.empty();
        text.data();
        text.size();
        text.get_allocator();
    }
[[nodiscard]] inline StringT BuildSafeCacheName(const StringT& text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameCopy<false>(text);
}


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildCanonicalSafeCacheName(ArenaT& arena, const BasicStringView<CharT> text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameImpl<CharT, ArenaT, true>(arena, text);
}
template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildCanonicalSafeCacheName(const BasicString<CharT, ArenaT>& text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameCopy<true>(text);
}
template<typename StringT>
    requires requires(const StringT& text){
        typename StringT::value_type;
        text.empty();
        text.data();
        text.size();
        text.get_allocator();
    }
[[nodiscard]] inline StringT BuildCanonicalSafeCacheName(const StringT& text){
    return GlobalFilesystemUtilityDetail::BuildSafeCacheNameCopy<true>(text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT, typename PathT>
[[nodiscard]] inline StringT LowerPathExtension(const PathT& path){
    return ToAsciiLowerCopy(PathToGenericString<StringT>(path.extension()));
}

template<typename PathT, typename ExtensionArray>
[[nodiscard]] inline bool PathHasListedExtension(const PathT& path, const ExtensionArray& extensions){
    using PathChar = typename PathT::value_type;
    const auto extensionPath = path.extension();
    const auto extension = extensionPath.native();
    for(usize i = 0u; i < LengthOf(extensions); ++i){
        const AStringView listed(extensions[i].data(), extensions[i].size());
        if(extension.size() != listed.size())
            continue;
        bool matched = true;
        for(usize c = 0u; c < extension.size(); ++c){
            if(ToAsciiLower(extension[c]) != static_cast<PathChar>(ToAsciiLower(listed[c]))){
                matched = false;
                break;
            }
        }
        if(matched)
            return true;
    }
    return false;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

