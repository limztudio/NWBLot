// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "ui_internal.h"

#include <core/ecs/world.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/compiled_graph.h>
#include <core/task/gpu/task_graph.h>
#include <impl/assets/graphics/imgui/binding_slots.h>
#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_frame{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr f32 s_FallbackDeltaSeconds = 1.0f / 60.0f;
static constexpr f32 s_DefaultFramebufferScale = 1.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiSystem::setCurrentContext()const{
    ImGui::SetCurrentContext(m_imguiContext);
}

void UiSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    if(m_taskGraphPresentationRetryPending)
        return;

    beginFrame(delta);

    UiDrawContext context{ m_world, Core::ECS::ENTITY_ID_INVALID, m_deltaSeconds };
    m_world.view<UiComponent>().each(
        [&context](const Core::ECS::EntityID entity, UiComponent& component){
            if(!component.visible || !component.draw)
                return;

            context.entity = entity;
            component.draw(context);
        }
    );

    finishFrame();
}

void UiSystem::beginFrame(const f32 delta){
    setCurrentContext();

    if(m_frameStarted && !m_frameFinished)
        finishFrame();

    m_taskGraphPresentationRetryPending = false;
    m_taskGraphPresentationPrepared = false;
    m_taskGraphPresentationHasWork = false;
    m_taskGraphPresentationClaimed = false;
    m_taskGraphLegacyPresentationClaimed = false;
    m_taskGraphDrawUploadsPrepared = false;
    m_taskGraphPresentationGraphGeneration = 0u;
    m_taskGraphPresentationFrame = {};
    m_taskGraphVertexUpload.clear();
    m_taskGraphIndexUpload.clear();
    clearTaskGraphDrawSnapshot();

    ++m_frameGeneration;
    if(m_frameGeneration == 0u)
        ++m_frameGeneration;

    m_deltaSeconds = IsFinite(delta) && delta > 0.0f ? delta : __hidden_ui_frame::s_FallbackDeltaSeconds;

    i32 windowWidth = 0;
    i32 windowHeight = 0;
    m_graphics.getWindowDimensions(windowWidth, windowHeight);

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(
        static_cast<f32>(Max(windowWidth, 0)),
        static_cast<f32>(Max(windowHeight, 0))
    );
    io.DisplayFramebufferScale = ImVec2(__hidden_ui_frame::s_DefaultFramebufferScale, __hidden_ui_frame::s_DefaultFramebufferScale);
    io.DeltaTime = m_deltaSeconds;

    ImGui::NewFrame();
    m_frameStarted = true;
    m_frameFinished = false;
    m_wantsKeyboardCapture = io.WantCaptureKeyboard;
    m_wantsMouseCapture = io.WantCaptureMouse;
    m_wantsTextInput = io.WantTextInput;
}

void UiSystem::finishFrame(){
    if(!m_frameStarted || m_frameFinished)
        return;

    setCurrentContext();
    ImGui::Render();

    const ImGuiIO& io = ImGui::GetIO();
    m_wantsKeyboardCapture = io.WantCaptureKeyboard;
    m_wantsMouseCapture = io.WantCaptureMouse;
    m_wantsTextInput = io.WantTextInput;
    m_frameFinished = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

