// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_ui/components.adhd.h>

#include <core/assets/ref.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiSkin;

// Commits a skin identity only after its complete asset and GPU binding succeeds.
template<typename TTryBind>
[[nodiscard]] UiSkinSelectionResult::Enum EnsureUiSkinSelection(
    UiSkinSelectionState& state,
    const Core::Assets::AssetRef<UiSkin>& basicSkin,
    TTryBind&& tryBind
)noexcept(noexcept(static_cast<b8>(tryBind(state.selected))) && noexcept(static_cast<b8>(tryBind(basicSkin))) && noexcept(static_cast<b8>(true || !tryBind(basicSkin)))){
    if(state.selected.valid())
        return tryBind(state.selected) ? UiSkinSelectionResult::Selected : UiSkinSelectionResult::Failed;
    if(!basicSkin.valid())
        return UiSkinSelectionResult::Failed;

    const Core::Assets::AssetRef<UiSkin> preferred = state.requested.valid() ? state.requested : basicSkin;
    if(tryBind(preferred)){
        state.selected = preferred;
        return UiSkinSelectionResult::Selected;
    }
    if(preferred == basicSkin || !tryBind(basicSkin))
        return UiSkinSelectionResult::Failed;
    state.selected = basicSkin;
    return UiSkinSelectionResult::DefaultFallback;
}

template<typename TTryBind>
[[nodiscard]] UiSkinChangeResult::Enum ApplyUiSkinSelectionChangeIfReady(
    UiSkinSelectionState& state,
    const b8 resourcesReady,
    const b8 gpuFramePending,
    const b8 layoutPending,
    const Core::Assets::AssetRef<UiSkin>& basicSkin,
    TTryBind&& tryBind
)noexcept(noexcept(static_cast<b8>(!tryBind(basicSkin, DeclVal<const u64&>())))){
    if(!state.changePending || !resourcesReady || gpuFramePending || layoutPending)
        return UiSkinChangeResult::Deferred;
    state.changePending = false;
    const Core::Assets::AssetRef<UiSkin> candidate = state.requestedChange.valid() ? state.requestedChange : basicSkin;
    if(!state.selected.valid() || !candidate.valid() || state.generation == Limit<u64>::s_Max){
        state.changeFailed = true;
        return UiSkinChangeResult::Failed;
    }
    const u64 nextGeneration = state.generation + 1u;
    if(!tryBind(candidate, nextGeneration)){
        state.changeFailed = true;
        return UiSkinChangeResult::Failed;
    }
    state.selected = candidate;
    state.generation = nextGeneration;
    state.changeFailed = false;
    return UiSkinChangeResult::Applied;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

