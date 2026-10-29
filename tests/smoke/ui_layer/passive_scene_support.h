// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "marker_positions.h"
#include "smoke_geometry.h"

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


struct PassiveSmokeColumns{
    f32 mainWidth = 0.0f;
    f32 rightColumn = 0.0f;
    f32 rightWidth = 0.0f;
};

[[nodiscard]] inline PassiveSmokeColumns ResolvePassiveSmokeColumns(const Impl::Ui::DisplayMetrics& display)noexcept{
    const SIMDVectorSingle logicalWidth = VectorReplicate(display.logicalWidth);
    const SIMDVectorSingle mainWidth = VectorMax(VectorReplicate(300.0f), VectorMultiplyAddExpression(logicalWidth, VectorReplicate(0.5f), VectorNegate(VectorReplicate(40.0f))));
    const SIMDVectorSingle rightColumn = VectorAdd(VectorAdd(VectorReplicate(24.0f), mainWidth), VectorReplicate(32.0f));
    const SIMDVectorSingle rightWidth = VectorMax(VectorReplicate(220.0f), VectorSubtract(VectorSubtract(logicalWidth, rightColumn), VectorReplicate(24.0f)));
    return { VectorGetX(mainWidth), VectorGetX(rightColumn), VectorGetX(rightWidth) };
}

struct PassiveSmokeTargetObservation{
    u32 focusCode = 0u;
    u32 passiveTargets = 0u;
};

template<typename RectanglesT, typename IncludeIndexT>
[[nodiscard]] inline PassiveSmokeTargetObservation ObservePassiveSmokeTargets(
    RectanglesT& rectangles,
    const Impl::Ui::InputRouter& input,
    const u64 parentGeneration,
    const u64 childGeneration,
    const usize beginIndex,
    const usize endIndex,
    const IncludeIndexT& includeIndex
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

struct PassiveSmokeSnapshotUpdate{
    b8 published = false;
    b8 displayChanged = false;
};

template<typename SnapshotT, typename StateChangedT>
[[nodiscard]] inline PassiveSmokeSnapshotUpdate PublishPassiveSmokeSnapshot(SnapshotT& current, SnapshotT& snapshot, const StateChangedT& stateChanged)noexcept(
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

[[nodiscard]] inline b8 HandlePassiveSmokeKeyboard(const i32 key, const i32 action, b8& phase, Impl::Ui::PopupState& parent, Impl::Ui::PopupState& child)noexcept{
    if(key != Core::Key::F4 && key != Core::Key::F5)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    if(key == Core::Key::F4)
        phase = !phase;
    else if(parent.isOpen()){
        child.close();
        parent.close();
    }
    else{
        parent.open();
        child.open();
    }
    return true;
}

template<typename ValuesT>
inline void PaintPassiveSmokeValueMarkers(Impl::UiPaintContext& context, const ValuesT& values, const u64 sequence){
    PaintKeyedSmokeValueMarkers<12u, 8u, 8u, 18u>(context, values, sequence);
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

