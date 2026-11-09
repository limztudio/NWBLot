// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "project.h"
#include "CoolStuff/Testbed/fly_camera_motion.adhd.h"
#include "CoolStuff/Testbed/scene_constants.adhd.h"
#include "CoolStuff/Testbed/ui_gallery_constants.adhd.h"

#include <impl/assets_graphics/bindless/runtime_abi.h>
#include <impl/assets_ui_skin/asset.h>
#include <impl/assets_ui_skin/types.adhd.h>
#include <impl/ecs_mesh/skinning/system.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_model_renderer/model_renderer.h>
#include <impl/ecs_render/module.h>
#include <impl/ecs_scene/module.h>
#include <impl/ecs_ui/layer_system.h>
#include <impl/ecs_ui/toolkit/images/image_loader.h>

#include <core/common/log.h>
#include <core/ecs/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/input/module.h>
#include <core/telemetry/frame_graph_registry.h>

#include <global/numeric_text.h>
#include <global/simd/frame.h>
#include <global/simd/vector.h>
#include <global/simplemath.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_testbed_runtime{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr TStringView s_DefaultSceneDescription = NWB_TEXT("45-degree directional + point light, female skinned character on a white ground plane");
static constexpr TStringView s_InitWorldFailedText = NWB_TEXT("ProjectTestbed initialization failed: CreateInitialProjectWorld returned false");
static constexpr TStringView s_SceneEntitiesInvalidText = NWB_TEXT("ProjectTestbed initialization failed: failed to instantiate scene entities");
static constexpr TStringView s_StartupSceneText = NWB_TEXT("ProjectTestbed: startup scene created ({})");
static constexpr TStringView s_ShutdownText = NWB_TEXT("ProjectTestbed: shutdown");
static constexpr TStringView s_WorldAllocFailed = NWB_TEXT("CreateInitialProjectWorld failed: ECS world allocation failed");
static constexpr TStringView s_ResolverNull = NWB_TEXT("CreateInitialProjectWorld failed: shader path resolver callback is null");
static constexpr TStringView s_DestroyRequiresIdleOrLoss = NWB_TEXT("Project-world destruction requires either a completed device join or terminal device loss");

using PaintBuilder = NWB::Impl::Ui::PaintBuilder;
using Label = NWB::Impl::Ui::Label;
using ShapeRequest = NWB::Impl::Ui::ShapeRequest;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ApplyFlyCameraInputToMainCamera(NWB::Core::ECS::World& world, const SIMDVectorSingle moveAxes, const b8 boosted, const SIMDVectorSingle mouseDelta, const f32 deltaSeconds)noexcept{
    const NWB::Impl::Scene::SceneCameraView cameraView = NWB::Impl::Scene::ResolveSceneCameraView(world);
    NWB_ASSERT(cameraView.valid());

    const SIMDVectorSingle resolvedRotation = FlyCameraMotion().resolveStepRotation(LoadFloat(cameraView.transform->rotation), mouseDelta);
    const SIMDVectorSingle resolvedPosition = FlyCameraMotion().resolvePosition(LoadFloat(cameraView.transform->position), moveAxes, resolvedRotation, deltaSeconds, boosted);
    StoreFloat(resolvedRotation, cameraView.transform->rotation);
    StoreFloat(resolvedPosition, cameraView.transform->position);
}

