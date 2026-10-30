// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "numeric_edit_snapshot.h"

#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/toolkit/edit/integer_model.h>
#include <impl/ecs_ui/toolkit/edit/float_model.h>

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
    virtual ~UiNumericEditSmokeScene()noexcept override;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;


private:
    void resetModels();
    [[nodiscard]] Array<u64, 30u> values()const noexcept;
    void observeState(const Impl::Ui::DisplayMetrics& display, Impl::Ui::TextService& text);
    void paintMarkers(Impl::UiPaintContext& context)const;
    void count(const Impl::Ui::NumericEditResult& result)noexcept;
    [[nodiscard]] Impl::Ui::FloatBounds floatBounds()const noexcept;


private:
    Core::Alloc::GlobalArena& m_arena;
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
    b8 m_enabled = true;
    b8 m_readOnly = false;
    b8 m_clamp = true;
};

using SharedUiNumericEditSmokeScene = RefCountPtr<RefCounter<UiNumericEditSmokeScene>, ArenaRefDeleter<RefCounter<UiNumericEditSmokeScene>, Core::Alloc::GlobalArena>>;

[[nodiscard]] b8 IsUiLayerNumericEditSmokeEnabled();
[[nodiscard]] b8 IsUiLayerNumericEditSkinSmokeEnabled();
[[nodiscard]] SharedUiNumericEditSmokeScene CreateUiNumericEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

