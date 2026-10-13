// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "basic_string.h"
#include "text_utils.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool CommandLineHasValidArgv(const isize argc, CharT** argv)noexcept{
    return argc > 0 && argv != nullptr && argv[0] != nullptr;
}

template<typename CLI, typename CharT>
inline void CommandLineParseApp(CLI& cli, const isize argc, CharT** argv){
    if(CommandLineHasValidArgv(argc, argv)){
        cli.parse(static_cast<int>(argc), argv);
        return;
    }

    CharT emptyProgramName[] = { CharT('\0') };
    CharT* emptyArgv[] = { emptyProgramName, nullptr };
    cli.parse(1, emptyArgv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
[[nodiscard]] inline StringT NormalizeOptionText(StringT text){
    return ToAsciiLowerCopy(TrimCopy(text));
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmYesText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text == BasicStringView<CharT>("y") || text == BasicStringView<CharT>("yes") || text == BasicStringView<CharT>("true") || text == BasicStringView<CharT>("1");
}

template<typename CharT>
[[nodiscard]] inline bool IsConfirmNoText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    return text == BasicStringView<CharT>("n") || text == BasicStringView<CharT>("no") || text == BasicStringView<CharT>("false") || text == BasicStringView<CharT>("0");
}

template<typename CharT>
[[nodiscard]] inline Expected<bool> ParseConfirmText(const BasicStringView<CharT> text)noexcept(IsArithmetic_V<CharT>){
    if(IsConfirmYesText(text))
        return true;
    if(IsConfirmNoText(text))
        return false;
    return MakeUnexpected(Failure{});
}

template<typename StringT, typename EnumT, typename TextFunction>
[[nodiscard]] inline StringT BuildEnumOptionTexts(TextFunction textFunction, const EnumT* values, const usize valueCount){
    StringT text;
    for(usize i = 0u; i < valueCount; ++i){
        if(i > 0u)
            text += (i + 1u == valueCount) ? ", or " : ", ";
        text += textFunction(values[i]);
    }
    return text;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