[[nodiscard]] static b8 DrawSkinGallery(PaintBuilder& paint, const f32 originX, const f32 originY, const f32 captionHeight){
    const f32 leftColumnX = originX + s_LeftColumnX;
    const f32 rightColumnX = originX + s_RightColumnX;
    const f32 buttonTopY = originY + s_ButtonTopY;
    const f32 buttonBottomY = originY + s_ButtonBottomY;
    return
        paint.drawRegion(s_Panel, { originX, originY, s_OrdinaryGalleryWidth, s_PanelBaseHeight + captionHeight })
        && paint.drawRegion(s_ButtonNormal, { leftColumnX, buttonTopY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonHover, { rightColumnX, buttonTopY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonPressed, { leftColumnX, buttonBottomY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_ButtonDisabled, { rightColumnX, buttonBottomY, s_ButtonWidth, s_ButtonHeight })
        && paint.drawRegion(s_EditFocused, { leftColumnX, originY + s_EditFocusedY, s_EditFocusedWidth, s_EditFocusedHeight })
        && paint.drawRegion(s_ComboArrow, { originX + s_ComboArrowX, originY + s_ComboArrowY, s_ComboArrowSize, s_ComboArrowSize })
        && paint.drawRegion(s_Checkbox, { originX + s_CheckboxX, originY + s_CheckboxY, s_CheckboxSize, s_CheckboxSize })
        && paint.drawRegion(s_CheckboxMark, { originX + s_CheckboxMarkX, originY + s_CheckboxMarkY, s_CheckboxMarkSize, s_CheckboxMarkSize })
        && paint.drawRegion(s_ProgressTrack, { leftColumnX, originY + s_ProgressTrackY, s_ProgressTrackWidth, s_ProgressTrackHeight })
        && paint.drawRegion(s_ProgressFill, { originX + s_ProgressFillX, originY + s_ProgressFillY, s_ProgressFillWidth, s_ProgressFillHeight })
        && paint.drawRegion(s_EditNormal, { leftColumnX, originY + s_EditNormalY, s_EditNormalWidth, captionHeight })
    ;
}

[[nodiscard]] static b8 ConfigureLabel(const StringView text, const f32 size, Label& outLabel){
    return outLabel.setText(ShapeRequest{ .text = text, .fontSize = size }) == NWB::Impl::Ui::TextLayoutStatus::Success;
}

[[nodiscard]] static b8 ConfigureLabel(const StringView text, Label& outLabel){
    return ConfigureLabel(text, s_LabelFontSize, outLabel);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


StringView UiListSource::text(const u64 index)const noexcept{
    const u64 rowKey = key(index);
    if(rowKey == 0u)
        return {};
    m_label[0] = 'R';
    m_label[1] = 'o';
    m_label[2] = 'w';
    m_label[3] = ' ';
    Array<char, s_VirtualListMaxDecimalDigits> digits{};
    const AStringView formatted = FormatDecimal(static_cast<usize>(rowKey), digits);
    for(usize offset = 0u; offset < formatted.size(); ++offset)
        m_label[s_VirtualListRowPrefixLength + offset] = formatted[offset];
    return { m_label.data(), s_VirtualListRowPrefixLength + formatted.size() };
}

void UiListGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("list_gallery", { x, y, s_OrdinaryGalleryWidth, s_ListGalleryPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "100000 virtual rows");
        ListOptions options;
        options.height = { LayoutSizePolicy::Fixed, s_ListGalleryListHeight };
        const ListResult result = ui.virtualList("rows", m_source, m_state, options);
        if(result.activated)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom list selected key={}"), m_state.selectedKey());
        declared = DeclareSelectedKeyLabel(ui, scratchArena, m_state.selectedKey()) && result.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom virtual list declaration failed"));
}

void UiComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("combo_gallery", { x, y, s_OrdinaryGalleryWidth, s_ComboGalleryPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Keyed combo selection");
        ComboOptions options;
        options.popupHeight = s_ComboGalleryPopupHeight;
        const ComboResult result = ui.comboBox(s_ChoiceKey, m_source, m_state, options);
        if(result.committed)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom combo selected key={}"), m_state.selectedKey());
        declared = DeclareSelectedKeyLabel(ui, scratchArena, m_state.selectedKey()) && result.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom combo declaration failed"));
}

UiEditGallery::UiEditGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_text(arena)
    , m_readOnly(arena)
{
    const b8 configured = m_text.setText("Edit me: íê¸") && m_readOnly.setText("Read-only: copy this text");
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed edit gallery must have valid initial text"));
}

void UiEditGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("edit_gallery", { x, y, s_OrdinaryGalleryWidth, s_EditGalleryPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Text editing");
        EditBoxOptions options;
        const EditBoxResult text = ui.editBox("editable", m_text, m_textState, options);
        if(text.textChanged)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom edit text bytes={}"), m_text.text().size());
        options.readOnly = true;
        const EditBoxResult readOnly = ui.editBox(s_ReadOnlyKey, m_readOnly, m_readOnlyState, options);
        declared = text.valid && readOnly.valid && declared;
        declared = ui.label(s_HintKey, "Ctrl+A/C/X/V, undo and redo") && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom edit widget declaration failed"));
}

b8 UiImageGallery::loadAssets(NWB::Core::Alloc::GlobalArena& arena, const NWB::Core::Assets::AssetManager& assets, const NWB::Impl::ActiveSceneSubsystems& sceneSubsystems){
    const NWB::Impl::UiSubsystem* const uiSubsystem = sceneSubsystems.findSubsystem<NWB::Impl::UiSubsystem>();
    if(!uiSubsystem || !uiSubsystem->basicSkin.valid())
        return false;
    auto skin = assets.loadTypedSync<NWB::Impl::UiSkin>(uiSubsystem->basicSkin.name(), NWB_TEXT("UiImageGallery"));
    if(!skin)
        return false;
    auto source = NWB::Impl::Ui::LoadImageSource(arena, assets, (*skin)->texture());
    if(!source)
        return false;
    m_source = Move(*source);
    return true;
}

void UiImageGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("image_gallery", { x, y, s_OrdinaryGalleryWidth, s_ImageGalleryPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Atlas and engine images");
    valid = ui.beginRow("samples") && valid;
    ImageOptions sprite;
    sprite.width = { LayoutSizePolicy::Fixed, s_ImageGallerySpriteSize };
    sprite.height = { LayoutSizePolicy::Fixed, s_ImageGallerySpriteSize };
    valid = ui.image("sprite", s_RadioMarkRegion, sprite) && valid;
    ImageOptions slice = sprite;
    slice.width.value = s_ImageGallerySliceWidth;
    slice.tint = ResolveImageGallerySliceTint();
    valid = ui.image("slice", s_ButtonNormal, slice) && valid;
    valid = ui.image("natural", s_ComboArrow) && valid;
    valid = ui.endContainer() && valid;
    ImageOptions texture;
    texture.width = { LayoutSizePolicy::Stretch, 1.0f };
    texture.height = { LayoutSizePolicy::Fixed, s_ImageGalleryTextureHeight };
    valid = ui.image("engine_texture", m_source, texture) && valid;
    valid = ui.label(s_HintKey,"Texture, sprite and nine-slice") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom image declaration failed"));
}

UiNestedPopupGallery::UiNestedPopupGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_source(arena)
    , m_search(arena)
    , m_beforeText(arena)
    , m_afterText(arena)
    , m_childText(arena)
{
    const b8 initialized = m_beforeText.setText("Before child") && m_afterText.setText("After child") && m_childText.setText("Inside child");
    NWB_FATAL_ASSERT(initialized);
    m_combo.select(1u);
    m_search.combo().select(1u);
}

void UiNestedPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    const f32 previousGap = ui.style().gap;
    ui.style().gap = s_NestedPopupCompactGap;
    ScopeExit restoreGap([&ui, previousGap]()noexcept{ ui.style().gap = previousGap; });
    WithGalleryFontSize(ui, s_NestedPopupFontSize, [&](){
        if(!ui.beginPanel("nested_popup_gallery", { x, y, s_OrdinaryGalleryWidth, s_NestedPopupPanelHeight }))
            return;
        b8 valid = ui.label(s_TitleKey, "Nested popup / parent scope");
        if(ui.button("open", "Open parent and right child"))
            m_parent.open();
        valid = ui.label(s_HintKey, "Escape/outside closes the top child") && valid;
        valid = ui.endPanel() && valid;
        PopupOptions parent;
        parent.anchor = ResolveNestedPopupParentAnchorRect(x, y);
        parent.size = { s_NestedPopupParentWidth, s_NestedPopupParentHeight };
        if(ui.beginPopup("nested_parent", m_parent, parent)){
            valid = paintParent(ui) && valid;
            valid = ui.endPopup() && valid;
        }
        if(!valid || ui.failed())
            NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom nested popup declaration failed"));
    });
}

b8 UiNestedPopupGallery::paintParent(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions button{ { LayoutSizePolicy::Fixed, s_NestedPopupButtonWidth }, {} };
    const WidgetOptions wide{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    ContainerOptions row;
    row.gap = s_NestedPopupCompactGap;
    EditBoxOptions edit;
    ListOptions list;
    list.height = { LayoutSizePolicy::Fixed, s_NestedPopupListHeight };
    if(!ui.label(s_TitleKey, "Parent: controls before and after child") || !ui.beginRow("before", row))
        return false;
    ui.button(s_NestedPopupActionKey, "Before", button);
    const EditBoxResult before = ui.editBox(s_NestedPopupEditKey, m_beforeText, m_beforeEdit, edit);
    if(!before.valid || !ui.endContainer() || !ui.virtualList("before_list", m_source, m_beforeList, list).valid)
        return false;
    if(ui.button("open_child", "Open child outside parent clip", wide))
        m_child.open();
    if(!paintChild(ui) || !ui.beginRow("after", row))
        return false;
    ui.button(s_NestedPopupActionKey, "After", button);
    const EditBoxResult after = ui.editBox(s_NestedPopupEditKey, m_afterText, m_afterEdit, edit);
    if(!after.valid || !ui.endContainer() || !ui.virtualList("after_list", m_source, m_afterList, list).valid)
        return false;
    ComboOptions combo;
    combo.popupHeight = s_NestedPopupComboPopupHeight;
    if(!ui.comboBox("combo", m_source, m_combo, combo).valid)
        return false;
    SearchComboOptions search;
    search.combo = combo;
    search.combo.popupHeight = s_NestedPopupSearchPopupHeight;
    if(!ui.searchComboBox("search", m_source, m_search, search).combo.valid)
        return false;
    ui.button(s_NestedPopupMenuAnchorKey, "Right-click / Menu for commands", wide);
    ContextMenuOptions menu;
    menu.size = { s_NestedPopupMenuWidth, s_NestedPopupMenuHeight };
    const ContextMenuResult result = ui.contextMenu(s_CommandsKey, s_NestedPopupMenuAnchorKey, m_source, m_menu, menu);
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: nested context command={}"), result.key);
    return result.valid;
}

b8 UiNestedPopupGallery::paintChild(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const Rect& parent = m_parent.placement().bounds;
    PopupOptions options;
    options.anchor = ResolveNestedPopupChildAnchorRect(parent);
    options.side = PopupPlacementSide::Right;
    options.size = { s_NestedPopupChildWidth, s_NestedPopupChildHeight };
    if(!ui.beginPopup("right_child", m_child, options))
        return !ui.failed();
    const WidgetOptions button{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
    if(!ui.label(s_TitleKey, "Independent child overlay"))
        return false;
    ui.button(s_NestedPopupActionKey, "Child action", button);
    EditBoxOptions edit;
    const EditBoxResult result = ui.editBox(s_NestedPopupEditKey, m_childText, m_childEdit, edit);
    if(ui.button("close_branch", "Close ancestor and entire branch", button))
        m_parent.close();
    return result.valid && ui.endPopup();
}

UiNumericEditGallery::UiNumericEditGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_integer(arena)
    , m_float(arena)
{
    // 2^53 + 1 is not exact in f64, so it shows integer editing keeps every digit.
    const b8 initialized = m_integer.setValue(s_NumericEditInitialInteger) && m_float.setValue(s_NumericEditInitialDecimal);
    NWB_FATAL_ASSERT(initialized);
}

void UiNumericEditGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("numeric_edit_gallery", { x, y, s_OrdinaryGalleryWidth, s_NumericEditPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Exact integer / decimal editing");
        IntegerEditOptions integer;
        const NumericEditBoxResult first = ui.integerEdit("integer", m_integer, m_integerState, integer);
        FloatEditOptions floating;
        floating.bounds = { -s_NumericEditDecimalLimit, s_NumericEditDecimalLimit, NumericBoundsPolicy::Clamp };
        const NumericEditBoxResult second = ui.floatEdit("float", m_float, m_floatState, floating);
        const auto text = StringFormat(scratchArena, "Committed: {} / {}", m_integer.value(), m_float.value());
        declared = ui.label("committed", { text.data(), text.size() }) && first.edit.valid && second.edit.valid && declared;
        declared = ui.label(s_HintKey, "Enter: commit, Escape: restore") && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom numeric editor declaration failed"));
}

void UiPopupGallery::trigger(NWB::Impl::Ui::Builder& ui){
    using namespace NWB::Impl::Ui;
    const WidgetOptions options{ { LayoutSizePolicy::Fixed, s_PopupMenuButtonWidth }, {} };
    if(ui.button("menu", "Menu", options))
        m_popup.open();
}

void UiPopupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    PopupOptions options;
    options.anchor = ResolvePopupGalleryAnchorRect(x, y);
    options.size = { s_PopupGalleryWidth, s_PopupGalleryHeight };
    if(!ui.beginPopup("gallery_menu", m_popup, options))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        const WidgetOptions row{ { LayoutSizePolicy::Stretch, 1.0f }, {} };
        const StringView caption = ResolvePopupGalleryCaption(m_choice);
        b8 declared = ui.label(s_TitleKey, caption);
        if(ui.button("first", "First choice", row)){
            m_choice = s_PopupFirstChoice;
            if(!m_keepOpen)
                m_popup.close();
        }
        if(ui.button("second", "Second choice", row)){
            m_choice = s_PopupSecondChoice;
            if(!m_keepOpen)
                m_popup.close();
        }
        if(ui.checkbox("keep_open", "Keep open on selection", m_keepOpen, row))
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: popup keep open={}"), m_keepOpen);
        return ui.endPopup() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom popup declaration failed"));
}

void UiPopupToolsGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("popup_tools_gallery", { x, y, s_OrdinaryGalleryWidth, s_PopupToolsPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Tooltip and context commands");
        if(ui.button(s_PopupToolsAnchorKey, "Right-click / Menu / Shift+F10"))
            ++m_anchorClicks;
        declared = ui.tooltip("help", s_PopupToolsAnchorKey, "Open commands here; Delete is disabled.", m_tooltip) && declared;
        ContextMenuOptions options;
        options.size = { s_PopupToolsMenuWidth, s_PopupToolsMenuHeight };
        const ContextMenuResult result = ui.contextMenu(s_CommandsKey, s_PopupToolsAnchorKey, m_source, m_menu, options);
        if(result.activated){
            m_command = result.key;
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom context command key={}"), m_command);
        }
        const auto text = StringFormat(scratchArena, "Command: {} / Clicks: {}", m_command, m_anchorClicks);
        declared = ui.label(s_SelectedKey, { text.data(), text.size() }) && result.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom popup tools declaration failed"));
}

void UiProgressGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    NWB::Impl::Ui::Builder& ui = context.ui;
    if(!ui.beginPanel("progress_gallery", { x, y, s_OrdinaryGalleryWidth, s_ProgressGalleryPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Skinned progress bar");
    if(ui.button("advance", "Advance by 25%"))
        m_fraction = AdvanceProgressGalleryFraction(m_fraction);
    valid = ui.progress(s_AmountKey, m_fraction) && valid;
    const auto caption = StringFormat(scratchArena, "Completed: {:.0f}%", m_fraction * s_ProgressPercentScale);
    valid = ui.label("value", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom progress declaration failed"));
}

void UiRadioGroupGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("radio_group_gallery", { x, y, s_OrdinaryGalleryWidth, s_RadioGroupPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Radio choices");
    const RadioGroupResult result = ui.radioGroup("quality", m_source, m_state);
    valid = ui.label(s_HintKey, "Arrows select; Enter / Space sets") && result.valid && valid;
    valid = ui.endPanel() && valid;
    if(result.activated)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom radio choice key={}"), m_state.selectedKey());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom radio group declaration failed"));
}

void UiSearchComboGallery::paint(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("search_combo_gallery", { x, y, s_OrdinaryGalleryWidth, s_SearchComboPanelHeight }))
        return;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Searchable fruit selection");
        SearchComboOptions options;
        options.combo.popupHeight = s_SearchComboPopupHeight;
        const SearchComboResult result = ui.searchComboBox(s_ChoiceKey, m_source, m_state, options);
        if(result.combo.committed)
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom searchable combo selected key={}"), m_state.combo().selectedKey());
        declared = DeclareSelectedKeyLabel(ui, scratchArena, m_state.combo().selectedKey()) && result.combo.valid && declared;
        return ui.endPanel() && declared;
    });
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom searchable combo declaration failed"));
}

void UiSliderGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("slider_gallery", { x, y, s_OrdinaryGalleryWidth, s_SliderGalleryPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Continuous slider");
    if(ui.checkbox(s_EnabledKey, "Enable slider", m_enabled))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider enabled={}"), m_enabled);
    SliderOptions options;
    options.enabled = m_enabled;
    options.keyStep = s_SliderKeyStep;
    valid = ui.slider(s_AmountKey, m_state, options) && valid;
    valid = ui.label(s_HintKey, "Drag, arrows, Home / End") && valid;
    valid = ui.endPanel() && valid;
    const SliderResult result = m_state.result();
    if(valid && result.valid && result.valueChanged)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom slider value={}"), m_state.value());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom slider declaration failed"));
}

UiTextAreaGallery::UiTextAreaGallery(NWB::Core::Alloc::GlobalArena& arena)
    : m_document(arena, {}, NWB::Impl::Ui::EditTextMode::Multiline)
{
    const b8 initialized = m_document.setText("Custom multiline editor\nUp / Down retain the preferred column.\nShift selects; Ctrl+Enter submits.\nClipboard and IME belong to the OS.\n\nLong lines scroll horizontally; longer documents scroll vertically.");
    NWB_FATAL_ASSERT(initialized);
}

void UiTextAreaGallery::paint(NWB::Impl::UiPaintContext& context, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("text_area_gallery", { x, y, s_SelectorWidth, s_ContentHeight }))
        return;
    EditBoxResult result;
    const b8 valid = WithGalleryFontSize(ui, [&](){
        b8 declared = ui.label(s_TitleKey, "Multiline text area");
        if(ui.checkbox(s_ReadOnlyKey, "Read only", m_readOnly))
            NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: text area read only={}"), m_readOnly);
        TextAreaOptions options;
        options.height = { LayoutSizePolicy::Stretch, 1.0f };
        options.readOnly = m_readOnly;
        result = ui.textArea("document", m_document, m_state, options);
        declared = result.valid && ui.label(s_HintKey, "Home / End: line; Ctrl: whole document") && declared;
        return ui.endPanel() && declared;
    });
    if(result.submitted)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: multiline document submitted, bytes={}"), m_document.text().size());
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom text area declaration failed"));
}

void UiWidgetGallery::paint(NWB::Impl::UiPaintContext& context, const NWB::Impl::Ui::Rect& bounds){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    const u32 previous = m_selectedGallery;
    if(!ui.beginPanel("gallery_selector", { bounds.x, bounds.y, bounds.width, s_SelectorHeight }))
        return;
    ContainerOptions row;
    b8 valid = ui.beginRow("navigation", row);
    WidgetOptions navigation;
    navigation.width = { LayoutSizePolicy::Fixed, s_NavigationButtonWidth };
    navigation.enabled = m_selectedGallery != 0u && !ui.input().hasPopup();
    if(ui.button("previous", "Previous", navigation))
        --m_selectedGallery;
    navigation.enabled = m_selectedGallery + 1u < Section::kCount && !ui.input().hasPopup();
    if(ui.button("next", "Next", navigation))
        ++m_selectedGallery;
    valid = ui.endContainer() && valid;
    const u32 selected = m_selectedGallery;
    NWB::Core::Alloc::ScratchArena scratchArena(s_SelectorArena);
    const auto caption = StringFormat(scratchArena, "UI samples {}/{}: {}", selected + 1u, static_cast<u32>(Section::kCount), SectionName(static_cast<Section::Enum>(selected)));
    valid = ui.label("section", { caption.data(), caption.size() }) && valid;
    valid = ui.endPanel() && valid;
    if(!valid){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: UI gallery selector declaration failed"));
        return;
    }
    if(previous != selected)
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: UI gallery section={}"), m_selectedGallery + 1u);

    const Point contentOrigin = ResolveWidgetGalleryContentOrigin(bounds, selected);
    const f32 x = contentOrigin.x;
    const f32 y = contentOrigin.y;
    switch(selected){
    case Section::Controls:
        paintControls(context, scratchArena, x, y);
        m_popups.paint(context, x, y);
        break;
    case Section::Edit: m_edits.paint(context, x, y); break;
    case Section::List: m_lists.paint(context, scratchArena, x, y); break;
    case Section::Combo: m_combos.paint(context, scratchArena, x, y); break;
    case Section::SearchCombo: m_searchCombos.paint(context, scratchArena, x, y); break;
    case Section::PopupTools: m_popupTools.paint(context, scratchArena, x, y); break;
    case Section::NestedPopup: m_nestedPopups.paint(context, x, y); break;
    case Section::NumericEdit: m_numericEdits.paint(context, scratchArena, x, y); break;
    case Section::TextArea: m_textAreas.paint(context, x, y); break;
    case Section::RadioGroup: m_radioGroups.paint(context, x, y); break;
    case Section::Slider: m_sliders.paint(context, x, y); break;
    case Section::Progress: m_progress.paint(context, scratchArena, x, y); break;
    case Section::Image: m_images.paint(context, x, y); break;
    default: break;
    }
}

void UiWidgetGallery::paintControls(NWB::Impl::UiPaintContext& context, NWB::Core::Alloc::ScratchArena& scratchArena, const f32 x, const f32 y){
    using namespace NWB::Impl::Ui;
    Builder& ui = context.ui;
    if(!ui.beginPanel("interactive_gallery", { x, y, s_OrdinaryGalleryWidth, s_ControlsPanelHeight }))
        return;
    b8 valid = ui.label(s_TitleKey, "Interactive custom UI");
    if(ui.checkbox(s_EnabledKey, "Enable counter", m_enabled))
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom UI counter enabled={}"), m_enabled);
    valid = ui.beginRow("actions") && valid;
    WidgetOptions options;
    options.enabled = m_enabled;
    options.width = { LayoutSizePolicy::Stretch, 1.0f };
    if(ui.button("increase", "Increase", options)){
        m_count = StepWidgetGalleryCounter(m_count);
        NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("Testbed: custom UI counter={}"), m_count);
    }
    if(ui.button("reset", "Reset"))
        m_count = 0u;
    m_popups.trigger(ui);
    valid = ui.endContainer() && valid;
    const auto caption = StringFormat(scratchArena, "Count: {}", m_count);
    valid = ui.label("count", { caption.data(), caption.size() }) && valid;
    valid = ui.label("keyboard_hint", "Tab / Shift+Tab, Enter / Space") && valid;
    valid = ui.endPanel() && valid;
    if(!valid)
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI widget declaration failed"));
}

UiSkinPreview::UiSkinPreview(NWB::Core::Alloc::GlobalArena& arena)
    : m_normal(arena)
    , m_hover(arena)
    , m_pressed(arena)
    , m_disabled(arena)
    , m_edit(arena)
    , m_caption(arena)
    , m_korean(arena)
    , m_widgets(arena)
{
    using namespace __hidden_testbed_runtime;
    const b8 configured = ConfigureLabel("Normal", m_normal) && ConfigureLabel("Hover", m_hover)
        && ConfigureLabel("Pressed", m_pressed) && ConfigureLabel("Disabled", m_disabled)
        && ConfigureLabel("UTF-8 labels", s_GalleryFontSize, m_edit) && ConfigureLabel("FreeType + HarfBuzz", s_GalleryFontSize, m_caption)
        && m_korean.setText(ShapeRequest{ .text = s_KoreanText, .language = "ko", .fontSize = s_LabelFontSize, .scriptTag = ::PackFourCharTag('H', 'a', 'n', 'g') }) == NWB::Impl::Ui::TextLayoutStatus::Success;
    NWB_FATAL_ASSERT_MSG(configured, NWB_TEXT("Testbed UI labels must have valid constant text"));
}

Expected<NWB::Impl::Ui::Rect> UiSkinPreview::galleryBounds(NWB::Impl::UiPaintContext& context){
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    if(!caption || !korean)
        return MakeUnexpected(Failure{});
    const f32 captionHeight = ResolveSkinGalleryCaptionHeight(caption->height, korean->height);
    return ResolveWidgetGalleryLayoutBounds(context.display, ResolveSkinGalleryPreferredTop(captionHeight));
}

void UiSkinPreview::paint(NWB::Impl::UiPaintContext& context){
    using namespace __hidden_testbed_runtime;
    const f32 originX = ResolveSkinGalleryOriginX(context.display.logicalWidth);
    const f32 originY = s_GalleryMargin;
    const auto caption = m_caption.measure(context.text, context.display);
    const auto korean = m_korean.measure(context.text, context.display);
    const auto normal = m_normal.measure(context.text, context.display);
    const auto hover = m_hover.measure(context.text, context.display);
    const auto pressed = m_pressed.measure(context.text, context.display);
    const auto disabled = m_disabled.measure(context.text, context.display);
    const auto edit = m_edit.measure(context.text, context.display);
    if(!caption || !korean || !normal || !hover || !pressed || !disabled || !edit){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label measurement failed"));
        return;
    }
    const f32 captionHeight = ResolveSkinGalleryCaptionHeight(caption->height, korean->height);
    const f32 preferredTop = ResolveSkinGalleryPreferredTop(captionHeight);
    const NWB::Impl::Ui::Rect gallery = ResolveWidgetGalleryLayoutBounds(context.display, preferredTop);
    m_widgets.paint(context, gallery);
    if(gallery.y < preferredTop || originX < s_MinimumGalleryX)
        return;
    if(!DrawSkinGallery(context.paint, originX, originY, captionHeight)){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: default UI skin is missing a gallery region"));
        return;
    }

    const f32 captionX = originX + s_CaptionX;
    const f32 captionY = originY + s_CaptionY;
    const f32 leftTextX = originX + s_LeftTextX;
    const f32 rightTextX = originX + s_RightTextX;
    const f32 normalY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonTopY, s_ButtonHeight, normal->height);
    const f32 hoverY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonTopY, s_ButtonHeight, hover->height);
    const f32 pressedY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonBottomY, s_ButtonHeight, pressed->height);
    const f32 disabledY = ResolveSkinGalleryCenteredTextY(originY, s_ButtonBottomY, s_ButtonHeight, disabled->height);
    const f32 editY = ResolveSkinGalleryCenteredTextY(originY, s_EditFocusedY, s_EditFocusedHeight, edit->height);
    const b8 captionPainted = m_caption.paint(context.text, context.paint, { captionX, captionY, s_CaptionWidth, caption->height })
        && m_korean.paint(context.text, context.paint, { captionX, captionY + caption->height, s_CaptionWidth, korean->height });
    if(!captionPainted){
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI caption shaping or rasterization failed"));
        return;
    }
    if(
        !m_normal.paint(context.text, context.paint, { leftTextX, normalY, s_ButtonTextWidth, normal->height }, s_LabelTextColor)
        || !m_hover.paint(context.text, context.paint, { rightTextX, hoverY, s_ButtonTextWidth, hover->height }, s_LabelTextColor)
        || !m_pressed.paint(context.text, context.paint, { leftTextX, pressedY, s_ButtonTextWidth, pressed->height }, s_LabelTextColor)
        || !m_disabled.paint(context.text, context.paint, { rightTextX, disabledY, s_ButtonTextWidth, disabled->height }, s_DisabledLabelTextColor)
        || !m_edit.paint(context.text, context.paint, { leftTextX, editY, s_EditTextWidth, edit->height }, s_LabelTextColor)
    )
        NWB_LOGGER_ERROR(NWB_TEXT("Testbed: custom UI label shaping or rasterization failed"));
}

Expected<UniquePtr<NWB::Core::ECS::World>, TStringView> CreateInitialProjectWorld(NWB::ProjectRuntimeContext& context){
    auto world = MakeUnique<NWB::Core::ECS::World>(context.objectArena, context.cpuTasks);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_testbed_runtime::s_WorldAllocFailed);
        return MakeUnexpected(__hidden_testbed_runtime::s_WorldAllocFailed);
    }

    if(!context.shaderPathResolver){
        NWB_LOGGER_FATAL(__hidden_testbed_runtime::s_ResolverNull);
        return MakeUnexpected(__hidden_testbed_runtime::s_ResolverNull);
    }

    auto& meshSystem = world->addSystem<NWB::Impl::MeshSystem>(*world);
    auto& rendererSystem = world->addSystem<NWB::Impl::RendererSystem>(*world, context.graphics, context.assetManager, context.shaderPathResolver);
    world->addSystem<NWB::Impl::ModelSystem>(*world, context.assetManager, NWB::Impl::CreateModelObjectRendererHooks());
    auto& meshSkinningSystem = world->addSystem<NWB::Impl::MeshSkinningSystem>(*world, context.graphics, context.assetManager, meshSystem, context.shaderPathResolver);
    auto& uiLayerSystem = world->addSystem<NWB::Impl::UiLayerSystem>(
        *world,
        context.graphics,
        context.input,
        context.clipboard,
        context.textInput,
        context.assetManager,
        context.shaderPathResolver,
        NWB::Core::Assets::AssetRef<NWB::Impl::UiSkin>{},
        NWB::Impl::UiLayerPresentation::Scene
    );
    context.graphics.addRenderPassToBack(meshSkinningSystem);
    context.graphics.addRenderPassToBack(rendererSystem);
    context.graphics.addRenderPassToBack(uiLayerSystem);
    context.frameGraphRegistry.registerContributor(rendererSystem);

    return world;
}

