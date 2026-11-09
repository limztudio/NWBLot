// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "task_graph.h"

#include <global/range_traits.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace GpuTaskGraphBuiltinDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ResourceDesc>
[[nodiscard]] inline b8 BuiltInTaskCanMaterializeRetainedState(const ResourceDesc& resourceDesc, const ResourceStates::Mask graphInitialState, const ResourceStates::Mask externalFinalState)noexcept{
    // Retained resources restore to descriptor state at packet close. Built-ins may start from a different state via compiler barriers.
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
[[nodiscard]] inline b8 CopiesPayloadHasWork(const Payload& payload)noexcept(noexcept(static_cast<b8>(!payload.copies.empty()))){
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
struct CopiesTaskBase : public SingletonTokenTaskBase<CopiesPayloadBase<Copy>>{
    struct Payload : public CopiesPayloadBase<Copy>{
        explicit Payload(GraphicsArena& arena)noexcept
            : CopiesPayloadBase<Copy>(arena)
        {}
    };
};

template<typename Payload, typename CaptureCopyFn, typename EmitCopyFn>
[[nodiscard]] inline b8 RecordCopies(const Payload& payload, CommandList& commandList, const GpuTaskRecordContext& context, CaptureCopyFn&& captureCopy, EmitCopyFn&& emitCopy)noexcept(
    noexcept(CopiesPayloadHasWork(payload))
    && []()constexpr noexcept{
        using Range = RemoveReference_T<decltype((DeclVal<const Payload&>().copies))>;
        if constexpr(requires(Range& values){ values.begin(); values.end(); }){
            using Traits = RangeTraits<Range>;
            using Item = typename Traits::Item;
            return
                Traits::IsNothrowTraversal(false)
                && noexcept(static_cast<b8>(!DeclVal<CaptureCopyFn&>()(DeclVal<const GpuTaskRecordContext&>(), DeclVal<Item>())))
                && noexcept(static_cast<b8>(!DeclVal<EmitCopyFn&>()(DeclVal<CommandList&>(), DeclVal<Item>())))
            ;
        }
        else
            return false;
    }()
){
    FOUNDATION_ASSERT(CopiesPayloadHasWork(payload));
    for(const auto& copy : payload.copies){
        if(context.commandIrCapture && !captureCopy(context, copy))
            return false;
        if(!emitCopy(commandList, copy))
            return false;
    }
    return true;
}

[[nodiscard]] inline b8 CopyOrClearTextureDestinationCanMaterializeRetainedState(const TextureDesc& resourceDesc, const ResourceStates::Mask graphInitialState, const ResourceStates::Mask externalFinalState)noexcept{
    if(!resourceDesc.keepInitialState)
        return true;
    if(resourceDesc.initialState == ResourceStates::Unknown || (externalFinalState != ResourceStates::Unknown && externalFinalState != resourceDesc.initialState))
        return false;
    // An Unknown write-only destination never invents an input state. Fresh managed subresources lower from Undefined.
    return graphInitialState == ResourceStates::Unknown || graphInitialState == resourceDesc.initialState;
}

[[nodiscard]] b8 UploadTextureTaskCanMaterializeRetainedState(const TextureDesc& resourceDesc, ResourceStates::Mask graphInitialState, ResourceStates::Mask externalFinalState, ResourceStates::Mask uploadFinalState)noexcept;

[[nodiscard]] inline b8 BuiltinDeclarationHasNoCallerResourceUses(const GpuTaskDesc& desc)noexcept{
    return !desc.resourceUses && desc.resourceUseCount == 0u && !desc.resourceSetUses && desc.resourceSetUseCount == 0u;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_CORE_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

