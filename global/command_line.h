// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_utils.h"
#include "type.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename StringT>
[[nodiscard]] inline StringT MakeOptionsErrorText(const AStringView prefix, const StringT& options){
    StringT message(prefix.data(), prefix.size());
    message += options;
    return message;
}

template<typename EnumT, typename ParseFunction, typename StringT>
[[nodiscard]] inline bool ParseOptionText(const StringT& value, EnumT& outValue, ParseFunction parseValue){
    const StringT normalized = NormalizeOptionText(value);
    return parseValue(AStringView(normalized.data(), normalized.size()), outValue);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CharT>
[[nodiscard]] inline bool CommandLineHasValidArgv(const isize argc, CharT** argv){
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

