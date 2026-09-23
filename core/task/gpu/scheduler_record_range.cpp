// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "scheduler.h"

#include "packet_runtime_internal.h"

#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool GpuTaskScheduler::recordPacketRange(
    const GpuTaskGraph& graph,
    const GpuCompiledGraph& compiledGraph,
    const GpuNativePacketRecorder& recorder,
    const GpuSubmissionPacketRange& range,
    GpuRecordedGraph& recordedGraph,
    CpuTaskScheduler* const readyFrontierScheduler,
    GpuCommandIrCapture* const commandIrCapture,
    GpuSubmissionPacketId* const outFailedPacket
)const{
    return readyFrontierScheduler
        ? recorder.recordPacketRangeInReadyFrontiers(
            graph,
            compiledGraph,
            range,
            recordedGraph,
            *readyFrontierScheduler,
            outFailedPacket,
            commandIrCapture
        )
        : recorder.recordPacketRangeInCompileOrder(
            graph,
            compiledGraph,
            range,
            recordedGraph,
            outFailedPacket,
            commandIrCapture
        )
    ;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

