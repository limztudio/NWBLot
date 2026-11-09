// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "fbx_cli_diagnostics.h"

#include <core/common/log.h>

#include <CLI.hpp>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_FBX_TO_NWB_BEGIN


i32 FbxCliDiagnostics::ExitParseError(const CLI::App& app, const CLI::ParseError& error, Core::Alloc::ScratchArena& scratchArena){
    if(error.get_exit_code() == s_FbxToNwbExitSuccess)
        return app.exit(error, NWB_COUT, NWB_CERR);

    ScratchStringStream report(s_FileOpenWrite, scratchArena);
    const i32 result = app.exit(error, report, report);
    const ScratchString message = Move(report).str();
    if(message.empty())
        NWB_LOGGER_ERROR(NWB_TEXT("{}"), ConvertString(AStringView(error.what())));
    else
        NWB_LOGGER_ERROR(NWB_TEXT("{}"), ConvertString(message));
    return result;
}

i32 FbxCliDiagnostics::ExitValidation(const CLI::App& app, const AStringView option, const AStringView message, Core::Alloc::ScratchArena& scratchArena){
    return ExitParseError(app, CLI::ValidationError(AInteropString(option.data(), option.size()), AInteropString(message.data(), message.size())), scratchArena);
}


NWB_FBX_TO_NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

