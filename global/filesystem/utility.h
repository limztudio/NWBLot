// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "operations.h"
#include "../span.h"
#include "../text_utils.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemUtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, GlobalFilesystemPathDetail::PathChar SourceCharT>
struct PathToStringArg{
    BasicStringView<SourceCharT> path;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT = char, GlobalFilesystemPathDetail::PathChar SourceCharT>
[[nodiscard]] inline GlobalFilesystemUtilityDetail::PathToStringArg<CharT, SourceCharT> PathToString(const BasicStringView<SourceCharT> path)noexcept{
    return GlobalFilesystemUtilityDetail::PathToStringArg<CharT, SourceCharT>{ path };
}

template<typename CharT = char, typename PathT>
    requires requires(const PathT& path){
        path.native().data();
        path.native().size();
    }
[[nodiscard]] inline auto PathToString(const PathT& path)noexcept(noexcept(path.native().data()) && noexcept(path.native().size())){
    using SourceChar = typename Decay_T<decltype(path.native())>::value_type;
    return GlobalFilesystemUtilityDetail::PathToStringArg<CharT, SourceChar>{ BasicStringView<SourceChar>(path.native().data(), path.native().size()) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace std{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename RequestedCharT, GlobalFilesystemPathDetail::PathChar SourceCharT, typename FormatCharT>
struct formatter<GlobalFilesystemUtilityDetail::PathToStringArg<RequestedCharT, SourceCharT>, FormatCharT>{
    constexpr auto parse(basic_format_parse_context<FormatCharT>& ctx)noexcept{
        return ctx.begin();
    }

    template<typename FormatContext>
    auto format(const GlobalFilesystemUtilityDetail::PathToStringArg<RequestedCharT, SourceCharT>& arg, FormatContext& ctx)const noexcept([]()consteval noexcept{
        using Output = Decay_T<decltype(DeclVal<FormatContext&>().out())>;
        return noexcept(DeclVal<void(*)(Output)noexcept>()(DeclVal<FormatContext&>().out()))
            && noexcept(WriteConvertedText<FormatCharT>(DeclVal<Output&>(), DeclVal<const BasicStringView<SourceCharT>&>()))
            && IsNothrowMoveConstructible_V<Output> && IsNothrowDestructible_V<Output>
        ;
    }()){
        auto out = ctx.out();
        WriteConvertedText<FormatCharT>(out, arg.path);
        return out;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT = char, typename ArenaT, GlobalFilesystemPathDetail::PathChar SourceCharT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToString(ArenaT& arena, const BasicStringView<SourceCharT> path){
    if constexpr(SameAs<CharT, SourceCharT>){
        return BasicString<CharT, ArenaT>(path.data(), path.size(), arena);
    }
    else{
        BasicString<CharT, ArenaT> output{arena};
        auto out = std::back_inserter(output);
        WriteConvertedText<CharT>(out, path);
        return output;
    }
}

template<typename CharT = char, typename ArenaT, typename PathT>
    requires requires(const PathT& path){ path.native(); }
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToString(ArenaT& arena, const PathT& path){
    return PathToString<CharT>(arena, path.native());
}

template<typename CharT = char, typename ArenaT, GlobalFilesystemPathDetail::PathChar SourceCharT>
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToGenericString(ArenaT& arena, const BasicStringView<SourceCharT> path){
    BasicString<CharT, ArenaT> output = PathToString<CharT>(arena, path);
    for(CharT& ch : output){
        if(ch == static_cast<CharT>('\\'))
            ch = static_cast<CharT>('/');
    }
    return output;
}

template<typename CharT = char, typename ArenaT, typename PathT>
    requires requires(const PathT& path){ path.native(); }
[[nodiscard]] inline BasicString<CharT, ArenaT> PathToGenericString(ArenaT& arena, const PathT& path){
    return PathToGenericString<CharT>(arena, path.native());
}

template<typename StringT, GlobalFilesystemPathDetail::PathChar SourceCharT>
[[nodiscard]] inline StringT PathToGenericString(const BasicStringView<SourceCharT> path){
    using CharT = typename StringT::value_type;

    StringT output;
    auto out = std::back_inserter(output);
    WriteConvertedText<CharT>(out, path);
    for(CharT& ch : output){
        if(ch == static_cast<CharT>('\\'))
            ch = static_cast<CharT>('/');
    }
    return output;
}

template<typename StringT, typename PathT>
    requires requires(const PathT& path){ path.native(); }
[[nodiscard]] inline StringT PathToGenericString(const PathT& path){
    return PathToGenericString<StringT>(path.native());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline constexpr b8 IsPathPrefixText(const BasicStringView<CharT> root, const BasicStringView<CharT> file)noexcept(IsArithmetic_V<CharT>){
    if(root == file)
        return true;
    return !root.empty() && file.size() > root.size() && StartsWith(file, root) && (root.back() == static_cast<CharT>('/') || file[root.size()] == static_cast<CharT>('/'));
}

template<typename CharT>
[[nodiscard]] inline b8 IsSingleLinePathText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return !text.empty() && !HasEmbeddedNull(text) && !HasLineBreak(text);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GlobalFilesystemUtilityDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
inline constexpr b8 IsSafeCacheNameChar(CharT ch)noexcept(IsArithmetic_V<CharT>){
    const b8 alphaNum = (ch >= CharT('a') && ch <= CharT('z')) || (ch >= CharT('0') && ch <= CharT('9'));
    const b8 safePunctuation = ch == CharT('.') || ch == CharT('_') || ch == CharT('-');
    return alphaNum || safePunctuation;
}

template<typename StringT>
inline void SanitizeCanonicalSafeCacheName(StringT& text)noexcept(IsNothrowRangeFor<StringT, decltype([](typename StringT::value_type&)noexcept([]()consteval noexcept{
    using CharT = typename StringT::value_type;
    return noexcept(IsSafeCacheNameChar(DeclVal<CharT&>())) && noexcept(DeclVal<CharT&>() = CharT('_')) && noexcept(DeclVal<CharT&>() = Canonicalize(DeclVal<CharT&>()));
}()){})>()){
    using CharT = typename StringT::value_type;
    for(CharT& ch : text){
        ch = Canonicalize(ch);

        if(!IsSafeCacheNameChar(ch))
            ch = CharT('_');
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT, typename ArenaT>
[[nodiscard]] inline BasicString<CharT, ArenaT> BuildCanonicalSafeCacheName(ArenaT& arena, const BasicStringView<CharT> text){
    if(text.empty())
        return BasicString<CharT, ArenaT>(arena);

    BasicString<CharT, ArenaT> output(text.data(), text.size(), arena);
    GlobalFilesystemUtilityDetail::SanitizeCanonicalSafeCacheName(output);
    return output;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename PathT>
[[nodiscard]] inline b8 PathHasListedExtension(const PathT& path, const Span<const AStringView> extensions)noexcept([]()consteval noexcept{
    using PathChar = typename PathT::value_type;
    if constexpr(requires{ DeclVal<const PathT&>().extensionView(); }){
        using Extension = Decay_T<decltype(DeclVal<const PathT&>().extensionView())>;
        return
            noexcept(DeclVal<void(*)(Extension)noexcept>()(DeclVal<const PathT&>().extensionView())) && IsNothrowDestructible_V<Extension>
            && noexcept(static_cast<b8>(DeclVal<const Extension&>().size() != DeclVal<usize>()))
            && noexcept(static_cast<b8>(DeclVal<usize&>() < DeclVal<const Extension&>().size()))
            && noexcept(static_cast<b8>(ToAsciiLower(DeclVal<const Extension&>()[DeclVal<usize&>()]) != static_cast<PathChar>(ToAsciiLower(DeclVal<const AStringView&>()[DeclVal<usize&>()]))))
        ;
    }
    else{
        using ExtensionPath = Decay_T<decltype(DeclVal<const PathT&>().extension())>;
        using Extension = Decay_T<decltype(DeclVal<const ExtensionPath&>().native())>;
        return
            noexcept(DeclVal<void(*)(ExtensionPath)noexcept>()(DeclVal<const PathT&>().extension())) && IsNothrowDestructible_V<ExtensionPath>
            && noexcept(DeclVal<void(*)(Extension)noexcept>()(DeclVal<const ExtensionPath&>().native())) && IsNothrowDestructible_V<Extension>
            && noexcept(static_cast<b8>(DeclVal<const Extension&>().size() != DeclVal<usize>()))
            && noexcept(static_cast<b8>(DeclVal<usize&>() < DeclVal<const Extension&>().size()))
            && noexcept(static_cast<b8>(ToAsciiLower(DeclVal<const Extension&>()[DeclVal<usize&>()]) != static_cast<PathChar>(ToAsciiLower(DeclVal<const AStringView&>()[DeclVal<usize&>()]))))
        ;
    }
}()){
    using PathChar = typename PathT::value_type;
    if constexpr(requires{ path.extensionView(); }){
        const auto extension = path.extensionView();
        for(const AStringView listed : extensions){
            if(extension.size() != listed.size())
                continue;
            b8 matched = true;
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
    else{
        const auto extensionPath = path.extension();
        const auto extension = extensionPath.native();
        for(const AStringView listed : extensions){
            if(extension.size() != listed.size())
                continue;
            b8 matched = true;
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
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

