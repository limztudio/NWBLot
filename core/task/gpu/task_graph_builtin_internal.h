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
[[nodiscard]] inline bool BuiltInTaskCanMaterializeRetainedState(const ResourceDesc& resourceDesc, const ResourceStates::Mask graphInitialState, const ResourceStates::Mask externalFinalState)noexcept{
    // Retained resources restore to their descriptor state when a native packet closes.
    // The graph may still use a different built-in state when it starts from that descriptor state: compiler-owned barriers establish the primitive state and the packet exports the restored one.
    // A mismatched declaration has no native source that this helper can prove, and a terminal external state must agree with the close-time restoration before the graph publishes its handoff.
    if(!resourceDesc.keepInitialState)
        return true;
    return resourceDesc.initialState != ResourceStates::Unknown && graphInitialState == resourceDesc.initialState && (externalFinalState == ResourceStates::Unknown || externalFinalState == resourceDesc.initialState);
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
    GraphicsVector<Copy> copies;
    QueueSubmissionToken* acceptedToken = nullptr;


public:
    explicit CopiesPayloadBase(GraphicsArena& arena)noexcept
        : copies(arena)
    {}
};

template<typename Payload>
[[nodiscard]] inline bool CopiesPayloadHasWork(const Payload& payload)noexcept(noexcept(static_cast<bool>(!payload.copies.empty()))){
    return !payload.copies.empty();
}

template<typename PayloadT>
struct SingletonTokenTaskBase{
    using Payload = PayloadT;


public:
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
        explicit Payload(GraphicsArena& arena)noexcept
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

template<typename Payload, typename CaptureCopyFn, typename EmitCopyFn>
[[nodiscard]] inline bool RecordCopies(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context, CaptureCopyFn&& captureCopy, EmitCopyFn&& emitCopy)noexcept(
    noexcept(CopiesPayloadHasWork(payload))
    && []()constexpr noexcept{
        using Range = RemoveReference_T<decltype((DeclVal<const Payload&>().copies))>;
        if constexpr(requires(Range& values){ values.begin(); values.end(); }){
            using Iterator = RemoveCVRef_T<decltype(DeclVal<Range&>().begin())>;
            using Sentinel = RemoveCVRef_T<decltype(DeclVal<Range&>().end())>;
            using Returned = decltype(*DeclVal<Iterator&>());
            using Item = const RemoveReference_T<Returned>&;
            return
                noexcept(DeclVal<void(*)(Iterator)noexcept>()(DeclVal<Range&>().begin()))
                && noexcept(DeclVal<void(*)(Sentinel)noexcept>()(DeclVal<Range&>().end()))
                && IsNothrowDestructible_V<Iterator>
                && IsNothrowDestructible_V<Sentinel>
                && noexcept(static_cast<bool>(DeclVal<Iterator&>() != DeclVal<Sentinel&>()))
                && noexcept(++DeclVal<Iterator&>())
                && noexcept(*DeclVal<Iterator&>())
                && noexcept(DeclVal<void(*)(Item)noexcept>()(*DeclVal<Iterator&>()))
                && noexcept(static_cast<bool>(!DeclVal<CaptureCopyFn&>()(DeclVal<const GpuTaskRecordContext&>(), DeclVal<Item>())))
                && noexcept(static_cast<bool>(!DeclVal<EmitCopyFn&>()(DeclVal<CommandList&>(), DeclVal<Item>())))
            ;
        }
        else
            return false;
    }()
){
    NWB_ASSERT(CopiesPayloadHasWork(payload));
    for(const auto& copy : payload.copies){
        if(context.commandIrCapture && !captureCopy(context, copy))
            return false;
        if(!emitCopy(commandList, copy))
            return false;
    }
    return true;
}

[[nodiscard]] inline bool CopyOrClearTextureDestinationCanMaterializeRetainedState(const TextureDesc& resourceDesc, const ResourceStates::Mask graphInitialState, const ResourceStates::Mask externalFinalState)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(resourceDesc.initialState == ResourceStates::Unknown || (externalFinalState != ResourceStates::Unknown && externalFinalState != resourceDesc.initialState))
        return false;
    // An Unknown write-only destination never invents an input state. Fresh managed subresources lower from Undefined
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] bool UploadTextureTaskCanMaterializeRetainedState(const TextureDesc& resourceDesc, ResourceStates::Mask graphInitialState, ResourceStates::Mask externalFinalState, ResourceStates::Mask uploadFinalState)noexcept;

[[nodiscard]] Expected<usize> ComputeTextureUploadByteSize(const TextureDesc& textureDesc, u32 arraySlice, u32 mipLevel, usize rowPitch, usize depthPitch, TextureUploadAspect::Enum aspect)noexcept;

[[nodiscard]] inline bool BuiltinDeclarationHasNoCallerResourceUses(const GpuTaskDesc& desc)noexcept{
    return !desc.resourceUses && desc.resourceUseCount == 0u && !desc.resourceSetUses && desc.resourceSetUseCount == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

