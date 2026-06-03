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
            AddCsgFrameReceiverWork(
                inOutState,
                receiverKind,
                receiverState.generateCaps,
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

            auto result = m_cutterRanges.try_emplace(cutter.receiverGroup, CsgFrameCutterRange{});
            __hidden_frame_state::AddSaturating(result.first.value().cutterCount, 1u);
        }
    );

    u32 firstCutter = 0u;
    for(auto it = m_cutterRanges.begin(); it != m_cutterRanges.end(); ++it){
        CsgFrameCutterRange& range = it.value();
        range.firstCutter = firstCutter;
        __hidden_frame_state::AddSaturating(firstCutter, range.cutterCount);
    }

    if(firstCutter == 0u)
        return;

    m_cutterRefs.resize(static_cast<usize>(firstCutter));
    CutterWriteCountMap writtenCounts(0, Hasher<Name>(), EqualTo<Name>(), scratchArena);
    writtenCounts.reserve(m_cutterRanges.size());

    cutterView.each(
        [&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            if(!__hidden_frame_state::ActiveCutter(cutter))
                return;

            const auto foundRange = m_cutterRanges.find(cutter.receiverGroup);
            if(foundRange == m_cutterRanges.end())
                return;

            auto writtenResult = writtenCounts.try_emplace(cutter.receiverGroup, 0u);
            u32& writtenCount = writtenResult.first.value();
            const CsgFrameCutterRange& range = foundRange.value();
            if(writtenCount >= range.cutterCount)
                return;

            const usize cutterIndex = static_cast<usize>(range.firstCutter + writtenCount);
            NWB_ASSERT(cutterIndex < m_cutterRefs.size());
            m_cutterRefs[cutterIndex] = CsgFrameCutterRef{ entity, &cutter };
            ++writtenCount;
        }
    );
}


bool CsgFrameReceiverLookup::resolveReceiverDrawState(
    const Core::ECS::EntityID entity,
    const CsgReceiverPass::Enum receiverPass,
    CsgReceiverDrawState& outState
)const{
    outState = CsgReceiverDrawState{};
    if(m_cutterRanges.empty())
        return false;

    CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
    const CsgReceiverComponent* receiver = ResolveCsgReceiverComponent(m_world, entity, receiverKind);
    if(!receiver || !receiver->enabled || !__hidden_frame_state::ReceiverPassEnabled(*receiver, receiverPass))
        return false;

    const auto foundCutterRange = m_cutterRanges.find(receiver->receiverGroup);
    if(foundCutterRange == m_cutterRanges.end() || foundCutterRange.value().cutterCount == 0u)
        return false;

    outState.active = true;
    outState.receiverKind = receiverKind;
    outState.generateCaps = receiver->generateCaps;
    outState.cutterCount = foundCutterRange.value().cutterCount;
    return true;
}

bool CsgFrameReceiverLookup::resolveReceiverCutterRange(
    const Core::ECS::EntityID entity,
    CsgFrameCutterRange& outRange
)const{
    outRange = CsgFrameCutterRange{};
    if(m_cutterRanges.empty())
        return false;

    CsgReceiverKind::Enum receiverKind = CsgReceiverKind::Static;
    const CsgReceiverComponent* receiver = ResolveCsgReceiverComponent(m_world, entity, receiverKind);
    if(!receiver || !receiver->enabled)
        return false;

    const auto foundCutterRange = m_cutterRanges.find(receiver->receiverGroup);
    if(foundCutterRange == m_cutterRanges.end() || foundCutterRange.value().cutterCount == 0u)
        return false;

    outRange = foundCutterRange.value();
    return true;
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

const CsgReceiverComponent* ResolveCsgReceiverComponent(
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

void AddCsgFrameReceiverWork(
    CsgFrameState& inOutState,
    const CsgReceiverKind::Enum receiverKind,
    const bool generateCaps,
    const bool opaqueWork,
    const bool transparentWork,
    const u32 cutterCount
){
    __hidden_frame_state::AddSaturating(inOutState.receiverCount, 1u);
    __hidden_frame_state::AddSaturating(inOutState.cutterCount, cutterCount);

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

void FinalizeCsgFrameState(CsgFrameState& inOutState){
    inOutState.hasAnyWork =
        inOutState.hasOpaqueStaticWork
        || inOutState.hasOpaqueSkinnedWork
        || inOutState.hasTransparentStaticWork
        || inOutState.hasTransparentSkinnedWork
        || inOutState.hasOpaqueCapWork
        || inOutState.hasTransparentCapWork
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

