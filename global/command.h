// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ArgCommand{
    enum Enum : u8{
        LogAddress,
        LogPort,

        iNumArg,
    };
};
inline constexpr const char* g_ArgCmd[] = {
    "-a,--logaddress",
    "-p,--logport",
};
inline const Tuple<
    AString
    , u16
> g_ArgDefault = {
    "http://localhost",
    static_cast<u16>(7117),
};
inline constexpr const char* g_ArgDesc[] = {
    "Log server address",
    "Log server port",
};

template<ArgCommand::Enum i, typename CLI, typename T>
inline auto ArgAddOption(CLI& cli, T& arg){
    return cli.add_option(g_ArgCmd[static_cast<usize>(i)], arg, g_ArgDesc[static_cast<usize>(i)])->default_val(Get<static_cast<usize>(i)>(g_ArgDefault));
}

template<typename CharT>
[[nodiscard]] inline bool ArgHasValidArgv(const isize argc, CharT** argv){
    return argc > 0 && argv != nullptr && argv[0] != nullptr;
}

template<typename CLI, typename CharT>
inline void ArgParseApp(CLI& cli, const isize argc, CharT** argv){
    if(ArgHasValidArgv(argc, argv)){
        cli.parse(static_cast<int>(argc), argv);
        return;
    }

    CharT emptyProgramName[] = { CharT('\0') };
    CharT* emptyArgv[] = { emptyProgramName, nullptr };
    cli.parse(1, emptyArgv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