void DestroyInitialProjectWorld(NWB::ProjectRuntimeContext& context, UniquePtr<NWB::Core::ECS::World>& world)noexcept{
    NWB_ASSERT(world);

    NWB::Impl::MeshSkinningSystem& meshSkinningSystem = *MakeNotNull(world->getSystem<NWB::Impl::MeshSkinningSystem>());
    NWB::Impl::RendererSystem& rendererSystem = *MakeNotNull(world->getSystem<NWB::Impl::RendererSystem>());
    NWB::Impl::UiLayerSystem& uiLayerSystem = *MakeNotNull(world->getSystem<NWB::Impl::UiLayerSystem>());

    context.frameGraphRegistry.unregisterContributor(rendererSystem);
    context.graphics.removeRenderPass(meshSkinningSystem);
    context.graphics.removeRenderPass(rendererSystem);
    context.graphics.removeRenderPass(uiLayerSystem);

    const b8 deviceIdle = context.graphics.waitForIdle();
    NWB_FATAL_ASSERT_MSG(deviceIdle || context.graphics.isDeviceLost(), __hidden_testbed_runtime::s_DestroyRequiresIdleOrLoss);

    world->clear();
    world.reset();
}

NotNullUniquePtr<NWB::Core::ECS::World> Project::CreateInitialWorldOrDie(NWB::ProjectRuntimeContext& context){
    auto world = CreateInitialProjectWorld(context);
    if(!world){
        NWB_LOGGER_FATAL(__hidden_testbed_runtime::s_InitWorldFailedText);
        throw RuntimeException(s_InitWorldFailedNarrow.data());
    }
    return MakeNotNullUnique(Move(*world));
}

