// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "edit_scene.h"

#include "edit_selection_probe.h"

#include "smoke_geometry.h"

#include "../smoke_environment.h"

#include <core/common/log.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ui_edit_smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_Primary = "Hello \xED\x95\x9C\xEA\xB8\x80";
static constexpr AStringView s_Secondary = "Target";
static constexpr Impl::Ui::Rect s_Panel{ 24.0f, 24.0f, 340.0f, 276.0f };


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditSmokeScene::UiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input)
    : m_arena(arena)
    , m_input(input)
    , m_primary(arena)
    , m_secondary(arena)
{
    resetModels();
    m_input.addHandlerToBack(*this);
}

UiEditSmokeScene::~UiEditSmokeScene(){
    m_input.removeHandler(*this);
}

bool UiEditSmokeScene::paint(Impl::UiPaintContext& context){
    using namespace Impl::Ui;
    observeDisplay(context.display);
    Builder& ui = context.ui;
    ui.style().fontSize = 16.0f;
    ui.style().gap = 8.0f;
    if(!ui.beginPanel("edit_fixture", __hidden_ui_edit_smoke::s_Panel))
        return false;
    const WidgetOptions title{ {}, { LayoutSizePolicy::Fixed, 24.0f } };
    const WidgetOptions label{ {}, { LayoutSizePolicy::Fixed, 18.0f } };
    if(!ui.label("title", "Single-line custom edit boxes", title) || !ui.label("primary_caption", "Latin + Korean", label))
        return false;
    EditBoxOptions options;
    options.width = { LayoutSizePolicy::Fixed, 260.0f };
    options.height = { LayoutSizePolicy::Fixed, 40.0f };
    options.readOnly = m_readOnly;
    m_primaryFocused = false;
    if(m_visible){
        const EditBoxResult result = ui.editBox("primary", m_primary, m_primaryState, options);
        if(!result.valid)
            return false;
        m_primaryFocused = result.focused;
    }
    else if(!ui.label("primary_hidden", "Primary field is hidden", title))
        return false;
    if(!ui.label("secondary_caption", "Clipboard destination", label))
        return false;
    options.readOnly = false;
    const EditBoxResult secondary = ui.editBox("secondary", m_secondary, m_secondaryState, options);
    if(!secondary.valid)
        return false;
    m_secondaryFocused = secondary.focused;
    if(
        !ui.label("read_only_hint", m_readOnly ? "F5: primary read-only" : "F5: primary editable", label)
        || !ui.label("visibility_hint", "F6: hide primary / F7: reset", label)
        || !ui.endPanel()
    )
        return false;
    observeState(context.text);
    paintMarkers(context);
    return true;
}

bool UiEditSmokeScene::keyboardUpdate(const i32 key, const i32 scancode, const i32 action, const i32 mods){
    static_cast<void>(scancode);
    static_cast<void>(mods);
    if(key != Core::Key::F5 && key != Core::Key::F6 && key != Core::Key::F7)
        return false;
    if(action == Core::InputAction::Press){
        if(key == Core::Key::F5)
            m_readOnly = !m_readOnly;
        else if(key == Core::Key::F6)
            m_visible = !m_visible;
        else
            resetModels();
    }
    return true;
}

void UiEditSmokeScene::resetModels(){
    const bool reset = m_primary.setText(__hidden_ui_edit_smoke::s_Primary)
        && m_secondary.setText(__hidden_ui_edit_smoke::s_Secondary);
    NWB_FATAL_ASSERT_MSG(reset, NWB_TEXT("UI edit smoke documents must be valid UTF8"));
}

void UiEditSmokeScene::observeDisplay(const Impl::Ui::DisplayMetrics& display){
    if(
        m_lastDisplay.logicalWidth == display.logicalWidth && m_lastDisplay.logicalHeight == display.logicalHeight
        && m_lastDisplay.pixelScaleX == display.pixelScaleX && m_lastDisplay.pixelScaleY == display.pixelScaleY
    )
        return;
    m_lastDisplay = display;
    m_displayChanged = true;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiEditSmoke: display logical={}x{} scale={}x{}")
        , display.logicalWidth, display.logicalHeight, display.pixelScaleX, display.pixelScaleY
    );
}

Array<u32, 12u> UiEditSmokeScene::values()const{
    return { static_cast<u32>(m_primary.text().size()), HashSmokeText32(m_primary.text()),
        static_cast<u32>(m_primary.anchor()), static_cast<u32>(m_primary.caret()), static_cast<u32>(m_primaryFocused),
        static_cast<u32>(m_secondary.text().size()), HashSmokeText32(m_secondary.text()),
        static_cast<u32>(m_secondary.anchor()), static_cast<u32>(m_secondary.caret()), static_cast<u32>(m_secondaryFocused),
        static_cast<u32>(m_readOnly), static_cast<u32>(m_visible) };
}

void UiEditSmokeScene::observeState(Impl::Ui::TextService& text){
    const auto current = values();
    const Impl::Ui::EditBoxPlacement primary = m_visible ? m_primaryState.placement : Impl::Ui::EditBoxPlacement{};
    const auto& secondary = m_secondaryState.placement;
    const auto primarySelection = CaptureEditSelection(m_arena, text, m_primary, primary, 16.0f);
    const auto secondarySelection = CaptureEditSelection(m_arena, text, m_secondary, secondary, 16.0f);
    if(
        m_sequence != 0u && !m_displayChanged && current == m_lastValues
        && SameSmokePlacement(primary, m_lastPrimary)
        && SameSmokePlacement(secondary, m_lastSecondary)
        && SameSmokeRect(primarySelection, m_lastPrimarySelection)
        && SameSmokeRect(secondarySelection, m_lastSecondarySelection)
    )
        return;
    ++m_sequence;
    m_lastValues = current;
    m_lastPrimary = primary;
    m_lastSecondary = secondary;
    m_lastPrimarySelection = primarySelection;
    m_lastSecondarySelection = secondarySelection;
    m_displayChanged = false;
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("UiEditSmoke: state sequence={} primary={},{},{},{},{} secondary={},{},{},{},{} readonly={} visible={}")
        , m_sequence, current[0], current[1], current[2], current[3], current[4]
        , current[5], current[6], current[7], current[8], current[9], current[10], current[11]
    );
    LogSmokeEditGeometry(NWB_TEXT("UiEditSmoke"), m_sequence, NWB_TEXT("primary"), primary, primarySelection);
    LogSmokeEditGeometry(NWB_TEXT("UiEditSmoke"), m_sequence, NWB_TEXT("secondary"), secondary, secondarySelection);
}

void UiEditSmokeScene::paintMarkers(Impl::UiPaintContext& context)const{
    const auto current = values();
    const f32 y = context.display.logicalHeight - 20.0f;
    for(usize index = 0u; index <= current.size(); ++index){
        const u32 value = index == current.size() ? m_sequence : current[index];
        context.paint.fillRect({ 12.0f + static_cast<f32>(index) * 20.0f, y, 14.0f, 12.0f },
            EncodeSmokeColor(value));
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


bool IsUiLayerEditSmokeEnabled(){
    return ReadSmokeEnvironmentFlag("NWB_UI_LAYER_EDIT");
}

SharedUiEditSmokeScene CreateUiEditSmokeScene(Core::Alloc::GlobalArena& arena, Core::InputDispatcher& input){
    return SharedUiEditSmokeScene(
        NewArenaObject<RefCounter<UiEditSmokeScene>>(arena, arena, input),
        ArenaRefDeleter<RefCounter<UiEditSmokeScene>, Core::Alloc::GlobalArena>(&arena),
        AdoptRef
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

