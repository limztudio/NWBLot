// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box.h"
#include "../edit/actions.h"
#include "../edit/vertical_navigation.h"
#include "../state/store.h"
#include <impl/ecs_ui/toolkit/input/popup.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EditBoxOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    LayoutSize height;
    b8 enabled = true;
    b8 readOnly = false;
};

// The host keeps this value alive through the matching endPanel/endWindow; paint and GPU snapshots own their text.
// Members are ordered by alignment (8-byte, then 4-byte, then 1-byte) to minimize padding.
struct EditBoxState{
    f64 caretElapsed = 0.0;
    u64 modelGeneration = 0u;
    u64 revision = 0u;
    u64 selectionGeneration = 0u;
    usize anchor = 0u;
    usize caret = 0u;
    EditBoxPlacement placement;
    f32 scroll = 0.0f;
    b8 focused = false;
};

struct EditBoxResult{
    b8 valid = false;
    b8 textChanged = false;
    b8 selectionChanged = false;
    b8 submitted = false;
    b8 cancelled = false;
    b8 focused = false;
    b8 preeditCaretVisible = true;
    b8 blurred = false;
    b8 abandoned = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The toolkit lends a model only during edit(). Native services and asynchronous exchange belong to the host adapter.
interface IEditBoxHost : private NoCopy{
public:
    virtual ~IEditBoxHost()noexcept = default;


public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState& widget, EditModel& model, const EditBoxOptions& options) = 0;
    // Deferred composite controls lend an explicit popup identity before its candidate geometry is emitted.
    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState& widget, EditModel& model, const EditBoxOptions& options, const PopupToken& popup) = 0;
    // Action-capable hosts invoke the borrowed sink in event order, before returning the draft for a paint snapshot.
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState& widget, EditModel& model, const EditBoxOptions& options, const PopupToken& popup, IEditActionSink& actions) = 0;
    // Navigation resolves the current model between copied events; application objects are lent only for this call.
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState& widget, EditModel& model, const EditBoxOptions& options, const PopupToken& popup, EditNavigationState& navigation, IEditNavigationResolver& resolver, IEditActionSink& actions) = 0;
    [[nodiscard]] virtual b8 publish(const WidgetState& widget, const EditBoxView& view, const EditBoxPlacement& placement, const EditBoxOptions& options) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

