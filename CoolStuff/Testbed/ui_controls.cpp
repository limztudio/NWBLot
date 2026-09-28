// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"

#include <imgui.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_controls{
static constexpr f32 s_InitialX = 18.0f;
static constexpr f32 s_InitialY = 18.0f;
static constexpr f32 s_InitialWidth = 360.0f;
static constexpr AStringView s_WindowTitle = "NWB Testbed";
static constexpr AStringView s_RendererLine = "Renderer: mesh shader path with compute emulation fallback";
static constexpr AStringView s_CharacterLine = "Character: female model";
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void ProjectTestbed::drawUiControls(){
    ImGui::SetNextWindowPos(ImVec2(__hidden_ui_controls::s_InitialX, __hidden_ui_controls::s_InitialY), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(__hidden_ui_controls::s_InitialWidth, 0.0f), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin(__hidden_ui_controls::s_WindowTitle.data())){
        ImGui::End();
        return;
    }

    ImGui::TextUnformatted(__hidden_ui_controls::s_RendererLine.data());
    ImGui::Separator();
    ImGui::TextUnformatted(__hidden_ui_controls::s_CharacterLine.data());
    ImGui::End();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

