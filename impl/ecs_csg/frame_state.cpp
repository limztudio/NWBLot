// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using CutterCountMap = HashMap<Name, u32, Hasher<Name>, EqualTo<Name>, Core::Alloc::ScratchArena>;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ActiveCutter(const CsgCutterComponent& cutter){
    return cutter.active && cutter.shapeType != NAME_NONE;
}

void AddSaturating(u32& inOutValue, const u32 value){
    inOutValue = inOutValue <= Limit<u32>::s_Max - value ? inOutValue + value : Limit<u32>::s_Max;
}

void CountActiveCuttersByReceiverGroup(Core::ECS::World& world, CutterCountMap& outCutterCounts){
    auto cutterView = world.view<CsgCutterComponent>();
    outCutterCounts.reserve(cutterView.candidateCount());

    cutterView.each(
        [&outCutterCounts](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            static_cast<void>(entity);
            if(!ActiveCutter(cutter))
                return;

            auto result = outCutterCounts.try_emplace(cutter.receiverGroup, 0u);
            AddSaturating(result.first.value(), 1u);
        }
    );
}

[[nodiscard]] bool ReceiverVisible(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const CsgReceiverKind::Enum receiverKind,
    const CsgReceiverComponent& receiver,
    const CsgFrameBuildDesc& desc
){
    if(!desc.receiverVisible)
        return true;

    return desc.receiverVisible(world, entity, receiverKind, receiver, desc.receiverVisibleUserData);
}

[[nodiscard]] bool ReceiverHasPassWork(const CsgReceiverComponent& receiver, const CsgFrameBuildDesc& desc){
    return
        (desc.includeOpaquePass && receiver.affectOpaquePass)
        || (desc.includeTransparentPass && receiver.affectTransparentPass)
    ;
}

void ApplyReceiverWork(
    const CsgReceiverKind::Enum receiverKind,
    const CsgReceiverComponent& receiver,
    const u32 cutterCount,
    const CsgFrameBuildDesc& desc,
    CsgFrameState& inOutState
){
    const bool opaqueWork = desc.includeOpaquePass && receiver.affectOpaquePass;
    const bool transparentWork = desc.includeTransparentPass && receiver.affectTransparentPass;

    AddSaturating(inOutState.receiverCount, 1u);
    AddSaturating(inOutState.cutterCount, cutterCount);

    if(receiverKind == CsgReceiverKind::Static){
        inOutState.hasOpaqueStaticWork = inOutState.hasOpaqueStaticWork || opaqueWork;
        inOutState.hasTransparentStaticWork = inOutState.hasTransparentStaticWork || transparentWork;
    }
    else{
        inOutState.hasOpaqueSkinnedWork = inOutState.hasOpaqueSkinnedWork || opaqueWork;
        inOutState.hasTransparentSkinnedWork = inOutState.hasTransparentSkinnedWork || transparentWork;
    }

    if(receiver.generateCaps){
        inOutState.hasOpaqueCapWork = inOutState.hasOpaqueCapWork || opaqueWork;
        inOutState.hasTransparentCapWork = inOutState.hasTransparentCapWork || transparentWork;
    }
}

template<typename ReceiverT>
void GatherReceiverState(
    Core::ECS::World& world,
    const CsgReceiverKind::Enum receiverKind,
    const CutterCountMap& cutterCounts,
    const CsgFrameBuildDesc& desc,
    CsgFrameState& inOutState
){
    auto receiverView = world.view<ReceiverT>();
    receiverView.each(
        [&](const Core::ECS::EntityID entity, ReceiverT& typedReceiver){
            const CsgReceiverComponent& receiver = typedReceiver;
            if(!receiver.enabled)
                return;
            if(!ReceiverHasPassWork(receiver, desc))
                return;
            if(!ReceiverVisible(world, entity, receiverKind, receiver, desc))
                return;

            const auto foundCutterCount = cutterCounts.find(receiver.receiverGroup);
            if(foundCutterCount == cutterCounts.end() || foundCutterCount.value() == 0u)
                return;

            ApplyReceiverWork(receiverKind, receiver, foundCutterCount.value(), desc, inOutState);
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState BuildCsgFrameState(
    Core::ECS::World& world,
    Core::Alloc::ScratchArena& scratchArena,
    const CsgFrameBuildDesc& desc
){
    CsgFrameState state;

    __hidden_frame_state::CutterCountMap cutterCounts(
        0,
        Hasher<Name>(),
        EqualTo<Name>(),
        scratchArena
    );
    __hidden_frame_state::CountActiveCuttersByReceiverGroup(world, cutterCounts);
    if(cutterCounts.empty())
        return state;

    __hidden_frame_state::GatherReceiverState<StaticCsgMeshComponent>(
        world,
        CsgReceiverKind::Static,
        cutterCounts,
        desc,
        state
    );
    __hidden_frame_state::GatherReceiverState<SkinnedCsgMeshComponent>(
        world,
        CsgReceiverKind::Skinned,
        cutterCounts,
        desc,
        state
    );

    state.hasAnyWork =
        state.hasOpaqueStaticWork
        || state.hasOpaqueSkinnedWork
        || state.hasTransparentStaticWork
        || state.hasTransparentSkinnedWork
        || state.hasOpaqueCapWork
        || state.hasTransparentCapWork
    ;
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

