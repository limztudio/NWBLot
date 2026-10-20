// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "window_scene.h"

#include "smoke_geometry.h"
#include "../smoke_environment.h"

#include <impl/ecs_ui/toolkit/builder.h>

#include <core/common/log.h>

#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_window_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


constexpr TStringView s_IncreaseLabel = NWB_TEXT("increase");
constexpr AStringView s_IncreaseKey = "increase";
constexpr TStringView s_LockedLabel = NWB_TEXT("locked");
constexpr AStringView s_LockedKey = "locked";
static constexpr Impl::Ui::Color s_TitleAnchor{ 0.70f, 0.02f, 0.35f, 1.0f };
static constexpr Impl::Ui::Color s_ContentAnchor{ 0.02f, 0.30f, 0.75f, 1.0f };
static constexpr f32 s_LabelHeight = 18.0f;
static constexpr f32 s_SeparatorHeight = 1.0f;
static constexpr f32 s_ControlHeight = 26.0f;
static constexpr f32 s_ControlWidth = 80.0f;
static constexpr f32 s_Gap = 4.0f;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Impl::Ui::Rect Button(const Impl::Ui::WindowState& state, const Impl::Ui::WindowMetrics& metrics)noexcept{
    const Impl::Ui::Rect content = Impl::Ui::WindowLayout::Content(state, metrics);
    const SIMDVectorSingle origin = VectorAdd(VectorSet(content.x, content.y, 0.0f, 0.0f), VectorSet(metrics.contentPadding.left, metrics.contentPadding.top, 0.0f, 0.0f));
    const SIMDVectorSingle buttonY = VectorAdd(VectorAdd(VectorAdd(VectorSplatY(origin), VectorReplicate(s_LabelHeight)), VectorReplicate(s_SeparatorHeight)), VectorReplicate(2.0f * s_Gap));
    return { VectorGetX(origin), VectorGetX(buttonY), s_ControlWidth, s_ControlHeight };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool UiWindowSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    Builder& ui = context.ui;
    ui.style().gap = __hidden_ui_window_smoke::s_Gap;
    ui.style().fontSize = 12.0f;
    WindowOptions options;
    options.initialBounds = { 40.0f, 40.0f, 300.0f, 180.0f };
    options.minimumSize = { 180.0f, 110.0f };
    options.movable = !m_locked;
    options.resizable = !m_locked;
    const bool contentVisible = ui.beginWindow("window", "Window", m_window, options);
    const WindowMetrics metrics = ui.windowMetrics();
    if(contentVisible){
        const WidgetOptions label{ {}, { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_LabelHeight } };
        if(!ui.label("caption", "Drag, resize or collapse", label))
            return false;
        SeparatorOptions separator;
        separator.thickness = __hidden_ui_window_smoke::s_SeparatorHeight;
        if(!ui.separator("separator", separator))
            return false;
        ContainerOptions row;
        row.height = { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlHeight };
        row.gap = __hidden_ui_window_smoke::s_Gap;
        if(!ui.beginRow("actions", row))
            return false;
        const WidgetOptions control{ { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlWidth }, { LayoutSizePolicy::Fixed, __hidden_ui_window_smoke::s_ControlHeight } };
        if(ui.button(__hidden_ui_window_smoke::s_IncreaseKey, "Increase", control)){
            ++m_count;
            logAction(__hidden_ui_window_smoke::s_IncreaseLabel);
        }
        if(ui.checkbox(__hidden_ui_window_smoke::s_LockedKey, "Lock", m_locked, control))
            logAction(__hidden_ui_window_smoke::s_LockedLabel);
        if(!ui.endContainer())
            return false;
    }
    if(!ui.endWindow())
        return false;
    observeState(metrics);
    paintMarkers(context, metrics);
    return true;
}

void UiWindowSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY)
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: display logical={}x{} scale={}x{}"), display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY);
}