Project::~Project()noexcept{
    unregisterInputHandler();
    destroyWorld();
}

b8 Project::onStartup(){
    if(!m_sceneSubsystems.loadProjectStartScene(m_context.assetManager))
        return false;
    if(!m_uiPreview.loadAssets(m_context.objectArena, m_context.assetManager, m_sceneSubsystems))
        return false;
    if(auto* const rendererSystem = m_world->getSystem<NWB::Impl::RendererSystem>())
        rendererSystem->setActiveSceneSubsystems(&m_sceneSubsystems);
    if(auto* const meshSkinningSystem = m_world->getSystem<NWB::Impl::MeshSkinningSystem>())
        meshSkinningSystem->setActiveSceneSubsystems(&m_sceneSubsystems);
    if(auto* const uiLayerSystem = m_world->getSystem<NWB::Impl::UiLayerSystem>())
        uiLayerSystem->setActiveSceneSubsystems(&m_sceneSubsystems);

    auto activeCameraEntity = m_world->createEntity();
    auto& activeCamera = activeCameraEntity.addComponent<NWB::Impl::Scene::ActiveCameraComponent>();
    const Float4 cameraPosition(0.0f, s_CharacterCameraTargetY, s_CameraStartDepth);
    activeCamera.camera = NWB::Impl::Scene::CreateSceneCameraEntity(*m_world, cameraPosition);
    NWB_ASSERT(activeCamera.camera.valid());
    auto& cameraTransform = *MakeNotNull(m_world->tryGetComponent<NWB::Impl::Scene::TransformComponent>(activeCamera.camera));
    StoreFloat(QuaternionRotationRollPitchYaw(0.0f, s_PI, 0.0f), cameraTransform.rotation);
    const auto directionalLight = NWB::Impl::Scene::CreateDirectionalLightEntity(
        *m_world,
        s_DefaultDirectionalLightPitch,
        s_DefaultDirectionalLightYaw,
        s_DefaultDirectionalLightRoll,
        s_DefaultDirectionalLightColor,
        s_DefaultDirectionalLightIntensity
    );
    const auto pointLight = NWB::Impl::Scene::CreatePointLightEntity(
        *m_world,
        s_PointLightPosition,
        s_PointLightColor,
        s_PointLightIntensity,
        s_PointLightRange
    );
    if(!directionalLight.valid() || !pointLight.valid())
        return false;

    if(!createDefaultScene())
        return false;
    NWB::Impl::ModelSystem& modelSystem = *MakeNotNull(m_world->getSystem<NWB::Impl::ModelSystem>());
    modelSystem.syncModelRuntimes();
    registerInputHandler();
    return true;
}

