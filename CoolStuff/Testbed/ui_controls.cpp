// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_controls{
static constexpr StringView s_WindowTitle = "NWB Testbed";
static constexpr StringView s_RendererLine = "Renderer: mesh shader path with compute emulation fallback";
static constexpr StringView s_CharacterLine = "Character: female model";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ProjectTestbed::drawUiControls(NWB::Impl::UiPaintContext& context){
    NWB::Impl::Ui::WindowOptions options;
    options.initialBounds = { 18.0f, 18.0f, 360.0f, 160.0f };
    options.contentHeightFirstUse = true;
    NWB::Impl::Ui::SeparatorOptions separator;
    separator.thickness = 1.0f;
    if(context.ui.beginWindow("testbed_window", __hidden_ui_controls::s_WindowTitle, m_uiWindow, options)){
        const bool declared = context.ui.label("renderer", __hidden_ui_controls::s_RendererLine)
            && context.ui.separator("separator", separator)
            && context.ui.label("character", __hidden_ui_controls::s_CharacterLine);
        NWB_FATAL_ASSERT(declared);
    }
    const bool ended = context.ui.endWindow();
    NWB_FATAL_ASSERT(ended);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

