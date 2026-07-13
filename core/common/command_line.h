
#pragma once


#include "module.h"
#include <global/command_line.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


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
    const char*
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
    return CommandLineHasValidArgv(argc, argv);
}

template<typename CLI, typename CharT>
inline void ArgParseApp(CLI& cli, const isize argc, CharT** argv){
    CommandLineParseApp(cli, argc, argv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

