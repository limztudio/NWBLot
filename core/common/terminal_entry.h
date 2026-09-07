// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "global.h"

#include <global/type_properties.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TerminalErrorExitPolicy{
    enum Enum : u8{
        PreserveHandlerResult,
        ApplicationFailure,
    };
};

// Used only by application entry functions that return immediately with the terminal result. Keep any state
// needed by error reporting outside invoke, so its lifetime continues after the application work unwinds.
// Native worker exceptions terminate at their thread boundary; this helper never transports exceptions.
template<typename Error, typename Invoke, typename ErrorHandler, typename UnexpectedHandler>
[[nodiscard]] inline int InvokeTerminalEntry(
    Invoke&& invoke,
    ErrorHandler&& handleError,
    UnexpectedHandler&& handleUnexpected,
    const TerminalErrorExitPolicy::Enum errorExitPolicy = TerminalErrorExitPolicy::PreserveHandlerResult
){
    try{
        return Forward<Invoke>(invoke)();
    }
    catch(const Error& error){
        const int result = Forward<ErrorHandler>(handleError)(error);
        return errorExitPolicy == TerminalErrorExitPolicy::ApplicationFailure ? -1 : result;
    }
    catch(...){
        return Forward<UnexpectedHandler>(handleUnexpected)();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_COMMON_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

