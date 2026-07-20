#pragma once


#include "type.h"


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

