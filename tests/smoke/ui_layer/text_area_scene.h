// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "text_area_snapshot.h"

#include <impl/ecs_ui/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class UiTextAreaSmokeScene : NoCopy{
public:
    UiTextAreaSmokeScene(Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);
    ~UiTextAreaSmokeScene();


public:
    [[nodiscard]] bool paint(Impl::UiPaintContext& context);


private:
    void resetModel();
    void longDocument();
    [[nodiscard]] bool seedClipboard();
    [[nodiscard]] bool drainClipboard();
    [[nodiscard]] bool observeState(Impl::UiPaintContext& context);
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
    bool m_enabled = true;
    bool m_readOnly = false;
    bool m_compact = false;
    bool m_longDocument = false;
};

using SharedUiTextAreaSmokeScene = RefCountPtr<
    RefCounter<UiTextAreaSmokeScene>, ArenaRefDeleter<RefCounter<UiTextAreaSmokeScene>, Core::Alloc::GlobalArena>
>;

[[nodiscard]] bool IsUiLayerTextAreaSmokeEnabled();
[[nodiscard]] bool IsUiLayerTextAreaSkinSmokeEnabled();
[[nodiscard]] SharedUiTextAreaSmokeScene CreateUiTextAreaSmokeScene(
    Core::Alloc::GlobalArena& arena, Core::IClipboardService& clipboard);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

