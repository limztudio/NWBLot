// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "progress_scene.h"

#include "../smoke_environment.h"

#include <global/bit.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiProgressSmokeScene::UiProgressSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
{
    static_cast<void>(arena);
    m_input.addHandlerToBack(*this);
}

UiProgressSmokeScene::~UiProgressSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiProgressSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    ui.progressStyle().padding = { 8.0f, 8.0f, 8.0f, 8.0f };
    ui.progressStyle().trackTint = {};
    ui.progressStyle().fillTint = {};
    m_declared = {};
    const f32 width = Max(300.0f, context.display.logicalWidth * 0.5f - 40.0f);
    const f32 right = 24.0f + width + 32.0f;
    const f32 otherWidth = Max(220.0f, context.display.logicalWidth - right - 24.0f);
    if(
        !paintMain(ui, { 24.0f, 24.0f, width, 360.0f })
        || !paintFrozen(ui, { right, 24.0f, otherWidth, 120.0f })
        || !paintExternal(context, { right, 176.0f, otherWidth, 84.0f }) || !paintPopups(ui, right) || ui.failed()
    )
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiProgressSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F4 && key != Core::Key::F5)
        return false;
    if(action != Core::InputAction::Press)
        return true;
    if(key == Core::Key::F4)
        m_phase = !m_phase;
    else if(m_parent.isOpen()){
        m_child.close();
        m_parent.close();
    }
    else{
        m_parent.open();
        m_child.open();
    }
    return true;
}

bool UiProgressSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("progress_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label("title", "Passive progress / bounded skin fill", caption))
        return false;
    if(ui.button("before", "Before progress", button))
        ++m_beforeClicks;
    constexpr Array<StringView, 6u> keys{ "zero", "quarter", "full", "below", "above", "tiny" };
    constexpr Array<f64, 6u> fractions{ 0.0, 0.25, 1.0, -2.0, 3.0, 0.01 };
    for(usize index = 0u; index < keys.size(); ++index){
        if(!ui.progress(keys[index], fractions[index]))
            return false;
        m_declared.rectangles[index + 2u] = { bounds.x + 8.0f, bounds.y + 68.0f + static_cast<f32>(index) * 40.0f,
            bounds.width - 16.0f, 32.0f };
    }
    if(ui.button("after", "After progress", button))
        ++m_afterClicks;
    return ui.endPanel();
}

bool UiProgressSmokeScene::paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("progress_frozen", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label("title", "Declaration owns value, options and style", caption))
        return false;
    const ProgressStyle original = ui.progressStyle();
    f64 fraction = m_phase ? 0.75 : 0.25;
    m_declared.bits[0u] = BitCast<u64>(fraction);
    ProgressOptions options;
    if(!ui.progress("frozen", fraction, options))
        return false;
    m_declared.rectangles[8u] = { bounds.x + 8.0f, bounds.y + 32.0f, bounds.width - 16.0f, 32.0f };
    fraction = m_phase ? 0.25 : 0.75;
    m_declared.bits[1u] = BitCast<u64>(fraction);
    options.height = 70.0f;
    options.width = { LayoutSizePolicy::Fixed, 70.0f };
    ui.progressStyle().padding = { 20.0f, 20.0f, 20.0f, 20.0f };
    ui.progressStyle().fillTint = { 0.0f, 0.0f, 0.0f, 1.0f };
    const bool painted = ui.endPanel();
    ui.progressStyle() = original;
    return painted;
}

bool UiProgressSmokeScene::paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    m_declared.rectangles[9u] = { bounds.x + 8.0f, bounds.y + 8.0f, bounds.width - 16.0f, 32.0f };
    const Rect clip{ bounds.x + 8.0f, bounds.y + 8.0f, (bounds.width - 16.0f) * 0.5f, 32.0f };
    m_declared.rectangles[10u] = clip;
    context.paint.fillRect(bounds, { 0.07f, 0.13f, 0.2f, 1.0f });
    context.paint.pushClip(clip);
    const bool painted = ui.beginPanel("progress_external", bounds) && ui.progress("external", 0.75) && ui.endPanel();
    const bool popped = context.paint.popClip();
    return painted && popped;
}

bool UiProgressSmokeScene::paintPopups(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions parent;
    parent.anchor = { right, 330.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 120.0f };
    if(!ui.beginPopup("progress_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    static_cast<void>(ui.button("parent_action", "Parent action", button));
    if(!ui.progress("parent_value", 0.25))
        return false;
    PopupOptions child;
    child.anchor = { right + 100.0f, 392.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 120.0f };
    if(ui.beginPopup("progress_child", m_child, child)){
        if(!ui.progress("child_value", 0.75))
            return false;
        static_cast<void>(ui.button("child_action", "Child action", button));
        if(!ui.endPopup())
            return false;
    }
    if(ui.failed() || !ui.endPopup())
        return false;
    const Rect& parentBounds = m_parent.placement().bounds;
    m_declared.rectangles[13u] = parentBounds;
    m_declared.rectangles[11u] = { parentBounds.x + 8.0f, parentBounds.y + 44.0f, parentBounds.width - 16.0f, 32.0f };
    if(m_child.isOpen()){
        const Rect& childBounds = m_child.placement().bounds;
        m_declared.rectangles[14u] = childBounds;
        m_declared.rectangles[12u] = { childBounds.x + 8.0f, childBounds.y + 8.0f, childBounds.width - 16.0f, 32.0f };
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerProgressSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PROGRESS");
}

bool IsUiLayerProgressSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_PROGRESS_SKIN");
}

SharedUiProgressSmokeScene CreateUiProgressSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiProgressSmokeScene(
        NewArenaObject<RefCounter<UiProgressSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiProgressSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