void Project::onShutdown(){
    unregisterInputHandler();
    m_cameraInput.clear();
    destroyWorld();
    NWB_LOGGER_ESSENTIAL_INFO(__hidden_testbed_runtime::s_ShutdownText);
}

b8 Project::onUpdate(f32 delta){
    const f32 safeDelta = FlyCameraMotion().sanitizeDelta(delta);

    updateMainCamera(safeDelta);
    const auto updated = m_world->tick(safeDelta);
    if(!updated){
        NWB_LOGGER_ERROR(NWB_TEXT("Project: world update rejected CPU work: {}"), static_cast<u32>(updated.error()));
        return false;
    }
    return true;
}

void Project::windowFocusUpdate(const b8 focused)noexcept{
    if(!focused)
        m_cameraInput.clear();
}

void Project::pointerCaptureLost()noexcept{
    m_cameraInput.pointerCaptureLost();
}

::NWB::Core::KeyboardInputResult Project::keyboardUpdate(const i32 key, [[maybe_unused]] const i32 scancode, const i32 action, [[maybe_unused]] const i32 mods)noexcept{
    if(action == NWB::Core::InputAction::Release)
        m_cameraInput.setKeyState(key, false);

    if(MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsKeyboard())
        return false;

    if(action == NWB::Core::InputAction::Press || action == NWB::Core::InputAction::Repeat)
        m_cameraInput.setKeyState(key, true);

    return false;
}

