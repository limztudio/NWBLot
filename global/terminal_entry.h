// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "type_properties.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Entry helper only; keep error-reporting state outside invoke; never transports exceptions.
template<typename Error, typename Invoke, typename ErrorHandler, typename UnexpectedHandler>
[[nodiscard]] inline int InvokeTerminalEntry(Invoke&& invoke, ErrorHandler&& handleError, UnexpectedHandler&& handleUnexpected)noexcept(
    noexcept(DeclVal<int&>() = Forward<ErrorHandler>(handleError)(DeclVal<const Error&>()))
    && noexcept(DeclVal<int&>() = Forward<UnexpectedHandler>(handleUnexpected)())
){
    int result;
    try{
        result = Forward<Invoke>(invoke)();
    }
    catch(const Error& error){
        result = Forward<ErrorHandler>(handleError)(error);
    }
    catch(...){
        result = Forward<UnexpectedHandler>(handleUnexpected)();
    }
    return result;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

