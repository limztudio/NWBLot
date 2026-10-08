// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_state.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_state{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] bool ActiveCutter(const CsgCutterComponent& cutter)noexcept{
    return cutter.active && cutter.shapeType != s_NameNone;
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

[[nodiscard]] bool ReceiverPassEnabled(const CsgReceiverComponent& receiver, const CsgReceiverPass::Enum receiverPass)noexcept{
    switch(receiverPass){
    case CsgReceiverPass::Opaque: return receiver.affectOpaquePass;
    case CsgReceiverPass::Transparent: return receiver.affectTransparentPass;
    default: return false;
    }
}

template<typename ComponentT>
[[nodiscard]] bool HasComponentCandidates(Core::ECS::World& world){
    return world.view<ComponentT>().candidateCount() > 0u;
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

            const auto opaqueState = desc.includeOpaquePass
                ? receiverLookup.resolveReceiverDrawState(entity, CsgReceiverPass::Opaque)
                : Expected<CsgReceiverDrawState>(MakeUnexpected(Failure{}))
            ;
            const auto transparentState = desc.includeTransparentPass
                ? receiverLookup.resolveReceiverDrawState(entity, CsgReceiverPass::Transparent)
                : Expected<CsgReceiverDrawState>(MakeUnexpected(Failure{}))
            ;
            const bool opaqueWork = opaqueState && opaqueState->receiverKind == receiverKind;
            const bool transparentWork = transparentState && transparentState->receiverKind == receiverKind;
            if(!opaqueWork && !transparentWork)
                return;

            const CsgReceiverDrawState& receiverState = opaqueWork ? *opaqueState : *transparentState;
            AddCsgFrameReceiverWork(
                inOutState,
                receiverKind,
                opaqueWork,
                transparentWork,
                receiverState.cutterCount
            );
        }
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


CsgFrameReceiverLookup::CsgFrameReceiverLookup(Core::ECS::World& world, Core::Alloc::ScratchArena& scratchArena)
    : m_world(world)
    , m_cutterRanges(0, Hasher<Name>(), EqualTo<Name>(), scratchArena)
    , m_cutterRefs(scratchArena)
{
    auto cutterView = m_world.view<CsgCutterComponent>();
    m_cutterRanges.reserve(cutterView.candidateCount());

    cutterView.each(
        [&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            static_cast<void>(entity);
            if(!__hidden_frame_state::ActiveCutter(cutter))
                return;

            auto result = m_cutterRanges.try_emplace(cutter.receiverGroup, CutterRangeEntry{});
            CsgFrameCutterRange& range = result.first.value().range;
            range.cutterCount = AddSaturating<u32>(range.cutterCount, 1u);
        }
    );

    u32 firstCutter = 0u;
    for(auto it = m_cutterRanges.begin(); it != m_cutterRanges.end(); ++it){
        CsgFrameCutterRange& range = it.value().range;
        range.firstCutter = firstCutter;
        firstCutter = AddSaturating<u32>(firstCutter, range.cutterCount);
    }

    if(firstCutter == 0u)
        return;

    m_cutterRefs.resize(static_cast<usize>(firstCutter));

    cutterView.each(
        [&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            if(!__hidden_frame_state::ActiveCutter(cutter))
                return;

            const auto foundRange = m_cutterRanges.find(cutter.receiverGroup);
            if(foundRange == m_cutterRanges.end())
                return;

            CutterRangeEntry& entry = foundRange.value();
            u32& writtenCount = entry.writtenCount;
            const CsgFrameCutterRange& range = entry.range;
            if(writtenCount >= range.cutterCount)
                return;

            const usize cutterIndex = static_cast<usize>(range.firstCutter + writtenCount);
            NWB_ASSERT(cutterIndex < m_cutterRefs.size());
            m_cutterRefs[cutterIndex] = CsgFrameCutterRef{ entity, &cutter };
            ++writtenCount;
        }
    );
}


