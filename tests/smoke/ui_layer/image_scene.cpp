// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "image_scene.h"

#include "../smoke_environment.h"
#include "passive_scene_support.h"

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_image_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr AStringView s_TitleKey = "title";
constexpr AStringView s_RadioRegion = "radio.mark";
constexpr AStringView s_ProgressRegion = "progress.fill";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiImageSmokeScene::UiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_input(input)
{
    static_cast<void>(arena);
    m_input.addHandlerToBack(*this);
}

UiImageSmokeScene::~UiImageSmokeScene()noexcept{
    m_input.removeHandler(*this);
}

bool UiImageSmokeScene::paint(Impl::UiPaintContext& context){
    auto& ui = context.ui;
    ui.style().fontSize = 14.0f;
    ui.style().gap = 8.0f;
    m_declared = {};
    const SIMDVectorSingle logicalWidth = VectorReplicate(context.display.logicalWidth);
    const SIMDVectorSingle mainWidth = VectorMax(VectorReplicate(300.0f), VectorMultiplyAddExpression(logicalWidth, VectorReplicate(0.5f), VectorNegate(VectorReplicate(40.0f))));
    const SIMDVectorSingle rightColumn = VectorAdd(VectorAdd(VectorReplicate(24.0f), mainWidth), VectorReplicate(32.0f));
    const SIMDVectorSingle rightWidth = VectorMax(VectorReplicate(220.0f), VectorSubtract(VectorSubtract(logicalWidth, rightColumn), VectorReplicate(24.0f)));
    const f32 width = VectorGetX(mainWidth);
    const f32 right = VectorGetX(rightColumn);
    const f32 otherWidth = VectorGetX(rightWidth);
    if(!paintMain(ui, { 24.0f, 24.0f, width, 392.0f }) || !paintFrozen(ui, { right, 24.0f, otherWidth, 120.0f }) || !paintExternal(context, { right, 176.0f, otherWidth, 84.0f }) || !paintPopups(ui, right) || ui.failed())
        return false;
    observeState(context);
    paintMarkers(context);
    return true;
}

bool UiImageSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods)noexcept{
    static_cast<void>(scancode);
    static_cast<void>(mods);
    return HandlePassiveSmokeKeyboard(key, action, m_phase, m_parent, m_child);
}

