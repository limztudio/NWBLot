// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <string>
#include <tuple>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


enum class ArgCommand : u8{
    LogServer,

    iNumArg,
};
constexpr const char* g_argCmd[] = {
    "-l,--logserver",
};
const std::tuple<
    std::string
> g_argDefault = {
    "http://localhost:7117",
};
constexpr const char* g_argDesc[] = {
    "Log server address",
};

template <ArgCommand i, typename CLI, typename T>
inline auto argAddOption(CLI& cli, T& arg){
    return cli.add_option(g_argCmd[static_cast<usize>(i)], arg, g_argDesc[static_cast<usize>(i)])->default_val(std::get<static_cast<usize>(i)>(g_argDefault));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