b8 Project::mousePosUpdate(const f64 xpos, const f64 ypos)noexcept{
    m_cameraInput.updatePointer(xpos, ypos, MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer());
    return false;
}

b8 Project::mouseButtonUpdate(const i32 button, const i32 action, [[maybe_unused]] const i32 mods)noexcept{
    m_cameraInput.updateButton(
        MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer(),
        button == NWB::Core::MouseButton::Right,
        action == NWB::Core::InputAction::Press,
        action == NWB::Core::InputAction::Release
    );
    return false;
}

b8 Project::createDefaultScene(){
    if(!m_sceneSubsystems.instantiateEntities(*m_world)){
        NWB_LOGGER_ERROR(__hidden_testbed_runtime::s_SceneEntitiesInvalidText);
        return false;
    }

    auto uiEntity = m_world->createEntity();
    auto& customUi = uiEntity.addComponent<NWB::Impl::UiPaintComponent>();
    customUi.paint = [this](NWB::Impl::UiPaintContext& context){
        drawUiControls(context);
        m_uiPreview.paint(context);
    };

    NWB_LOGGER_ESSENTIAL_INFO(__hidden_testbed_runtime::s_StartupSceneText, __hidden_testbed_runtime::s_DefaultSceneDescription);
    return true;
}

void Project::registerInputHandler(){
    if(m_inputRegistered)
        return;

    m_context.input.addHandlerToFront(*this);
    m_inputRegistered = true;
}

