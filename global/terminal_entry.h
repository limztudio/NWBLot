// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <cstdint>
#include <utility>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace TerminalErrorExitPolicy{
    enum Enum : std::uint8_t{
        PreserveHandlerResult,
        ApplicationFailure,
    };
};

// Entry helper only; keep error-reporting state outside invoke; never transports exceptions.
template<typename Error, typename Invoke, typename ErrorHandler, typename UnexpectedHandler>
[[nodiscard]] inline int InvokeTerminalEntry(
    Invoke&& invoke,
    ErrorHandler&& handleError,
    UnexpectedHandler&& handleUnexpected,
    const TerminalErrorExitPolicy::Enum errorExitPolicy = TerminalErrorExitPolicy::PreserveHandlerResult
){
    try{
        return std::forward<Invoke>(invoke)();
    }
    catch(const Error& error){
        const int result = std::forward<ErrorHandler>(handleError)(error);
        return errorExitPolicy == TerminalErrorExitPolicy::ApplicationFailure ? -1 : result;
    }
    catch(...){
        return std::forward<UnexpectedHandler>(handleUnexpected)();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

