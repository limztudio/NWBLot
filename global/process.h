// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "platform.h"
#include "basic_string.h"

#include <cstdlib>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline int RunSystemCommand(const char* command){
    return std::system(command);
}

template<typename ArenaT>
[[nodiscard]] inline int RunSystemCommand(const AString<ArenaT>& command){
#if defined(NWB_PLATFORM_WINDOWS)
    AString<ArenaT> systemCommand("\"", command.get_allocator());
    systemCommand += command;
    systemCommand += '"';
    return RunSystemCommand(systemCommand.c_str());
#else
    return RunSystemCommand(command.c_str());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

