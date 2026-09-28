// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "interactive_scene.h"

#include "../smoke_environment.h"

#include <impl/ui/builder.h>

#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_interactive_smoke{
static constexpr Impl::Ui::Rect s_Panel{ 18.0f, 18.0f, 320.0f, 220.0f };
static constexpr Impl::Ui::Color s_Off{ 0.08f, 0.01f, 0.01f, 1.0f };
static constexpr Impl::Ui::Color s_On{ 0.02f, 0.70f, 0.12f, 1.0f };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiInteractiveSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    Builder& ui = context.ui;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("interaction", __hidden_ui_interactive_smoke::s_Panel))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    if(!ui.label("caption", "Custom UI interaction", caption))
        return false;
    ContainerOptions actions;
    actions.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.beginRow("actions", actions))
        return false;
    const WidgetOptions increase{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f }, m_enabled };
    if(ui.button("increase", "Increase", increase)){
        m_count = Min(m_count + 1u, 255u);
        logAction(NWB_TEXT("increase"));
    }
    const WidgetOptions checkbox{ { LayoutSizePolicy::Fixed, 140.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.checkbox("enabled", "Enabled", m_enabled, checkbox))
        logAction(NWB_TEXT("enabled"));
    if(!ui.endContainer())
        return false;
    const WidgetOptions reset{ { LayoutSizePolicy::Fixed, 130.0f }, { LayoutSizePolicy::Fixed, 36.0f } };
    if(ui.button("reset", "Reset", reset)){
        m_count = 0u;
        logAction(NWB_TEXT("reset"));
    }
    if(!ui.endPanel())
        return false;
    paintMarkers(context);
    return true;
}

void UiInteractiveSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiInteractiveSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

void UiInteractiveSmokeScene::logAction(const TStringView action){
    ++m_actions;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiInteractiveSmoke: action={} count={} enabled={} actions={}")
        , action, m_count, static_cast<u32>(m_enabled), m_actions
    );
}

void UiInteractiveSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const f32 y = context.display.logicalHeight - 20.0f;
    for(u32 bit = 0u; bit < 8u; ++bit){
        const bool set = (m_count & (1u << bit)) != 0u;
        const Impl::Ui::Rect marker{ 18.0f + static_cast<f32>(bit) * 20.0f, y, 14.0f, 12.0f };
        context.paint.fillRect(marker, set ? __hidden_ui_interactive_smoke::s_On : __hidden_ui_interactive_smoke::s_Off);
    }
    const Impl::Ui::Rect enabledMarker{ 210.0f, y, 28.0f, 12.0f };
    context.paint.fillRect(enabledMarker, m_enabled ? __hidden_ui_interactive_smoke::s_On : __hidden_ui_interactive_smoke::s_Off);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerInteractionSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_INTERACTIVE");
}

SharedUiInteractiveSmokeScene CreateUiInteractiveSmokeScene(Core::Alloc::GlobalArena& arena){
    return SharedUiInteractiveSmokeScene(
        NewArenaObject<RefCounter<UiInteractiveSmokeScene>>(arena),
        ArenaRefDeleter<RefCounter<UiInteractiveSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

