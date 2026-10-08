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

inline void PublishAcceptedToken(QueueSubmissionToken* acceptedToken, const QueueSubmissionToken& token)noexcept{
    if(acceptedToken)
        *acceptedToken = token;
}

inline void ClearAcceptedToken(QueueSubmissionToken* acceptedToken)noexcept{
    if(acceptedToken)
        *acceptedToken = {};
}

template<typename Copy>
struct CopiesPayloadBase{
    explicit CopiesPayloadBase(GraphicsArena& arena)
        : copies(arena)
    {}

    GraphicsVector<Copy> copies;
    QueueSubmissionToken* acceptedToken = nullptr;
};

template<typename Payload>
[[nodiscard]] inline bool CopiesPayloadHasWork(const Payload& payload)noexcept(noexcept(static_cast<bool>(!payload.copies.empty()))){
    return !payload.copies.empty();
}

template<typename PayloadT>
struct SingletonTokenTaskBase{
    using Payload = PayloadT;

    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept(noexcept(PublishAcceptedToken(payload.acceptedToken, token))){
        PublishAcceptedToken(payload.acceptedToken, token);
    }

    static void Discarded(Payload& payload)noexcept(noexcept(ClearAcceptedToken(payload.acceptedToken))){
        ClearAcceptedToken(payload.acceptedToken);
    }
};

template<typename Copy>
struct CopiesTaskBase{
    struct Payload : public CopiesPayloadBase<Copy>{
        explicit Payload(GraphicsArena& arena)
            : CopiesPayloadBase<Copy>(arena)
        {}
    };

    static void Accepted(Payload& payload, const QueueSubmissionToken& token)noexcept(noexcept(PublishAcceptedToken(payload.acceptedToken, token))){
        PublishAcceptedToken(payload.acceptedToken, token);
    }

    static void Discarded(Payload& payload)noexcept(noexcept(ClearAcceptedToken(payload.acceptedToken))){
        ClearAcceptedToken(payload.acceptedToken);
    }
};

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
    // An Unknown write-only destination never invents an input state. Fresh managed subresources lower from Undefined
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] bool UploadTextureTaskCanMaterializeRetainedState(
    const TextureDesc& resourceDesc,
    ResourceStates::Mask graphInitialState,
    ResourceStates::Mask externalFinalState,
    ResourceStates::Mask uploadFinalState
)noexcept;

[[nodiscard]] Expected<usize> ComputeTextureUploadByteSize(
    const TextureDesc& textureDesc,
    u32 arraySlice,
    u32 mipLevel,
    usize rowPitch,
    usize depthPitch,
    TextureUploadAspect::Enum aspect
)noexcept;

[[nodiscard]] inline bool BuiltinDeclarationHasNoCallerResourceUses(const GpuTaskDesc& desc)noexcept{
    return !desc.resourceUses && desc.resourceUseCount == 0u && !desc.resourceSetUses && desc.resourceSetUseCount == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

