// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <utility>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Entry helper only; keep error-reporting state outside invoke; never transports exceptions.
template<typename Error, typename Invoke, typename ErrorHandler, typename UnexpectedHandler>
[[nodiscard]] inline int InvokeTerminalEntry(
    Invoke&& invoke,
    ErrorHandler&& handleError,
    UnexpectedHandler&& handleUnexpected
){
    int result;
    try{
        result = std::forward<Invoke>(invoke)();
    }
    catch(const Error& error){
        result = std::forward<ErrorHandler>(handleError)(error);
    }
    catch(...){
        result = std::forward<UnexpectedHandler>(handleUnexpected)();
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

