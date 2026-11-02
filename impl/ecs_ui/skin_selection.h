// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>
#include <impl/ecs_ui/skin_selection.adhd.h>

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


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

