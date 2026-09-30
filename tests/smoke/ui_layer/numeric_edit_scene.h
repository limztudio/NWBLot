// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "numeric_edit_snapshot.h"

#include <impl/ecs_ui/components.h>
#include <impl/ecs_ui/toolkit/widgets/numeric_edit.h>

#include <core/alloc/general.h>
#include <core/input/module.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiNumericEditSmokeScene : public Core::IInputEventHandler, NoCopy{
public:
    UiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);
    virtual ~UiNumericEditSmokeScene()override;


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    void observeState(const Impl::Ui::DisplayMetrics& display);
    void paintMarkers(Impl::UiPaintContext& context)const;
    void count(const Impl::Ui::NumericEditResult& result);
    [[nodiscard]] Impl::Ui::FloatBounds floatBounds()const;
    [[nodiscard]] Array<u64, 30u> values()const;


private:
    Core::InputDispatcher& m_input;
    Impl::Ui::IntegerEditModel m_integer;
    Impl::Ui::FloatEditModel m_float;
    Impl::Ui::EditModel m_clipboard;
    Array<Impl::Ui::EditBoxState, 3u> m_states{};
    UiNumericEditSnapshot m_snapshot;
    u32 m_commits = 0u;
    u32 m_cancels = 0u;
    u32 m_rejects = 0u;
    u32 m_clamps = 0u;
    u32 m_restored = 0u;
    bool m_enabled = true;
    bool m_readOnly = false;
    bool m_clamp = true;
};

using SharedUiNumericEditSmokeScene = RefCountPtr<
    RefCounter<UiNumericEditSmokeScene>, ArenaRefDeleter<RefCounter<UiNumericEditSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerNumericEditSmokeEnabled();
[[nodiscard]] bool IsUiLayerNumericEditSkinSmokeEnabled();
[[nodiscard]] SharedUiNumericEditSmokeScene CreateUiNumericEditSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

