// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_area_snapshot.h"

#include <impl/ecs_ui/paint_context.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextAreaSmokeScene : NoCopy{
public:
    UiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);
    ~UiTextAreaSmokeScene()noexcept;


public:
    [[nodiscard]] b8 paint(Impl::UiPaintContext& context);


private:
    void resetModel();
    void longDocument();
    [[nodiscard]] b8 seedClipboard();
    [[nodiscard]] b8 drainClipboard();
    [[nodiscard]] b8 observeState(Impl::UiPaintContext& context);
    void paintMarkers(Impl::UiPaintContext& context)const;


private:
    Core::IClipboardService& m_clipboard;
    AString<Core::Alloc::GlobalArena> m_longText;
    Impl::Ui::EditModel m_model;
    Impl::Ui::TextAreaState m_state;
    Impl::Ui::EditBoxView m_observed;
    Core::ClipboardCompletion m_completion;
    Core::ClipboardRequestToken m_clipboardToken;
    UiTextAreaSnapshot m_snapshot;
    u32 m_submits = 0u;
    u32 m_cancels = 0u;
    u32 m_blurs = 0u;
    u32 m_abandons = 0u;
    u32 m_outside = 0u;
    u32 m_clipboardSeeds = 0u;
    b8 m_enabled = true;
    b8 m_readOnly = false;
    b8 m_compact = false;
    b8 m_longDocument = false;
};

using SharedUiTextAreaSmokeScene = RefCountPtr< RefCounter<UiTextAreaSmokeScene>, ArenaRefDeleter<RefCounter<UiTextAreaSmokeScene>, Core::Alloc::GlobalArena> >;

[[nodiscard]] b8 IsUiLayerTextAreaSmokeEnabled();
[[nodiscard]] b8 IsUiLayerTextAreaSkinSmokeEnabled();
[[nodiscard]] SharedUiTextAreaSmokeScene CreateUiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

