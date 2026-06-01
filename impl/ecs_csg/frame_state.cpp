// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ActiveCutter(const CsgCutterComponent& cutter){
    return cutter.active && cutter.shapeType != NAME_NONE;
}

void AddSaturating(u32& inOutValue, const u32 value){
    inOutValue = inOutValue <= Limit<u32>::s_Max - value ? inOutValue + value : Limit<u32>::s_Max;
}

template<typename CutterCountMapT>
void CountActiveCuttersByReceiverGroup(Core::ECS::World& world, CutterCountMapT& outCutterCounts){
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

[[nodiscard]] const CsgReceiverComponent* ResolveReceiverComponent(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    CsgReceiverKind::Enum& outReceiverKind
){
    if(const StaticCsgMeshComponent* receiver = world.tryGetComponent<StaticCsgMeshComponent>(entity)){
        outReceiverKind = CsgReceiverKind::Static;
        return receiver;
    }

    if(const SkinnedCsgMeshComponent* receiver = world.tryGetComponent<SkinnedCsgMeshComponent>(entity)){
        outReceiverKind = CsgReceiverKind::Skinned;
        return receiver;
    }

    return nullptr;
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

[[nodiscard]] bool ReceiverPassEnabled(const CsgReceiverComponent& receiver, const CsgReceiverPass::Enum receiverPass){
    switch(receiverPass){
    case CsgReceiverPass::Opaque: return receiver.affectOpaquePass;
    case CsgReceiverPass::Transparent: return receiver.affectTransparentPass;
    default: return false;
    }
}

void ApplyReceiverWork(
    const CsgReceiverKind::Enum receiverKind,
    const bool generateCaps,
    const bool opaqueWork,
    const bool transparentWork,
    const u32 cutterCount,
    CsgFrameState& inOutState
){
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

    if(generateCaps){
        inOutState.hasOpaqueCapWork = inOutState.hasOpaqueCapWork || opaqueWork;
        inOutState.hasTransparentCapWork = inOutState.hasTransparentCapWork || transparentWork;
    }
}

template<typename ReceiverT>
void GatherReceiverState(
    Core::ECS::World& world,
    const CsgReceiverKind::Enum receiverKind,
    const CsgFrameReceiverLookup& receiverLookup,
    const CsgFrameBuildDesc& desc,
    CsgFrameState& inOutState
){
    auto receiverView = world.view<ReceiverT>();
    receiverView.each(
        [&](const Core::ECS::EntityID entity, ReceiverT& typedReceiver){
            const CsgReceiverComponent& receiver = typedReceiver;
            if(!receiver.enabled)
                return;
            if(!ReceiverVisible(world, entity, receiverKind, receiver, desc))
                return;

            CsgReceiverDrawState opaqueState;
            CsgReceiverDrawState transparentState;
            const bool opaqueWork =
                desc.includeOpaquePass
                && receiverLookup.resolveReceiverDrawState(entity, CsgReceiverPass::Opaque, opaqueState)
                && opaqueState.receiverKind == receiverKind
            ;
            const bool transparentWork =
                desc.includeTransparentPass
                && receiverLookup.resolveReceiverDrawState(entity, CsgReceiverPass::Transparent, transparentState)
                && transparentState.receiverKind == receiverKind
            ;
            if(!opaqueWork && !transparentWork)
                return;

            const CsgReceiverDrawState& receiverState = opaqueWork ? opaqueState : transparentState;
            ApplyReceiverWork(
                receiverKind,
                receiverState.generateCaps,
                opaqueWork,
                transparentWork,
                receiverState.cutterCount,
                inOutState
            );
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameReceiverLookup::CsgFrameReceiverLookup(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena)
    : m_world(world)
    , m_cutterCounts(0, Hasher<Name>(), EqualTo<Name>(), scratchArena)
{
    __hidden_frame_state::CountActiveCuttersByReceiverGroup(m_world, m_cutterCounts);
}


bool CsgFrameReceiverLookup::resolveReceiverDrawState(
    const Core::ECS::EntityID entity,
    const CsgReceiverPass::Enum receiverPass,
    CsgReceiverDrawState& outState
)const{
    outState = CsgReceiverDrawState{};
    if(m_cutterCounts.empty())
        return false;

    CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
    const CsgReceiverComponent* receiver = __hidden_frame_state::ResolveReceiverComponent(m_world, entity, receiverKind);
    if(!receiver || !receiver->enabled || !__hidden_frame_state::ReceiverPassEnabled(*receiver, receiverPass))
        return false;

    const auto foundCutterCount = m_cutterCounts.find(receiver->receiverGroup);
    if(foundCutterCount == m_cutterCounts.end() || foundCutterCount.value() == 0u)
        return false;

    outState.active = true;
    outState.receiverKind = receiverKind;
    outState.generateCaps = receiver->generateCaps;
    outState.cutterCount = foundCutterCount.value();
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameState BuildCsgFrameState(
    Core::ECS::World& world,
    Core::Alloc::ScratchArena& scratchArena,
    const CsgFrameBuildDesc& desc
){
    CsgFrameState state;

    const CsgFrameReceiverLookup receiverLookup(world, scratchArena);
    if(receiverLookup.empty())
        return state;

    __hidden_frame_state::GatherReceiverState<StaticCsgMeshComponent>(
        world,
        CsgReceiverKind::Static,
        receiverLookup,
        desc,
        state
    );
    __hidden_frame_state::GatherReceiverState<SkinnedCsgMeshComponent>(
        world,
        CsgReceiverKind::Skinned,
        receiverLookup,
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