Expected<CsgReceiverDrawState> CsgFrameReceiverLookup::resolveReceiverDrawState(
    const Core::ECS::EntityID entity,
    const CsgReceiverPass::Enum receiverPass
)const{
    if(m_cutterRanges.empty())
        return MakeUnexpected(Failure{});

    const auto resolvedReceiver = ResolveCsgReceiverComponent(m_world, entity);
    if(!resolvedReceiver)
        return MakeUnexpected(Failure{});
    const CsgReceiverComponent* receiver = resolvedReceiver->receiver;
    if(!receiver || !receiver->enabled || !__hidden_frame_state::ReceiverPassEnabled(*receiver, receiverPass))
        return MakeUnexpected(Failure{});

    const auto foundCutterRange = m_cutterRanges.find(receiver->receiverGroup);
    if(foundCutterRange == m_cutterRanges.end() || foundCutterRange.value().range.cutterCount == 0u)
        return MakeUnexpected(Failure{});

    CsgReceiverDrawState state;
    state.active = true;
    state.receiverKind = resolvedReceiver->receiverKind;
    state.firstCutter = foundCutterRange.value().range.firstCutter;
    state.cutterCount = foundCutterRange.value().range.cutterCount;
    return state;
}

Expected<CsgFrameCutterRange> CsgFrameReceiverLookup::resolveReceiverCutterRange(
    const Core::ECS::EntityID entity
)const{
    if(m_cutterRanges.empty())
        return MakeUnexpected(Failure{});

    const auto resolvedReceiver = ResolveCsgReceiverComponent(m_world, entity);
    if(!resolvedReceiver)
        return MakeUnexpected(Failure{});
    const CsgReceiverComponent* receiver = resolvedReceiver->receiver;
    if(!receiver || !receiver->enabled)
        return MakeUnexpected(Failure{});

    const auto foundCutterRange = m_cutterRanges.find(receiver->receiverGroup);
    if(foundCutterRange == m_cutterRanges.end() || foundCutterRange.value().range.cutterCount == 0u)
        return MakeUnexpected(Failure{});

    return foundCutterRange.value().range;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool HasCsgFrameCandidates(Core::ECS::World& world){
    return
        __hidden_frame_state::HasComponentCandidates<CsgCutterComponent>(world)
        && (
            __hidden_frame_state::HasComponentCandidates<StaticCsgMeshComponent>(world)
            || __hidden_frame_state::HasComponentCandidates<SkinnedCsgMeshComponent>(world)
        )
    ;
}

Expected<CsgResolvedReceiver> ResolveCsgReceiverComponent(Core::ECS::World& world, const Core::ECS::EntityID entity){
    if(const StaticCsgMeshComponent* receiver = world.tryGetComponent<StaticCsgMeshComponent>(entity))
        return CsgResolvedReceiver{ receiver, CsgReceiverKind::Static };
    if(const SkinnedCsgMeshComponent* receiver = world.tryGetComponent<SkinnedCsgMeshComponent>(entity))
        return CsgResolvedReceiver{ receiver, CsgReceiverKind::Skinned };
    return MakeUnexpected(Failure{});
}

void AddCsgFrameReceiverWork(
    CsgFrameState& inOutState,
    const CsgReceiverKind::Enum receiverKind,
    const bool opaqueWork,
    const bool transparentWork,
    const u32 cutterCount
){
    inOutState.receiverCount = AddSaturating<u32>(inOutState.receiverCount, 1u);
    inOutState.cutterCount = AddSaturating<u32>(inOutState.cutterCount, cutterCount);

    if(receiverKind == CsgReceiverKind::Static){
        inOutState.hasOpaqueStaticWork = inOutState.hasOpaqueStaticWork || opaqueWork;
        inOutState.hasTransparentStaticWork = inOutState.hasTransparentStaticWork || transparentWork;
    }
    else{
        inOutState.hasOpaqueSkinnedWork = inOutState.hasOpaqueSkinnedWork || opaqueWork;
        inOutState.hasTransparentSkinnedWork = inOutState.hasTransparentSkinnedWork || transparentWork;
    }
}

void FinalizeCsgFrameState(CsgFrameState& inOutState)noexcept{
    inOutState.hasAnyWork =
        inOutState.hasOpaqueStaticWork
        || inOutState.hasOpaqueSkinnedWork
        || inOutState.hasTransparentStaticWork
        || inOutState.hasTransparentSkinnedWork
    ;
}

CsgFrameState BuildCsgFrameState(
    Core::ECS::World& world,
    Core::Alloc::ScratchArena& scratchArena,
    const CsgFrameBuildDesc& desc
){
    CsgFrameState state;

    if(!HasCsgFrameCandidates(world))
        return state;

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

    FinalizeCsgFrameState(state);
    return state;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

