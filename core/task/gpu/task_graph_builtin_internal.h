// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphBuiltinDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ResourceDesc>
[[nodiscard]] inline bool BuiltInTaskCanMaterializeRetainedState(
    const ResourceDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    // Retained resources restore to their descriptor state when a native packet closes. The graph may still use a different built-in state when it explicitly starts from that descriptor state:
    // compiler-owned barriers then establish the primitive state and the recorded packet exports the restored state for the next packet. A mismatched graph declaration has no native source that this helper can prove, and a terminal external state must agree with the close-time restoration before the graph publishes its handoff.
    if(!resourceDesc.keepInitialState)
        return true;
    return resourceDesc.initialState != ResourceStates::Unknown
        && graphInitialState == resourceDesc.initialState
        && (
            externalFinalState == ResourceStates::Unknown
            || externalFinalState == resourceDesc.initialState
        )
    ;
}

inline void PublishAcceptedToken(QueueSubmissionToken* acceptedToken, const QueueSubmissionToken& token){
    if(acceptedToken)
        *acceptedToken = token;
}

inline void ClearAcceptedToken(QueueSubmissionToken* acceptedToken){
    if(acceptedToken)
        *acceptedToken = {};
}

// Shared copies-payload core for builtin copy tasks. CopyBuffer plus CopyTexture share the arena-owned copies vector plus accepted-token lifecycle and differ only in their per-item Copy shape.
template<typename Copy>
struct CopiesPayloadBase{
    explicit CopiesPayloadBase(GraphicsArena& arena)
        : copies(arena)
    {}

    GraphicsVector<Copy> copies;
    QueueSubmissionToken* acceptedToken = nullptr;
};

template<typename Payload>
[[nodiscard]] inline bool CopiesPayloadHasWork(const Payload& payload){
    return !payload.copies.empty();
}

// Shared task skeleton for builtin copy tasks. CopyBuffer plus CopyTexture share the arena-owned payload plus accepted-token lifecycle and differ only in their per-item Copy shape plus record steps.
template<typename Copy>
struct CopiesTaskBase{
    struct Payload : public CopiesPayloadBase<Copy>{
        explicit Payload(GraphicsArena& arena)
            : CopiesPayloadBase<Copy>(arena)
        {}
    };

    static void accepted(Payload& payload, const QueueSubmissionToken& token){
        PublishAcceptedToken(payload.acceptedToken, token);
    }

    static void discarded(Payload& payload){
        ClearAcceptedToken(payload.acceptedToken);
    }
};

// Shared record loop for builtin copy tasks. CopyBuffer plus CopyTexture share the empty-payload guard plus per-item validate, command-IR capture, and native emit sequence and differ only in those per-item steps.
template<typename Payload, typename ValidateCopyFn, typename CaptureCopyFn, typename EmitCopyFn>
[[nodiscard]] inline bool RecordCopies(
    const Payload& payload,
    CommandList& commandList,
    const GpuTaskRecordContext& context,
    ValidateCopyFn&& isCopyValid,
    CaptureCopyFn&& captureCopy,
    EmitCopyFn&& emitCopy
){
    if(!CopiesPayloadHasWork(payload))
        return false;
    for(const auto& copy : payload.copies){
        if(!isCopyValid(copy))
            return false;
        if(context.commandIrCapture && !captureCopy(context, copy))
            return false;
        if(!emitCopy(commandList, copy))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool CopyOrClearTextureDestinationCanMaterializeRetainedState(
    const TextureDesc& resourceDesc,
    const ResourceStates::Mask graphInitialState,
    const ResourceStates::Mask externalFinalState
)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(
        resourceDesc.initialState == ResourceStates::Unknown
        || (
            externalFinalState != ResourceStates::Unknown
            && externalFinalState != resourceDesc.initialState
        )
    )
        return false;
    // An Unknown write-only destination never invents an input state. Fresh managed subresources lower from Undefined; accepted retained subresources are restored to descriptor state at packet close and reused by StateTracker on later packets.
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