void Project::unregisterInputHandler()noexcept{
    if(!m_inputRegistered)
        return;

    m_context.input.removeHandler(*this);
    m_inputRegistered = false;
}

void Project::updateMainCamera(const f32 delta)noexcept{
    const b8 pointerCaptured = MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsPointer();
    const FlyCameraPointerDelta mouseDelta = m_cameraInput.takePointerDelta(pointerCaptured);

    const b8 keyboardCaptured = MakeNotNull(m_world->getSystem<NWB::Impl::UiLayerSystem>())->wantsKeyboard();
    const SIMDVectorSingle moveAxes = FlyCameraMotion().buildMoveAxes(
        keyboardCaptured,
        m_cameraInput.keyPressed(NWB::Core::Key::D),
        m_cameraInput.keyPressed(NWB::Core::Key::A),
        m_cameraInput.keyPressed(NWB::Core::Key::W),
        m_cameraInput.keyPressed(NWB::Core::Key::S)
    );
    const b8 boosted = !keyboardCaptured && (m_cameraInput.keyPressed(NWB::Core::Key::LeftShift) || m_cameraInput.keyPressed(NWB::Core::Key::RightShift));

    __hidden_testbed_runtime::ApplyFlyCameraInputToMainCamera(*m_world, moveAxes, boosted, VectorSet(mouseDelta.x, mouseDelta.y, 0.0f, 0.0f), delta);
}

