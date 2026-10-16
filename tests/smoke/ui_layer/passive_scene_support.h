// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "marker_positions.h"
#include "smoke_geometry.h"

#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


namespace Tests::Smoke{


struct PassiveSmokeTargetObservation{
    u32 focusCode = 0u;
    u32 passiveTargets = 0u;
};

template<typename RectanglesT, typename IncludeIndexT>
[[nodiscard]] inline PassiveSmokeTargetObservation ObservePassiveSmokeTargets(const Impl::Ui::InputRouter& input, RectanglesT& rectangles, const u64 parentGeneration, const u64 childGeneration, const usize beginIndex, const usize endIndex, const IncludeIndexT& includeIndex){
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
    bool published = false;
    bool displayChanged = false;
};

template<typename SnapshotT, typename StateChangedT>
[[nodiscard]] inline PassiveSmokeSnapshotUpdate PublishPassiveSmokeSnapshot(SnapshotT& current, SnapshotT& snapshot, const StateChangedT& stateChanged){
    const Impl::Ui::DisplayMetrics& previous = snapshot.display;
    const bool displayChanged = current.display.logicalWidth != previous.logicalWidth || current.display.logicalHeight != previous.logicalHeight || current.display.pixelScaleX != previous.pixelScaleX || current.display.pixelScaleY != previous.pixelScaleY;
    bool changed = stateChanged();
    for(usize index = 0u; index < current.rectangles.size(); ++index)
        changed |= !SameSmokeRect(current.rectangles[index], snapshot.rectangles[index]);
    if(snapshot.sequence != 0u && !changed && !displayChanged)
        return {};
    current.sequence = snapshot.sequence + 1u;
    snapshot = current;
    return { true, displayChanged };
}

[[nodiscard]] inline bool HandlePassiveSmokeKeyboard(const i32 key, const i32 action, bool& phase, Impl::Ui::PopupState& parent, Impl::Ui::PopupState& child)noexcept{
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
    const f32 y = context.display.logicalHeight - 18.0f;
    const auto positions = BuildSmokeMarkerPositions<(TupleSize_V<ValuesT> + 1u) * 2u, 12u, 12u>();
    usize marker = 0u;
    for(const u64 value : values){
        for(u32 part = 0u; part < 2u; ++part){
            context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(value >> (part * 12u)));
        }
    }
    for(u32 part = 0u; part < 2u; ++part){
        context.paint.fillRect({ positions[marker++], y, 8.0f, 8.0f }, EncodeSmokeColor(sequence >> (part * 12u)));
    }
}


};


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

