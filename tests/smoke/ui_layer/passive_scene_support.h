// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "marker_positions.h"
#include "smoke_geometry.h"

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct PassiveSmokeColumns{
    f32 mainWidth = 0.0f;
    f32 rightColumn = 0.0f;
    f32 rightWidth = 0.0f;
};

struct PassiveSmokeTargetObservation{
    u32 focusCode = 0u;
    u32 passiveTargets = 0u;
};

struct PassiveSmokeSnapshotUpdate{
    b8 published = false;
    b8 displayChanged = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] PassiveSmokeColumns ResolvePassiveSmokeColumns(const Impl::Ui::DisplayMetrics& display)noexcept;

template<typename RectanglesT, typename IncludeIndexT>
[[nodiscard]] inline PassiveSmokeTargetObservation ObservePassiveSmokeTargets(
    const Impl::Ui::InputRouter& input,
    const u64 parentGeneration,
    const u64 childGeneration,
    const usize beginIndex,
    const usize endIndex,
    const IncludeIndexT& includeIndex,
    RectanglesT& rectangles
)noexcept(
    noexcept(rectangles[DeclVal<u32&>()] = DeclVal<const Impl::Ui::Rect&>())
    && noexcept(static_cast<b8>(!includeIndex(DeclVal<usize&>()) || rectangles[DeclVal<usize&>()].width == 0.0f))
    && noexcept(SameSmokeRect(DeclVal<const Impl::Ui::Rect&>(), rectangles[DeclVal<usize&>()]))
){
    PassiveSmokeTargetObservation observation;
    u32 rootButton = 0u;
    for(const Impl::Ui::HitTarget& target : input.targets()){
        u32 code = 0u;
        if(target.activatable && !target.popup.valid() && rootButton < 2u){
            rectangles[rootButton] = target.rectangle;
            code = rootButton + 1u;
            ++rootButton;
        }
        else if(target.activatable && target.popup.instanceGeneration == parentGeneration)
            code = 3u;
        else if(target.activatable && target.popup.instanceGeneration == childGeneration)
            code = 4u;
        if(target.id == input.focus())
            observation.focusCode = code;
        for(usize index = beginIndex; index < endIndex; ++index){
            if(!includeIndex(index) || rectangles[index].width == 0.0f)
                continue;
            if(SameSmokeRect(target.rectangle, rectangles[index]))
                ++observation.passiveTargets;
        }
    }
    return observation;
}

template<typename SnapshotT, typename StateChangedT>
[[nodiscard]] inline PassiveSmokeSnapshotUpdate PublishPassiveSmokeSnapshot(const StateChangedT& stateChanged, SnapshotT& current, SnapshotT& snapshot)noexcept(
    noexcept(DeclVal<void(*)(const Impl::Ui::DisplayMetrics&)noexcept>()(snapshot.display))
    && noexcept(DeclVal<void(*)(b8)noexcept>()(
        current.display.logicalWidth != DeclVal<const Impl::Ui::DisplayMetrics&>().logicalWidth
        || current.display.logicalHeight != DeclVal<const Impl::Ui::DisplayMetrics&>().logicalHeight
        || current.display.pixelScaleX != DeclVal<const Impl::Ui::DisplayMetrics&>().pixelScaleX
        || current.display.pixelScaleY != DeclVal<const Impl::Ui::DisplayMetrics&>().pixelScaleY
    ))
    && noexcept(DeclVal<void(*)(b8)noexcept>()(stateChanged()))
    && noexcept(static_cast<b8>(DeclVal<usize&>() < current.rectangles.size()))
    && noexcept(SameSmokeRect(current.rectangles[DeclVal<usize&>()], snapshot.rectangles[DeclVal<usize&>()]))
    && noexcept(static_cast<b8>(snapshot.sequence != 0u && !b8{} && !b8{}))
    && noexcept(current.sequence = snapshot.sequence + 1u)
    && noexcept(snapshot = current)
){
    const Impl::Ui::DisplayMetrics& previous = snapshot.display;
    const b8 displayChanged = current.display.logicalWidth != previous.logicalWidth || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX || current.display.pixelScaleY != previous.pixelScaleY;
    b8 changed = stateChanged();
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], snapshot.rectangles[index]);
    if(snapshot.sequence != 0u && !changed && !displayChanged)
        return {};
    current.sequence = snapshot.sequence + 1u;
    snapshot = current;
    return { true, displayChanged };
}

[[nodiscard]] b8 HandlePassiveSmokeKeyboard(i32 key, i32 action, b8& phase, Impl::Ui::PopupState& parent, Impl::Ui::PopupState& child)noexcept;

template<typename ValuesT>
inline void PaintPassiveSmokeValueMarkers(const ValuesT& values, const u64 sequence, Impl::UiPaintContext& context){
    PaintKeyedSmokeValueMarkers<12u, 8u, 8u, 18u>(values, sequence, context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