void Project::destroyWorld()noexcept{
    if(!m_world.owner())
        return;

    DestroyInitialProjectWorld(m_context, m_world.owner());
}

void Project::drawUiControls(NWB::Impl::UiPaintContext& context){
    NWB::Impl::Ui::WindowOptions options;
    options.initialBounds = { s_GalleryMargin, s_GalleryMargin, s_OverviewWidth, s_OverviewHeight };
    options.contentHeightFirstUse = true;
    const auto gallery = m_uiPreview.galleryBounds(context);
    if(!gallery || !CanShowTestbedOverviewWindow(*gallery, options.initialBounds))
        return;
    NWB::Impl::Ui::SeparatorOptions separator;
    separator.thickness = 1.0f;
    if(context.ui.beginWindow("testbed_window", s_WindowTitleText, m_uiWindow, options)){
        const b8 declared = context.ui.label("renderer", s_RendererLine) && context.ui.separator("separator", separator) && context.ui.label("character", s_CharacterLine);
        NWB_FATAL_ASSERT(declared);
    }
    const b8 ended = context.ui.endWindow();
    NWB_FATAL_ASSERT(ended);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TESTBED_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB::ProjectFrameClientSize NWB::QueryProjectFrameClientSize(){
    return { s_DefaultProjectFrameClientWidth, s_DefaultProjectFrameClientHeight };
}

TStringView NWB::QueryProjectWindowTitle()noexcept{
    return Testbed::s_WindowTitle;
}

b8 NWB::ConfigureProjectRuntime(ProjectStartupContext& context)noexcept{
    Core::GraphicsRuntime& graphics = context.graphics;
    return graphics.setBindlessHeapAbi(Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi()) && graphics.setHDR10OutputEnabled(true);
}

UniquePtr<NWB::IProjectEntryCallbacks> NWB::CreateProjectEntryCallbacks(NWB::ProjectRuntimeContext& context){
    return MakeUnique<Testbed::Project>(context);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