void UiWindowSmokeScene::observeState(const Impl::Ui::WindowMetrics& metrics){
    using namespace Impl::Ui;
    const Rect& bounds = m_window.bounds;
    const Rect& previous = m_lastWindow.bounds;
    if(
        m_sequence != 0u && !m_displayChanged && bounds.x == previous.x && bounds.y == previous.y
        && bounds.width == previous.width && bounds.height == previous.height && m_window.collapsed == m_lastWindow.collapsed
        && m_count == m_lastCount && m_locked == m_lastLocked
    )
        return;
    ++m_sequence;
    m_lastWindow = m_window;
    m_lastCount = m_count;
    m_lastLocked = m_locked;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: state sequence={} bounds={},{},{},{} collapsed={} locked={} count={}")
        , m_sequence, bounds.x, bounds.y, bounds.width, bounds.height
        , static_cast<u32>(m_window.collapsed), static_cast<u32>(m_locked), m_count
    );
    const Rect title{ bounds.x, bounds.y, bounds.width, metrics.titleHeight };
    const Rect collapse = WindowLayout::Collapse(m_window, metrics);
    const Rect resize = WindowLayout::Resize(m_window, metrics);
    const Rect button = __hidden_ui_window_smoke::Button(m_window, metrics);
    const Rect content = WindowLayout::Content(m_window, metrics);
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: geometry sequence={} title={},{},{},{} collapse={},{},{},{} resize={},{},{},{}")
        , m_sequence, title.x, title.y, title.width, title.height
        , collapse.x, collapse.y, collapse.width, collapse.height
        , resize.x, resize.y, resize.width, resize.height
    );
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: controls sequence={} button={},{},{},{} lock={},{},{},{} content={},{},{},{} minimum={},{}")
        , m_sequence, button.x, button.y, button.width, button.height
        , VectorGetX(VectorAdd(VectorAdd(VectorReplicate(button.x), VectorReplicate(button.width)), VectorReplicate(__hidden_ui_window_smoke::s_Gap))), button.y, button.width, button.height
        , content.x, content.y, content.width, content.height, metrics.minimumSize.x, metrics.minimumSize.y
    );
}

void UiWindowSmokeScene::logAction(const TStringView action){
    ++m_actions;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiWindowSmoke: action={} count={} locked={} actions={}"), action, m_count, static_cast<u32>(m_locked), m_actions);
}

void UiWindowSmokeScene::paintMarkers(Impl::UiPaintContext& context, const Impl::Ui::WindowMetrics& metrics)const{
    using namespace Impl::Ui;
    const Rect& bounds = m_window.bounds;
    const f32 values[]{ bounds.x, bounds.y, bounds.width, bounds.height, static_cast<f32>(m_count), static_cast<f32>(m_window.collapsed), static_cast<f32>(m_locked), static_cast<f32>(m_sequence) };
    const f32 markerY = VectorGetX(VectorSubtract(VectorReplicate(context.display.logicalHeight), VectorReplicate(20.0f)));
    for(usize index = 0u; index < LengthOf(values); ++index)
        context.paint.fillRect({ VectorGetX(VectorMultiplyAddExpression(VectorReplicate(static_cast<f32>(index)), VectorReplicate(20.0f), VectorReplicate(12.0f))), markerY, 14.0f, 12.0f }, EncodeSmokeColor(values[index]));
    // Lanes are anchor x, title anchor y and content anchor y.
    const SIMDVectorSingle anchors = VectorAdd(VectorAdd(VectorSet(bounds.x, bounds.y, bounds.y, 0.0f), VectorSet(bounds.width, 4.0f, metrics.titleHeight, 0.0f)), VectorSet(-14.0f, 0.0f, 4.0f, 0.0f));
    context.paint.pushClip(WindowLayout::Visible(m_window, metrics));
    context.paint.fillRect({ VectorGetX(anchors), VectorGetY(anchors), 10.0f, 10.0f }, __hidden_ui_window_smoke::s_TitleAnchor);
    if(!m_window.collapsed)
        context.paint.fillRect({ VectorGetX(anchors), VectorGetZ(anchors), 10.0f, 10.0f }, __hidden_ui_window_smoke::s_ContentAnchor);
    const bool popped = context.paint.popClip();
    NWB_FATAL_ASSERT(popped);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerWindowSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_WINDOW");
}

bool IsUiLayerWindowSkinSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_WINDOW_SKIN");
}

SharedUiWindowSmokeScene CreateUiWindowSmokeScene(Core::Alloc::GlobalArena& arena){
    return SharedUiWindowSmokeScene(NewArenaObject<RefCounter<UiWindowSmokeScene>>(arena), ArenaRefDeleter<RefCounter<UiWindowSmokeScene>, Core::Alloc::GlobalArena>(&arena), s_AdoptRef);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

