// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <engine_namespace.h>

#include <global/process_capture.h>
#include <tests/common/memory_sdk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN
namespace Tests{

template<typename StringT>
[[nodiscard]] Expected<void, ProcessCaptureFailure> CaptureForTest(StringT& output, HeapStorage& storage, Span<const AStringView> command, usize limit, usize buffer, usize timeout){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    return ::CaptureProcessOutput(output, storage.arena, command, limit, buffer, timeout);
#else
    (void)output; (void)storage; (void)command; (void)limit; (void)buffer; (void)timeout;
    return MakeUnexpected(ProcessCaptureFailure{});
#endif
}

template<typename StringT>
[[nodiscard]] Expected<void, ProcessCaptureFailure> CaptureDefault(StringT& output, HeapStorage& storage, Span<const AStringView> command){
#if defined(FOUNDATION_PLATFORM_LINUX) && !defined(FOUNDATION_PLATFORM_ANDROID)
    return ::CaptureProcessOutput(output, storage.arena, command);
#else
    (void)output; (void)storage; (void)command;
    return MakeUnexpected(ProcessCaptureFailure{});
#endif
}

template<typename Output>
struct CaptureSink{
    Output& output;
    void clear(){ output.clear(); }
    [[nodiscard]] usize size()const{ return output.size(); }
    void append(const char* data, usize length){ output.append(AStringView(data, length)); }
};

template<typename Output>
[[nodiscard]] Expected<void, ProcessCaptureFailure> CaptureToSink(Output& output, HeapStorage& storage, Span<const AStringView> args, usize limit, usize buffer, usize timeout){
    CaptureSink<Output> sink{ output };
    return CaptureForTest(sink, storage, args, limit, buffer, timeout);
}

};
NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