bool UiImageSmokeScene::paintMain(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("image_main", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(!ui.label(__hidden_ui_image_smoke::s_TitleKey, "Atlas images / copied size and tint", caption))
        return false;
    if(ui.button("before", "Before images", button))
        ++m_beforeClicks;
    ImageOptions options;
    if(!ui.image("natural_sprite", Name(__hidden_ui_image_smoke::s_RadioRegion), options) || !ui.image("natural_slice", Name(__hidden_ui_image_smoke::s_ProgressRegion)))
        return false;
    const SIMDVectorSingle origin = VectorSet(bounds.x, bounds.y, 0.0f, 0.0f);
    m_declared.rectangles[2u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 68.0f, 24.0f, 24.0f)));
    m_declared.rectangles[3u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 100.0f, 24.0f, 24.0f)));
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.image("fixed_sprite", Name(__hidden_ui_image_smoke::s_RadioRegion), options) || !ui.image("fixed_slice", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    m_declared.rectangles[4u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 132.0f, 96.0f, 40.0f)));
    m_declared.rectangles[5u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 180.0f, 96.0f, 40.0f)));
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 32.0f };
    if(!ui.image("stretch", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    m_declared.rectangles[6u] = StoreSmokeRect(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 228.0f, -16.0f, 32.0f)));
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.tint = { 0.4f, 0.7f, 0.9f, 0.5f };
    if(!ui.image("tinted", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
        return false;
    m_declared.rectangles[7u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 268.0f, 96.0f, 40.0f)));
    options.height = { LayoutSizePolicy::Fixed, 24.0f };
    options.tint = { 1.0f, 1.0f, 1.0f, 0.0f };
    if(!ui.image("transparent", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
        return false;
    m_declared.rectangles[8u] = StoreSmokeRect(VectorAdd(origin, VectorSet(8.0f, 316.0f, 96.0f, 24.0f)));
    if(ui.button("after", "After images", button))
        ++m_afterClicks;
    return ui.endPanel();
}

bool UiImageSmokeScene::paintFrozen(Impl::Ui::Builder& ui, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    if(!ui.beginPanel("image_frozen", bounds))
        return false;
    const WidgetOptions caption{ {}, { LayoutSizePolicy::Fixed, 16.0f } };
    if(!ui.label(__hidden_ui_image_smoke::s_TitleKey, "Declaration retains region, options and tint", caption))
        return false;
    Name region(m_phase ? __hidden_ui_image_smoke::s_RadioRegion.data() : __hidden_ui_image_smoke::s_ProgressRegion.data());
    ImageOptions options;
    options.width = { LayoutSizePolicy::Fixed, 96.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.tint = m_phase ? Color{ 0.5f, 0.75f, 1.0f, 0.5f } : Color{};
    m_declared.values[8u] = m_phase ? 2u : 1u;
    if(!ui.image("frozen", region, options))
        return false;
    m_declared.rectangles[9u] = StoreSmokeRect(VectorAdd(VectorSet(bounds.x, bounds.y, 0.0f, 0.0f), VectorSet(8.0f, 32.0f, 96.0f, 40.0f)));
    region = Name(m_phase ? __hidden_ui_image_smoke::s_ProgressRegion.data() : __hidden_ui_image_smoke::s_RadioRegion.data());
    m_declared.values[9u] = m_phase ? 1u : 2u;
    options.width = { LayoutSizePolicy::Fixed, 70.0f };
    options.height = { LayoutSizePolicy::Fixed, 70.0f };
    options.tint = { 0.0f, 0.0f, 0.0f, 1.0f };
    return ui.endPanel();
}

bool UiImageSmokeScene::paintExternal(Impl::UiPaintContext& context, const Impl::Ui::Rect& bounds){
    using namespace Impl::Ui;
    auto& ui = context.ui;
    const SIMDVectorSingle content = VectorAdd(VectorSet(bounds.x, bounds.y, bounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 40.0f));
    m_declared.rectangles[10u] = StoreSmokeRect(content);
    const Rect clip = StoreSmokeRect(VectorMultiply(content, VectorSet(1.0f, 1.0f, 0.5f, 1.0f)));
    m_declared.rectangles[11u] = clip;
    context.paint.fillRect(bounds, { 0.07f, 0.13f, 0.2f, 1.0f });
    context.paint.pushClip(clip);
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    const bool painted = ui.beginPanel("image_external", bounds) && ui.image("external", Name(__hidden_ui_image_smoke::s_ProgressRegion), options) && ui.endPanel();
    const bool popped = context.paint.popClip();
    return painted && popped;
}

bool UiImageSmokeScene::paintPopups(Impl::Ui::Builder& ui, const f32 right){
    using namespace Impl::Ui;
    PopupOptions parent;
    parent.anchor = { right, 300.0f, 8.0f, 24.0f };
    parent.size = { 280.0f, 128.0f };
    if(!ui.beginPopup("image_parent", m_parent, parent))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, { LayoutSizePolicy::Fixed, 28.0f } };
    if(ui.button("parent_action", "Parent action", button))
        m_parent.close();
    ImageOptions options;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    if(!ui.image("parent_value", Name(__hidden_ui_image_smoke::s_ProgressRegion), options))
        return false;
    PopupOptions child;
    child.anchor = { VectorGetX(VectorAdd(VectorReplicate(right), VectorReplicate(100.0f))), 370.0f, 8.0f, 16.0f };
    child.size = { 240.0f, 128.0f };
    if(ui.beginPopup("image_child", m_child, child)){
        options.tint = { 0.5f, 1.0f, 0.5f, 0.75f };
        if(!ui.image("child_value", Name(__hidden_ui_image_smoke::s_RadioRegion), options))
            return false;
        if(ui.button("child_action", "Child action", button))
            m_child.close();
        if(!ui.endPopup())
            return false;
    }
    if(ui.failed() || !ui.endPopup())
        return false;
    const Rect& parentBounds = m_parent.placement().bounds;
    m_declared.rectangles[14u] = parentBounds;
    m_declared.rectangles[12u] = StoreSmokeRect(VectorAdd(VectorSet(parentBounds.x, parentBounds.y, parentBounds.width, 0.0f), VectorSet(8.0f, 44.0f, -16.0f, 40.0f)));
    if(m_child.isOpen()){
        const Rect& childBounds = m_child.placement().bounds;
        m_declared.rectangles[15u] = childBounds;
        m_declared.rectangles[13u] = StoreSmokeRect(VectorAdd(VectorSet(childBounds.x, childBounds.y, childBounds.width, 0.0f), VectorSet(8.0f, 8.0f, -16.0f, 40.0f)));
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerImageSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_IMAGE");
}

bool IsUiLayerImageSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_IMAGE_SKIN");
}

SharedUiImageSmokeScene CreateUiImageSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiImageSmokeScene(NewArenaObject<RefCounter<UiImageSmokeScene>>(arena, arena, input), ArenaRefDeleter<RefCounter<UiImageSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

