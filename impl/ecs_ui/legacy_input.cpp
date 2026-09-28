// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <core/graphics/runtime/runtime.h>

#include <global/simplemath.h>

#include <imgui_internal.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_legacy_input{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static bool Contains(const Ui::Rect& rectangle, const f64 x, const f64 y){
    return rectangle.width > 0.0f && rectangle.height > 0.0f && x >= rectangle.x && y >= rectangle.y
        && x < static_cast<f64>(rectangle.x) + rectangle.width && y < static_cast<f64>(rectangle.y) + rectangle.height;
}

[[nodiscard]] static Ui::Rect LogicalRect(const ImRect& rectangle, const f32 scaleX, const f32 scaleY){
    return { rectangle.Min.x / scaleX, rectangle.Min.y / scaleY,
        Max(0.0f, rectangle.Max.x - rectangle.Min.x) / scaleX, Max(0.0f, rectangle.Max.y - rectangle.Min.y) / scaleY };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSystem::windowFocusUpdate(const bool focused){
    if(!m_imguiContext)
        return;
    if(!focused)
        cancelInputFocus();
    setCurrentContext();
    ImGui::GetIO().AddFocusEvent(focused);
}

void UiSystem::setInputDelegated(const bool delegated){
    if(delegated && m_inputRegistered){
        m_input.removeHandler(*this);
        m_inputRegistered = false;
    }
    else if(!delegated && !m_inputRegistered){
        m_input.addHandlerToBack(*this);
        m_inputRegistered = true;
    }
    windowFocusUpdate(m_input.windowFocused());
}

bool UiSystem::hitTestUi(const f64 logicalX, const f64 logicalY)const noexcept{
    if(!m_imguiContext || !IsFinite(logicalX) || !IsFinite(logicalY))
        return false;
    if(m_legacyInputOverflow)
        return true;
    for(const LegacyInputRegion& region : m_legacyInputRegions){
        if(
            __hidden_legacy_input::Contains(region.bounds, logicalX, logicalY)
            && !__hidden_legacy_input::Contains(region.hole, logicalX, logicalY)
        )
            return true;
    }
    return false;
}

void UiSystem::cancelInputFocus(){
    if(!m_imguiContext)
        return;
    setCurrentContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ClearEventsQueue();
    io.ClearInputKeys();
    io.ClearInputMouse();
    // Clearing queued UI events must preserve the actual window focus transition.
    io.AddFocusEvent(m_input.windowFocused());
    ImGui::ClearActiveID();
    ImGui::FocusWindow(nullptr);
    m_wantsKeyboardCapture = false;
    m_wantsMouseCapture = false;
    m_wantsTextInput = false;
}

void UiSystem::captureInputRegions(){
    m_legacyInputRegions.clear();
    m_legacyInputOverflow = false;
    if(!m_imguiContext || !m_frameFinished)
        return;
    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;
    m_graphics.getDPIScaleInfo(scaleX, scaleY);
    if(!IsFinite(scaleX) || !IsFinite(scaleY) || scaleX <= 0.0f || scaleY <= 0.0f)
        return;
    const ImGuiContext& context = *m_imguiContext;
    for(i32 index = context.Windows.Size - 1; index >= 0; --index){
        const ImGuiWindow* window = context.Windows[index];
        if(
            !window->Active || window->Hidden || window->LastFrameActive != context.FrameCount
            || (window->Flags & ImGuiWindowFlags_NoMouseInputs) != 0
        )
            continue;
        if(m_legacyInputRegions.size() == s_MaxLegacyInputRegions){
            // Preserve UI ownership conservatively if a legacy application exceeds the bounded region snapshot.
            m_legacyInputOverflow = true;
            return;
        }
        const bool resizable = (window->Flags & (ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize)) == 0;
        const f32 resizePadding = resizable ? context.Style.WindowBorderHoverPadding : 0.0f;
        const ImVec2 padding(Max(context.Style.TouchExtraPadding.x, resizePadding), Max(context.Style.TouchExtraPadding.y, resizePadding));
        ImRect bounds = window->OuterRectClipped;
        bounds.Expand(padding);
        Ui::Rect hole;
        if(window->HitTestHoleSize.x != 0 && window->HitTestHoleSize.y != 0){
            hole = { (window->Pos.x + window->HitTestHoleOffset.x) / scaleX,
                (window->Pos.y + window->HitTestHoleOffset.y) / scaleY,
                static_cast<f32>(window->HitTestHoleSize.x) / scaleX, static_cast<f32>(window->HitTestHoleSize.y) / scaleY };
        }
        m_legacyInputRegions.push_back({ __hidden_legacy_input::LogicalRect(bounds, scaleX, scaleY), hole });
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

