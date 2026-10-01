// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "edit_box.h"
#include "../edit/actions.h"
#include "../edit/vertical_navigation.h"
#include "../state/store.h"
#include "../input/popup.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct EditBoxOptions{
    LayoutSize width = { LayoutSizePolicy::Stretch, 1.0f };
    LayoutSize height;
    bool enabled = true;
    bool readOnly = false;
};

// The host keeps this value alive through the matching endPanel/endWindow; paint and GPU snapshots own their text.
struct EditBoxState{
    EditBoxPlacement placement;
    f32 scroll = 0.0f;
    f64 caretElapsed = 0.0;
    u64 modelGeneration = 0u;
    u64 revision = 0u;
    u64 selectionGeneration = 0u;
    usize anchor = 0u;
    usize caret = 0u;
    bool focused = false;
};

struct EditBoxResult{
    bool valid = false;
    bool textChanged = false;
    bool selectionChanged = false;
    bool submitted = false;
    bool cancelled = false;
    bool focused = false;
    bool preeditCaretVisible = true;
    bool blurred = false;
    bool abandoned = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// The toolkit lends a model only during edit(). Native services and asynchronous exchange belong to the host adapter.
interface IEditBoxHost : private NoCopy{
public:
    virtual ~IEditBoxHost() = default;


public:
    [[nodiscard]] virtual EditBoxResult edit(const WidgetState& widget, EditModel& model, const EditBoxOptions& options) = 0;
    // Deferred composite controls lend an explicit popup identity before its candidate geometry is emitted.
    [[nodiscard]] virtual EditBoxResult editInPopup(const WidgetState& widget, EditModel& model,
        const EditBoxOptions& options, const PopupToken& popup) = 0;
    // Action-capable hosts invoke the borrowed sink in event order, before returning the draft for a paint snapshot.
    [[nodiscard]] virtual EditBoxResult editActions(const WidgetState& widget, EditModel& model, const EditBoxOptions& options,
        const PopupToken& popup, IEditActionSink& actions) = 0;
    // Navigation resolves the current model between copied events; application objects are lent only for this call.
    [[nodiscard]] virtual EditBoxResult editNavigated(const WidgetState& widget, EditModel& model, const EditBoxOptions& options,
        const PopupToken& popup, EditNavigationState& navigation, IEditNavigationResolver& resolver, IEditActionSink& actions) = 0;
    [[nodiscard]] virtual bool publish(const WidgetState& widget, const EditBoxView& view,
        const EditBoxPlacement& placement, const EditBoxOptions& options) = 0;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

