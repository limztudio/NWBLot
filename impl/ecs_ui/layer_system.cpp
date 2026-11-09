// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"
#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/runtime/runtime.h>
#include <global/scope_exit.h>
#include <global/termination.h>
#include <global/timer.h>
#include <global/simplemath.h>
#include "edit_box_host.h"
#include <impl/ecs_ui/edit_geometry.adhd.h>
#include <impl/ecs_ui/toolkit/edit/text_normalization.adhd.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiLayerSystem::UiLayerSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::GraphicsRuntime& graphics,
    Core::InputDispatcher& input,
    Core::IClipboardService& clipboard,
    Core::ITextInputService& textInput,
    Core::Assets::AssetManager& assetManager,
    ShaderPathResolveCallback shaderPathResolver,
    const Core::Assets::AssetRef<UiSkin>& skin,
    const UiLayerPresentation::Enum presentation
)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_world(world)
    , m_graphics(graphics)
    , m_input(input)
    , m_clipboard(clipboard)
    , m_textInput(textInput)
    , m_assetManager(assetManager)
    , m_defaultSceneSubsystems(arena)
    , m_skinSelection(MakeUiSkinSelectionState(skin))
    , m_paint(arena)
    , m_text(arena)
    , m_context(arena)
    , m_editHost(arena, m_context, textInput, clipboard)
    , m_ui(arena, m_context, m_paint, m_text)
    , m_liveRoots(arena)
    , m_rootIdentities(arena)
    , m_renderer(arena, graphics, assetManager, Move(shaderPathResolver))
    , m_presentation(presentation)
{
    writeAccess<UiPaintComponent>();
    m_ui.setEditHost(&m_editHost);
    m_liveRoots.reserve(Ui::s_InputMaxTargets);
    m_rootIdentities.reserve(Ui::s_InputMaxTargets);
    m_input.addHandlerToBack(*this);
    if(!m_input.windowFocused())
        windowFocusUpdate(false);
    if(m_presentation == UiLayerPresentation::Scene)
        m_graphics.setTaskGraphOutputLayerContributor(&m_renderer);
}

UiLayerSystem::~UiLayerSystem()noexcept{
    m_input.removeHandler(*this);
    m_graphics.clearTaskGraphOutputLayerContributor(m_renderer);
}

void UiLayerSystem::setActiveSceneSubsystems(const ActiveSceneSubsystems* const subsystems)noexcept{
    m_activeSceneSubsystems = subsystems;
    m_renderer.setActiveSceneSubsystems(subsystems);
}

b8 UiLayerSystem::validateResources(const u32 width, const u32 height, [[maybe_unused]] const u32 sampleCount){
    m_resourcesReady = false;
    m_width = width;
    m_height = height;
    if(width == 0u || height == 0u)
        return true;

    if(!m_activeSceneSubsystems){
        if(!m_defaultSceneSubsystems.loadProjectStartScene(m_assetManager))
            return false;
        m_activeSceneSubsystems = &m_defaultSceneSubsystems;
        m_renderer.setActiveSceneSubsystems(m_activeSceneSubsystems);
    }
    if(!m_activeSceneSubsystems->hasSubsystem<UiSubsystem>()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: active scene does not include required subsystem '{}'"), ConvertString(UiSubsystem::s_AssetTypeText));
        return false;
    }

    if(!m_fontsReady){
        Core::Alloc::ScratchArena scratchArena(s_UiLoadFontsArenaName);
        if(!loadFonts(scratchArena))
            return false;
    }
    if(!m_renderer.validateResources(width, height))
        return false;

    const UiSkinSelectionResult::Enum selection = EnsureUiSkinSelection(m_skinSelection, basicSkin(), [this](const Core::Assets::AssetRef<UiSkin>& ref){
        const Core::Assets::AssetRef<UiSkin> resolvedRef = resolveSkin(ref);
        if(ref == m_skinSelection.selected && m_skinAsset){
            const UiSkin* skin = Core::Assets::CastAsset<UiSkin>(m_skinAsset.get());
            return skin && ValidateUiSkinToolkitContract(*skin) && m_renderer.setSkin(resolvedRef, *skin, m_skinSelection.generation);
        }
        auto candidateAsset = m_assetManager.loadTypedSync<UiSkin>(resolvedRef.name(), s_UiLayerSystemOwnerName);
        const UiSkin* skin = candidateAsset ? candidateAsset->get() : nullptr;
        if(!skin || !ValidateUiSkinToolkitContract(*skin) || !m_renderer.setSkin(resolvedRef, *skin, m_skinSelection.generation))
            return false;
        m_skinAsset = Move(*candidateAsset);
        return true;
    });
    if(selection == UiSkinSelectionResult::Failed)
        return false;
    if(selection == UiSkinSelectionResult::DefaultFallback){
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: custom UI skin '{}' is unavailable; using engine default"), ConvertString(m_skinSelection.requested.name().resolvedText()));
    }

    f32 scaleX = 1.0f;
    f32 scaleY = 1.0f;
    m_graphics.getDPIScaleInfo(scaleX, scaleY);
    displayScaleChanged(scaleX, scaleY);
    m_resourcesReady = true;
    return true;
}

void UiLayerSystem::invalidateResources(){
    m_resourcesReady = false;
    m_frameDelta.clear();
    abandonPendingFrame(true);
    resetNativeInputState();
    m_renderer.invalidateResources();
}

b8 UiLayerSystem::prepareResources(Core::Framebuffer* framebuffer){
    if(!framebuffer || !m_resourcesReady)
        return false;
    const b8 prepared = m_renderer.prepareTaskGraphOutputLayer(m_graphics.acquiredPresentationFrame());
    if(!prepared && m_presentation == UiLayerPresentation::Standalone)
        m_graphics.requestDeviceRecreation();
    return prepared;
}

void UiLayerSystem::render(Core::Framebuffer* framebuffer){
    // The project chooses the final-output owner explicitly; optional layer failures cannot replace scene output.
    if(!framebuffer || !m_resourcesReady || m_presentation == UiLayerPresentation::Scene)
        return;
    if(!m_renderer.renderStandalone(m_graphics.acquiredPresentationFrame())){
        // The void render-pass API cannot abort presentation; recreation prevents presenting an unwritten acquired image.
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: standalone UI presentation failed"));
        m_graphics.requestDeviceRecreation();
    }
}

void UiLayerSystem::displayScaleChanged(const f32 scaleX, const f32 scaleY){
    NWB_ASSERT(IsFinite(scaleX) && scaleX > 0.0f && IsFinite(scaleY) && scaleY > 0.0f);
    m_frameDelta.clear();
    abandonPendingFrame(true);
    m_display = { static_cast<f32>(m_width) / scaleX, static_cast<f32>(m_height) / scaleY, scaleX, scaleY };
}

b8 UiLayerSystem::setInputBindings(const Span<const Ui::InputKeyBinding> bindings){
    synchronizeNativeInput();
    return m_context.input().setBindings(bindings);
}

b8 UiLayerSystem::loadFonts(Core::Alloc::ScratchArena& scratchArena){
    if(m_fontsReady)
        return true;
    const UiSubsystem* const uiSubsystem = m_activeSceneSubsystems ? m_activeSceneSubsystems->findSubsystem<UiSubsystem>() : nullptr;
    if(!uiSubsystem)
        return false;
    const usize fontCount = uiSubsystem->basicFonts.size();
    if(!ValidateUiSubsystemFontCount(fontCount, Ui::s_TextMaxFontSources))
        return false;
    Array<UniquePtr<Font>, Ui::s_TextMaxFontSources> assets;
    Array<UniquePtr<FontAtlas>, Ui::s_TextMaxFontSources> atlasAssets;
    Vector<Ui::FontSource, Core::Alloc::ScratchArena> sources(scratchArena);
    sources.reserve(fontCount);
    for(usize index = 0u; index < fontCount; ++index){
        const Core::Assets::AssetRef<FontAtlas>& atlasRef = uiSubsystem->basicFonts[index];
        auto loadedAtlas = m_assetManager.loadTypedSync<FontAtlas>(atlasRef.name(), s_UiLayerSystemOwnerName);
        if(!loadedAtlas)
            return false;
        atlasAssets[index] = Move(*loadedAtlas);
        const FontAtlas& atlas = *atlasAssets[index];
        const Core::Assets::AssetRef<Font>& fontRef = atlas.payload().font;
        auto font = m_assetManager.loadTypedSync<Font>(fontRef.name(), s_UiLayerSystemOwnerName);
        if(!font)
            return false;
        assets[index] = Move(*font);
        sources.push_back({ fontRef, *assets[index], 1u, &atlas });
    }
    // TextService owns the exact font bytes and immutable baked atlas versions; loaded asset objects can be released here.
    m_fontsReady = m_text.setFonts(scratchArena, sources);
    return m_fontsReady;
}

void UiLayerSystem::abandonPendingFrame(const b8 resetUi)noexcept{
    m_blockCommandChars = false;
    m_context.abandonFrame();
    m_editHost.reset();
    if(resetUi)
        m_ui.reset();
}

void UiLayerSystem::resetNormalizedInput(){
    m_blockCommandChars = false;
    m_context.resetInput();
    m_editHost.reset();
}

void UiLayerSystem::resetNativeInputState()noexcept{
    m_context.input().cancelDeviceInput(0u);
    m_nativeKeyOwners.fill(0u);
    m_pressedButtons = 0u;
    m_pointerOwner = UiInputOwner::Scene;
    m_blockNativeChars = false;
    m_blockCommandChars = false;
}

Core::Assets::AssetRef<UiSkin> UiLayerSystem::basicSkin()const noexcept{
    if(!m_activeSceneSubsystems)
        return {};
    const UiSubsystem* const uiSubsystem = m_activeSceneSubsystems->findSubsystem<UiSubsystem>();
    return uiSubsystem ? uiSubsystem->basicSkin : Core::Assets::AssetRef<UiSkin>{};
}

Core::Assets::AssetRef<UiSkin> UiLayerSystem::resolveSkin(const Core::Assets::AssetRef<UiSkin>& ref)const noexcept{
    const Core::Assets::AssetRef<UiSkin> fallbackSkin = basicSkin();
    if(!fallbackSkin.valid())
        return {};
    if(!ref.valid() || ref == fallbackSkin)
        return fallbackSkin;
    return m_activeSceneSubsystems->findSceneAsset<UiSkin>(ref.name());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


::NWB::Core::KeyboardInputResult UiLayerSystem::keyboardUpdate(const i32 key, [[maybe_unused]] const i32 scancode, const i32 action, const i32 mods){
    if(!Ui::IsValidInputKeyCode(key) || action < Core::InputAction::Release || action > Core::InputAction::Repeat)
        return false;
    synchronizeNativeInput();
    const usize slot = ResolveUiNativeKeySlot(key);
    b8 held = false;
    u8 previousOwner = 0u;
    b8 customOwned = false;
    ResolveUiNativeKeyAdmission(m_nativeKeyOwners[slot], m_context.input().ownsKey(key), held, previousOwner, customOwned);
    const b8 isRelease = action == Core::InputAction::Release;
    if(!isRelease)
        m_blockCommandChars = false;
    if(isRelease || customOwned || !held){
        Ui::InputEvent event;
        event.type = isRelease ? Ui::InputEventType::KeyUp : Ui::InputEventType::KeyDown;
        event.key = key;
        event.shift = (mods & Core::InputModifier::Shift) != 0;
        event.control = (mods & Core::InputModifier::Control) != 0;
        event.alt = (mods & Core::InputModifier::Alt) != 0;
        event.super = (mods & Core::InputModifier::Super) != 0;
        event.repeat = !isRelease && (action == Core::InputAction::Repeat || held);
        routeInput(event, isRelease ? nullptr : &m_blockCommandChars);
    }
    const b8 routerClaimsKey = m_context.input().hasPopup() || m_context.input().focus().valid() || m_context.input().ownsKey(key);
    b8 consumed = false;
    FinalizeUiNativeKeyState(isRelease, held, previousOwner, routerClaimsKey, m_nativeKeyOwners[slot], m_blockNativeChars, consumed);
    return { consumed, m_blockCommandChars };
}

b8 UiLayerSystem::keyboardCharInput(const u32 unicode, const i32 mods){
    synchronizeNativeInput();
    m_editHost.collectNative();
    if(m_editHost.hasTextFocus()){
        if((mods & Core::InputModifier::Control) != 0 && (mods & Core::InputModifier::Alt) == 0)
            return true;
        return m_editHost.character(unicode);
    }
    if(m_blockNativeChars || m_context.input().hasPopup() || m_context.input().focus().valid())
        return true;
    return false;
}

b8 UiLayerSystem::mousePosUpdate(const f64 xpos, const f64 ypos){
    if(!ValidUiMouseCoordinate(xpos, ypos))
        return false;
    synchronizeNativeInput();
    m_pointer = { static_cast<f32>(xpos), static_cast<f32>(ypos) };
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.position = m_pointer;
    event.hoverCapable = true;
    if(m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Scene){
        event.phase = Ui::PointerPhase::Leave;
        routePointerInput(event);
        return false;
    }
    event.phase = Ui::PointerPhase::Move;
    b8 consumed = false;
    routePointerInput(event, &consumed);
    return consumed || (m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Custom);
}

b8 UiLayerSystem::mouseButtonUpdate(const i32 button, const i32 action, const i32 mods){
    if(button < Core::MouseButton::Left || button > Core::MouseButton::Button8 || (action != Core::InputAction::Press && action != Core::InputAction::Release))
        return false;
    synchronizeNativeInput();
    const u32 bit = 1u << static_cast<u32>(button);
    const b8 firstPress = action == Core::InputAction::Press && (m_pressedButtons & bit) == 0u;
    if(action == Core::InputAction::Press && m_pressedButtons == 0u){
        const b8 acceptsSource = m_context.input().acceptsPointerSource(Ui::s_NativePointerSource);
        const b8 nativeActive = m_context.input().activePointerSource() == Ui::s_NativePointerSource;
        const b8 consumed = nativeActive ? m_context.input().wouldConsumePointer(m_pointer) : m_context.input().hasPopup() || m_context.input().hitTest(m_pointer).valid();
        m_pointerOwner = consumed ? UiInputOwner::Custom : UiInputOwner::Scene;
        if(acceptsSource && m_pointerOwner != UiInputOwner::Custom)
            m_context.input().clearFocus();
    }
    if(action == Core::InputAction::Press)
        m_pressedButtons |= bit;
    const u8 owner = m_pointerOwner;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.position = m_pointer;
    event.extend = (mods & Core::InputModifier::Shift) != 0;
    event.hoverCapable = true;
    if(owner == UiInputOwner::Custom && (button == Core::MouseButton::Left || button == Core::MouseButton::Right)){
        event.phase = action == Core::InputAction::Release ? Ui::PointerPhase::Release : Ui::PointerPhase::Press;
        event.intent = button == Core::MouseButton::Left ? Ui::PointerIntent::Activate : Ui::PointerIntent::ContextMenu;
        if(event.phase == Ui::PointerPhase::Press && event.intent == Ui::PointerIntent::Activate)
            event.timestampMs = Max<u64>(1u, DurationInMS<u64>(TimerNow()));
        routePointerInput(event);
    }
    if(owner == UiInputOwner::Custom && button == Core::MouseButton::Middle && action == Core::InputAction::Press && m_context.input().acceptsPointerSource(Ui::s_NativePointerSource)){
        m_editHost.collectNative();
        const b8 pasted = m_editHost.pastePrimary(m_pointer);
        if(!pasted)
            m_editHost.synchronizeFocus();
    }
    if(firstPress && (owner == UiInputOwner::Scene || (button != Core::MouseButton::Left && button != Core::MouseButton::Right))){
        event.phase = Ui::PointerPhase::Leave;
        routePointerInput(event);
    }
    if(action == Core::InputAction::Release){
        m_pressedButtons &= ~bit;
        if(m_pressedButtons == 0u){
            m_pointerOwner = UiInputOwner::Scene;
            event.phase = Ui::PointerPhase::Move;
            event.timestampMs = 0u;
            routePointerInput(event);
        }
    }
    return owner != UiInputOwner::Scene;
}

b8 UiLayerSystem::mouseScrollUpdate(const f64 xoffset, const f64 yoffset){
    if(!ValidUiMouseScroll(xoffset, yoffset))
        return false;
    synchronizeNativeInput();
    if(m_pressedButtons != 0u && m_pointerOwner == UiInputOwner::Scene)
        return false;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Scroll;
    event.position = m_pointer;
    event.scrollX = xoffset;
    event.scrollY = yoffset;
    event.hoverCapable = true;
    b8 consumed = false;
    routePointerInput(event, &consumed);
    return consumed || m_pointerOwner == UiInputOwner::Custom;
}

void UiLayerSystem::windowFocusUpdate(const b8 focused){
    Ui::InputEvent event;
    event.type = focused ? Ui::InputEventType::FocusGained : Ui::InputEventType::FocusLost;
    routeInput(event);
    if(!focused)
        resetNativeInputState();
}

void UiLayerSystem::pointerLeave(){
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Leave;
    event.position = m_pointer;
    event.hoverCapable = true;
    routePointerInput(event);
}

void UiLayerSystem::pointerCaptureLost(){
    if(m_pressedButtons == 0u)
        return;
    Ui::PointerInput event;
    event.source = Ui::s_NativePointerSource;
    event.phase = Ui::PointerPhase::Cancel;
    event.position = m_pointer;
    event.hoverCapable = true;
    routePointerInput(event);
    m_pressedButtons = 0u;
    m_pointerOwner = UiInputOwner::Scene;
}

b8 UiLayerSystem::wantsPointer()const noexcept{
    if(m_pressedButtons != 0u)
        return m_pointerOwner != UiInputOwner::Scene;
    return m_resourcesReady && m_context.input().wantsPointer();
}

void UiLayerSystem::routeInput(const Ui::InputEvent& event, b8* blockText){
    if(blockText)
        *blockText = false;
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queue(event);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing normalized input"));
        resetNormalizedInput();
        return;
    }
    const b8 submit = m_editHost.prepareSubmit(*normalized);
    const Ui::InputRoutingResult result = m_context.input().process();
    m_editHost.input(*normalized, previousCapture, submit);
    if(blockText){
        // Core captures this policy only if the native handler owns the physical sequence, including inert repeats.
        *blockText = normalized->type == Ui::InputEventType::CommandDown && normalized->command != Ui::InputCommand::None && !normalized->allowText;
    }
    LogUiInputRoutingOverflows(result.activationOverflow, result.gestureOverflow);
}

void UiLayerSystem::routePointerInput(const Ui::PointerInput& input, b8* consumed){
    if(consumed)
        *consumed = false;
    m_editHost.collectNative();
    const Ui::WidgetId previousCapture = m_context.input().capture();
    const auto normalized = m_context.input().queuePointer(input);
    if(!normalized){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: invalid or overflowing pointer input"));
        resetNormalizedInput();
        return;
    }
    const Ui::InputRoutingResult result = m_context.input().process();
    if(result.pointerRouted)
        m_editHost.input(*normalized, previousCapture, false);
    LogUiInputRoutingOverflows(result.activationOverflow, result.gestureOverflow);
    if(consumed)
        *consumed = result.pointerConsumed;
}

void UiLayerSystem::synchronizeNativeInput(){
    // Native dispatch occurs outside world system execution; use its joined boundary before reading host roots.
    m_world.taskScope().wait();
    synchronizeInput();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, Core::CpuTaskAdmissionError::Enum> UiLayerSystem::update([[maybe_unused]] Core::ECS::World& world, const f32 delta){
    m_frameDelta.add(delta);
    synchronizeInput();
    if(!m_resourcesReady || m_renderer.hasPendingFrame() || m_context.ready())
        return {};
    AdvanceUiLayerFrameGeneration(m_frameGeneration);
    const UiSkin& skin = *MakeNotNull(Core::Assets::CastAsset<UiSkin>(m_skinAsset.get()));
    if(!m_context.beginFrame(m_frameGeneration))
        TerminateInvariant();
    ScopeExit abandon([this]()noexcept{ abandonPendingFrame(true); });
    const f32 frameDelta = m_frameDelta.consume();
    m_editHost.beginFrame(m_frameGeneration, m_display);
    if(m_editHost.takeClipboardFailure())
        NWB_LOGGER_WARNING(NWB_TEXT("UiLayerSystem: clipboard publication failed"));
    m_ui.reset();
    m_ui.setSkin(skin);
    m_paint.begin(m_display, m_frameGeneration, m_skinSelection.generation, Core::Assets::AssetRef<UiSkin>(skin.virtualPath()), skin);
    m_paint.reserve(s_UiLayerPaintReserveQuads);
    m_ui.setDeltaSeconds(frameDelta);
    m_ui.setPointerBusy(m_pressedButtons != 0u || m_context.input().primaryDown() || m_context.input().secondaryDown());
    UiPaintContext context{ m_world, m_clipboard, m_textInput, m_paint, m_text, m_display, m_ui, Core::ECS::s_InvalidEntityId, frameDelta };
    Sort(m_liveRoots.begin(), m_liveRoots.end(), &IsUiLiveRootOrderedBefore);
    for(const auto& root : m_liveRoots){
        UiPaintComponent* component = m_world.tryGetComponent<UiPaintComponent>(root.entity);
        if(!component || !component->visible || !component->paint)
            continue;
        if(!m_context.beginRoot({ root.entity.id, 1u }))
            break;
        context.entity = root.entity;
        component->paint.invoke(context);
        if(!m_ui.balanced())
            m_context.fail();
        if(!m_context.endRoot())
            break;
    }
    if(!m_context.finishFrame()){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: rejected unbalanced or invalid UI declarations"));
        return {};
    }
    m_editHost.finishFrame();
    if(!m_renderer.submit(m_paint.freeze())){
        NWB_LOGGER_ERROR(NWB_TEXT("UiLayerSystem: GPU renderer rejected a new paint snapshot"));
        abandonPendingFrame();
        abandon.release();
        return {};
    }
    abandon.release();
    return {};
}

void UiLayerSystem::collectRoots(){
    m_liveRoots.clear();
    m_rootIdentities.clear();
    m_world.view<UiPaintComponent>().each([this](const Core::ECS::EntityID entity, UiPaintComponent& component){
        if(!component.visible || !component.paint.present())
            return;
        m_liveRoots.push_back({ entity, component.order });
        m_rootIdentities.push_back({ entity.id, 1u });
    });
}

void UiLayerSystem::synchronizeInput(){
    collectRoots();
    m_context.retainRoots(m_rootIdentities);
    if(m_context.ready() && m_renderer.lastAcceptedGeneration() == m_context.readyGeneration()){
        const auto status = m_renderer.lastAcceptedPresentationStatus();
        if(status == Core::PresentationReceiptStatus::Accepted){
            const b8 committed = m_context.commitFrame(m_renderer.lastAcceptedGeneration());
            NWB_FATAL_ASSERT(committed);
            m_editHost.commitFrame(m_renderer.lastAcceptedGeneration());
        }
        else if(status == Core::PresentationReceiptStatus::Rejected)
            abandonPendingFrame();
    }
    m_editHost.synchronizeFocus();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


UiEditBoxHost::UiEditBoxHost(Core::Alloc::GlobalArena& arena, Ui::Context& context, Core::ITextInputService& textInput, Core::IClipboardService& clipboard)
    : m_arena(arena)
    , m_context(context)
    , m_textInput(textInput)
    , m_clipboardService(clipboard)
    , m_session(arena, textInput)
    , m_clipboard(arena, clipboard)
    , m_primary(arena, clipboard)
    , m_publications(arena, clipboard)
    , m_entries(arena)
    , m_events(arena)
{
    m_entries.reserve(s_UiEditBoxMaxEntries);
    m_events.reserve(Ui::s_InputMaxEvents);
}

Ui::EditBoxResult UiEditBoxHost::editNavigated(
    const Ui::WidgetState& widget,
    const Ui::EditBoxOptions& options,
    const Ui::PopupToken& popup,
    Ui::EditModel& model,
    Ui::EditNavigationState& navigation,
    Ui::IEditNavigationResolver& resolver,
    Ui::IEditActionSink& actions
){
    NavigationBorrow borrowed{ navigation, resolver };
    return editBorrowed(widget, options, popup, model, &actions, &borrowed);
}

b8 UiEditBoxHost::publish(const Ui::WidgetState& widget, const Ui::EditBoxView& view, const Ui::EditBoxPlacement& placement, const Ui::EditBoxOptions& options){
    if(rejectBorrowedMutation())
        return false;
    Entry* entry = find(widget.id);
    if(!entry || entry->owner.declarationGeneration != widget.declarationGeneration || entry->popup != m_context.popupToken() || !view.ready())
        return false;
    UiEditGeometry().publish(
        entry->popup,
        placement,
        view.caretStops(),
        view.caretGeometry().lines(),
        view.textMode(),
        options,
        m_generation,
        entry->expected.m_expectedRevision,
        entry->expected.m_expectedExternalRevision,
        entry->owner.modelGeneration,
        entry->candidate
    );
    return true;
}

void UiEditBoxHost::beginFrame(const u64 generation, const Ui::DisplayMetrics& display){
    if(rejectBorrowedMutation())
        return;
    drainPublications();
    collectNative();
    m_generation = generation;
    m_display = display;
}

void UiEditBoxHost::finishFrame(){
    if(rejectBorrowedMutation())
        return;
    for(usize index = m_entries.size(); index > 0u; --index){
        const auto& entry = m_entries[index - 1u];
        if(entry.seen == m_generation)
            continue;
        if(m_session.owner() == entry.owner || (m_clipboard.pending() && m_clipboardOwner == entry.owner) || (m_primary.pending() && m_primaryOwner == entry.owner))
            cancelTransfers();
        discard(entry.owner);
        m_entries.erase(m_entries.begin() + static_cast<isize>(index - 1u));
    }
    synchronizeFocus();
}

void UiEditBoxHost::commitFrame(const u64 generation){
    if(rejectBorrowedMutation())
        return;
    if(m_context.input().layoutGeneration() != generation)
        return;
    for(auto& entry : m_entries)
        UiEditGeometry().commit(generation, entry.candidate, entry.displayed);
    if(m_session.token().valid()){
        const Entry* entry = find(m_session.owner().widget);
        if(entry && entry->owner == m_session.owner() && entry->displayed.generation == generation){
            const auto status = m_textInput.updateCaret(m_session.token(), nativeCaret(entry->displayed));
            if(status != Core::TextInputAdmission::Accepted && !m_session.cancel())
                TerminateInvariant();
        }
    }
    synchronizeFocus();
}

b8 UiEditBoxHost::hasTextFocus()const noexcept{
    return focusedTextEntry() != nullptr;
}

b8 UiEditBoxHost::wantsTextInput()const noexcept{
    if(!m_context.input().windowFocused())
        return false;
    const Entry* entry = focusedTextEntry();
    return entry && !entry->displayed.options.readOnly;
}

b8 UiEditBoxHost::takeClipboardFailure()noexcept{
    if(rejectBorrowedMutation())
        return false;
    const b8 failed = m_clipboardFailure;
    m_clipboardFailure = false;
    return failed;
}

void UiEditBoxHost::synchronizeFocus(){
    if(rejectBorrowedMutation())
        return;
    const Entry* entry = focusedTextEntry();
    const b8 focused = entry != nullptr;
    m_clickTracker.retainFocus(focused ? entry->owner : UiTextEditOwner{});
    for(auto& candidate : m_entries){
        const b8 active = focused && candidate.owner == entry->owner;
        if(candidate.actionCapable){
            if(active && !candidate.focused){
                candidate.focused = true;
                candidate.focusGeneration = nextFocusGeneration();
                if(candidate.navigationInstanceGeneration != 0u){
                    Event event(m_arena);
                    event.owner = candidate.owner;
                    event.focusGeneration = candidate.focusGeneration;
                    event.kind = UiEditBoxEventKind::Focus;
                    if(!append(Move(event)))
                        return;
                }
            }
            else if(!active && candidate.focused){
                Event event(m_arena);
                event.owner = candidate.owner;
                event.focusGeneration = candidate.focusGeneration;
                event.kind = UiEditBoxEventKind::Blur;
                candidate.focused = false;
                if(!append(Move(event)))
                    return;
            }
        }
        if(active)
            continue;
        if(!candidate.actionCapable)
            discard(candidate.owner);
        candidate.dragging = false;
        candidate.wordDragging = false;
    }
    if((m_session.token().valid() && (!focused || entry->owner != m_session.owner())) || (m_clipboard.pending() && (!focused || entry->owner != m_clipboardOwner)) || (m_primary.pending() && (!focused || entry->owner != m_primaryOwner)))
        cancelTransfers();
}

void UiEditBoxHost::reset(){
    if(rejectBorrowedMutation())
        return;
    cancelTransfers();
    m_clickTracker.cancel();
    m_activeSelectionSequence = 0u;
    m_selectionSource = {};
    m_entries.clear();
    m_events.clear();
    m_queuedTextBytes = 0u;
    m_context.input().clearFocus();
}

UiEditBoxHost::Entry* UiEditBoxHost::find(const Ui::WidgetId widget)noexcept{
    return const_cast<Entry*>(static_cast<const UiEditBoxHost&>(*this).find(widget));
}

const UiEditBoxHost::Entry* UiEditBoxHost::find(const Ui::WidgetId widget)const noexcept{
    for(const auto& entry : m_entries){
        if(entry.widget.id == widget)
            return &entry;
    }
    return nullptr;
}

const UiEditBoxHost::Entry* UiEditBoxHost::focusedTextEntry()const noexcept{
    const Entry* entry = find(m_context.input().focus());
    if(!entry || !UiEditGeometry().focusedReady(entry->displayed, entry->owner, entry->popup))
        return nullptr;
    const Ui::HitTarget* target = m_context.input().findTarget(entry->widget.id, entry->owner.declarationGeneration);
    return target && target->enabled && target->textEditable && target->popup == entry->popup ? entry : nullptr;
}

void UiEditBoxHost::discard(const UiTextEditOwner& owner)noexcept{
    for(usize index = m_events.size(); index > 0u; --index){
        if(m_events[index - 1u].owner == owner){
            m_queuedTextBytes -= m_events[index - 1u].native.text.size();
            m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
        }
    }
}

void UiEditBoxHost::cancelTransfers(){
    const b8 text = m_session.cancel();
    const b8 clipboard = m_clipboard.cancel();
    const b8 primary = m_primary.cancel();
    if(!text || !clipboard || !primary)
        TerminateInvariant();
}

void UiEditBoxHost::drainPublications(){
    const auto completion = m_publications.drain();
    m_clipboardFailure |= completion.failed != 0u || completion.status == UiClipboardPublicationStatus::WrongThread;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 UiEditBoxHost::apply(const Ui::EditBoxOptions& options, Event& event, Entry& entry, Ui::EditModel& model, Ui::EditBoxResult& result, Ui::IEditActionSink* actions, NavigationBorrow* navigation){
    const u64 revision = model.revision();
    const u64 externalRevision = model.externalRevision();
    const u64 selectionGeneration = model.selectionGeneration();
    const u64 compositionGeneration = model.compositionGeneration();
    const b8 clipboard = m_clipboard.cancel();
    const b8 primary = m_primary.cancel();
    if(!clipboard || !primary)
        TerminateInvariant();
    if(m_borrowRejected)
        return false;
    const b8 composing = model.composition().active;
    if(event.kind == UiEditBoxEventKind::Command){
        if(event.navigation)
            return navigation ? applyNavigation(event, event.navigationDirection, model, *navigation) : true;
        const auto command = Ui::ApplyEditCommand(model, event.command, options.readOnly);
        result.submitted |= command.submitted;
        result.cancelled |= command.cancelled;
        if(actions && (command.submitted || command.cancelled)){
            if(!applyAction(options, command.submitted ? Ui::EditAction::Submit : Ui::EditAction::Cancel, entry, model, result, *actions, navigation))
                return false;
        }
        if(command.cancelled){
            const b8 cancelFocus = !actions || (entry.focused && entry.focusGeneration == event.focusGeneration && m_context.input().focus() == entry.widget.id);
            if(actions){
                entry.retiredFocusGeneration = Max(entry.retiredFocusGeneration, event.focusGeneration);
                if(entry.focusGeneration == event.focusGeneration)
                    entry.focused = false;
            }
            if(cancelFocus){
                if(!m_context.input().dismissPopup(Ui::PopupDismissReason::Cancel))
                    m_context.input().clearFocus();
            }
        }
        if(command.clipboard != Ui::EditClipboardAction::None){
            if(command.clipboard == Ui::EditClipboardAction::Copy){
                if(!model.hasSelection())
                    return true;
                drainPublications();
                const auto status = m_publications.request(model.selectedText());
                m_clipboardFailure |= status != UiClipboardPublicationStatus::Pending;
            }
            else{
                const auto request = m_clipboard.request(entry.owner, model, command.clipboard, Core::ClipboardChannel::Clipboard, options.readOnly);
                if(request.status == UiEditClipboardStatus::Pending)
                    m_clipboardOwner = entry.owner;
            }
        }
    }
    else if(event.kind == UiEditBoxEventKind::Character){
        if(!options.readOnly && !model.replaceSelection(event.native.text))
            return true;
    }
    else if(event.kind == UiEditBoxEventKind::Native){
        if(options.readOnly || event.native.token == entry.rejectedNative)
            return true;
        if(event.native.kind == Core::TextInputEventKind::Commit && event.native.text.size() == 1u && !IsTextInputCodePoint(static_cast<u8>(event.native.text.front())))
            return true;
        const b8 matching = MatchesUiEditNativeSurroundingState(
            model.revision(),
            event.surroundingModelRevision,
            model.externalRevision(),
            event.geometryExternalRevision,
            model.selectionGeneration(),
            event.surroundingSelectionGeneration,
            model.anchor(),
            event.surroundingAnchor,
            model.caret(),
            event.surroundingCaret,
            !actions || (m_session.owner() == entry.owner && m_session.token() == event.native.token)
        );
        const auto status = ApplyUiTextEditEvent(model, event.native, event.surroundingRevision, matching);
        if(status != UiTextEditStatus::Applied){
            model.cancelComposition();
            entry.rejectedNative = event.native.token;
            if(m_session.owner() == entry.owner && !m_session.cancel())
                TerminateInvariant();
        }
        entry.preeditCaretVisible = event.native.kind != Core::TextInputEventKind::Preedit || event.native.caretVisible;
    }
    else if(event.kind == UiEditBoxEventKind::Selection){
        if(model.revision() != event.geometryRevision || model.externalRevision() != event.geometryExternalRevision){
            if(event.completed){
                entry.dragging = false;
                entry.wordDragging = false;
            }
            return true;
        }
        if(event.wordSelect){
            const auto range = model.wordRangeAt(event.wordPosition);
            if(!range || !model.setSelection(range->begin, range->end))
                return true;
            entry.wordDragStart = range->begin;
            entry.wordDragEnd = range->end;
            entry.wordDragging = true;
        }
        else if(event.dragging && entry.dragging && entry.wordDragging){
            const auto range = model.wordRangeAt(event.wordPosition);
            if(!range)
                return true;
            const Ui::EditWordRange selection = ResolveUiEditWordDragSelection(event.wordPosition, entry.wordDragStart, entry.wordDragEnd, *range);
            if(!model.setSelection(selection.begin, selection.end))
                return true;
        }
        else{
            const usize anchor = ResolveUiEditSelectionAnchor(event.dragging, entry.dragging, entry.dragAnchor, event.extend, model.anchor(), event.position);
            if(!model.setSelection(anchor, event.position))
                return true;
            entry.dragAnchor = anchor;
            entry.wordDragging = false;
        }
        entry.dragging = !event.completed;
        if(event.completed)
            entry.wordDragging = false;
        if(model.hasSelection() && m_clipboardService.capabilities(Core::ClipboardChannel::PrimarySelection).writeText){
            const auto request = m_primary.request(entry.owner, model, Ui::EditClipboardAction::PublishSelection, Core::ClipboardChannel::PrimarySelection, options.readOnly);
            if(request.status == UiEditClipboardStatus::Pending)
                m_primaryOwner = entry.owner;
        }
    }
    else if(event.kind == UiEditBoxEventKind::PastePrimary){
        const auto request = m_clipboard.request(entry.owner, model, Ui::EditClipboardAction::Paste, Core::ClipboardChannel::PrimarySelection, options.readOnly);
        if(request.status == UiEditClipboardStatus::Pending)
            m_clipboardOwner = entry.owner;
    }
    else if(event.kind == UiEditBoxEventKind::Blur && actions){
        entry.retiredFocusGeneration = event.focusGeneration;
        return applyAction(options, Ui::EditAction::Blur, entry, model, result, *actions, navigation);
    }
    else if(event.kind == UiEditBoxEventKind::Focus && navigation){
        navigation->state.reset();
    }
    if(composing && !model.composition().active && event.kind != UiEditBoxEventKind::Native){
        entry.rejectedNative = m_session.token();
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    if(m_borrowRejected)
        return false;
    if(navigation && (model.revision() != revision || model.externalRevision() != externalRevision || model.selectionGeneration() != selectionGeneration || model.compositionGeneration() != compositionGeneration))
        navigation->state.reset();
    return true;
}

b8 UiEditBoxHost::applyAction(const Ui::EditBoxOptions& options, const Ui::EditAction::Enum action, Entry& entry, Ui::EditModel& model, Ui::EditBoxResult& result, Ui::IEditActionSink& actions, NavigationBorrow* navigation){
    const u64 revision = model.revision();
    const u64 selectionGeneration = model.selectionGeneration();
    const u64 compositionGeneration = model.compositionGeneration();
    if(m_clipboard.pending() && m_clipboardOwner == entry.owner && !m_clipboard.cancel())
        TerminateInvariant();
    if(m_primary.pending() && m_primaryOwner == entry.owner && !m_primary.cancel())
        TerminateInvariant();
    if(action == Ui::EditAction::Blur || action == Ui::EditAction::Abandon)
        model.cancelComposition();
    const u64 externalRevision = model.externalRevision();
    if(m_borrowRejected || !actions.apply(model, action, options.readOnly) || m_borrowRejected)
        return false;
    if(
        navigation
        && ShouldResetUiEditNavigationAfterAction(
            action,
            revision,
            externalRevision,
            selectionGeneration,
            compositionGeneration,
            model.revision(),
            model.externalRevision(),
            model.selectionGeneration(),
            model.compositionGeneration()
        )
    )
        navigation->state.reset();
    MergeUiEditActionToResult(action, result);
    if(action != Ui::EditAction::Submit || model.externalRevision() != externalRevision){
        // Copied commits still apply at their ordered position; surrounding deletion cannot address this retired session.
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
    }
    return !m_borrowRejected;
}

b8 UiEditBoxHost::applyNavigation(const Event& event, const Ui::EditNavigationDirection::Enum direction, Ui::EditModel& model, NavigationBorrow& navigation){
    if(m_borrowRejected || m_context.failed())
        return false;
    if(model.textMode() != Ui::EditTextMode::Multiline || model.composition().active)
        return true;
    if(!IsValidUiEditNavigationViewportHeight(direction, event.navigationViewportHeight))
        return false;
    const Ui::TextAreaModelStamp expected = model.stamp();
    const Ui::EditNavigationSnapshot preferred = navigation.state.snapshot();
    const auto target = navigation.resolver.resolve(model, direction, preferred, event.navigationViewportHeight);
    if(m_borrowRejected || m_context.failed() || !model.matchesStamp(expected) || model.composition().active || !navigation.state.matches(preferred) || !target || !IsFinite(target->preferredX))
        return false;
    if(!IsCommittedByteOnGraphemeBoundary(model.graphemeBoundaries(), target->committedByte))
        return false;
    if(!model.setSelection(event.command.extend ? expected.anchor : target->committedByte, target->committedByte))
        return false;
    if(!navigation.state.setPreferredX(target->preferredX))
        TerminateInvariant();
    return true;
}

b8 UiEditBoxHost::rejectBorrowedMutation()noexcept{
    if(!m_borrowed)
        return false;
    m_borrowRejected = true;
    return true;
}

u64 UiEditBoxHost::nextFocusGeneration()noexcept{
    return AdvanceUiEditFocusGeneration(m_focusGeneration);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void UiEditBoxHost::collectNative(){
    if(rejectBorrowedMutation())
        return;
    for(usize index = 0u; m_session.token().valid() && index <= Core::s_TextInputMaxEvents; ++index){
        Event event(m_arena);
        const auto poll = m_session.pollOwned(event.native);
        if(poll == Core::TextInputPollResult::Pending)
            return;
        if(poll != Core::TextInputPollResult::Event){
            discard(m_session.owner());
            if(!m_session.cancel())
                TerminateInvariant();
            return;
        }
        if(index == Core::s_TextInputMaxEvents || event.native.token != m_session.token() || event.native.sequence <= m_lastNativeSequence){
            reset();
            return;
        }
        m_lastNativeSequence = event.native.sequence;
        m_clickTracker.cancel();
        event.owner = m_session.owner();
        const Entry* entry = find(event.owner.widget);
        event.focusGeneration = entry && entry->owner == event.owner ? entry->focusGeneration : 0u;
        event.kind = UiEditBoxEventKind::Native;
        event.surroundingRevision = m_session.surroundingRevision();
        event.surroundingModelRevision = m_nativePublished.revision;
        event.surroundingSelectionGeneration = m_nativePublished.selectionGeneration;
        event.geometryExternalRevision = m_nativePublished.externalRevision;
        event.surroundingAnchor = m_nativePublished.anchor;
        event.surroundingCaret = m_nativePublished.caret;
        if(event.native.kind == Core::TextInputEventKind::Preedit && !event.native.text.empty())
            m_context.input().cancelSubmitCommands(event.owner.widget);
        if(!append(Move(event)))
            return;
    }
}

void UiEditBoxHost::input(const Ui::InputEvent& input, const Ui::WidgetId previousCapture, const b8 submit){
    if(rejectBorrowedMutation())
        return;
    const b8 selectionRelease = input.type == Ui::InputEventType::PrimaryUp && input.source == m_selectionSource;
    ScopeExit finishSelection([this, selectionRelease]()noexcept{
        if(selectionRelease){
            m_activeSelectionSequence = 0u;
            m_selectionSource = {};
        }
    });

    if(input.type == Ui::InputEventType::FocusLost){
        reset();
        return;
    }
    synchronizeFocus();
    if(input.type == Ui::InputEventType::PointerMove && m_activeSelectionSequence != 0u && input.source == m_selectionSource)
        m_clickTracker.move(input.position);
    if(input.type == Ui::InputEventType::PointerCaptureLost){
        if(m_activeSelectionSequence == 0u || input.source != m_selectionSource)
            return;
        m_clickTracker.cancel();
        for(auto& entry : m_entries){
            entry.dragging = false;
            entry.wordDragging = false;
        }
        for(usize index = m_events.size(); index > 0u; --index){
            const Event& event = m_events[index - 1u];
            if(event.kind == UiEditBoxEventKind::Selection && event.selectionSequence == m_activeSelectionSequence && event.selectionSource == input.source)
                m_events.erase(m_events.begin() + static_cast<isize>(index - 1u));
        }
        m_activeSelectionSequence = 0u;
        m_selectionSource = {};
        return;
    }
    if(input.type == Ui::InputEventType::CommandDown || input.type == Ui::InputEventType::CommandUp){
        m_clickTracker.cancel();
        Entry* entry = find(m_context.input().focus());
        const b8 released = input.type == Ui::InputEventType::CommandUp;
        if(!input.edit || !entry || !hasTextFocus() || (released ? !submit : !m_context.input().canEditCommand(input)))
            return;
        Event event(m_arena);
        event.owner = entry->owner;
        event.focusGeneration = entry->focusGeneration;
        if(released)
            event.command.command = Ui::EditCommand::Submit;
        else{
            const Ui::InputCommandIntent intent{ input.command, input.extend, input.edit };
            event.command = Ui::TranslateEditCommand(intent, input.repeat, entry->displayed.textMode);
            if(event.command.command == Ui::EditCommand::Submit){
                if(compositionActive(*entry))
                    m_context.input().cancelSubmitCommands(entry->widget.id);
                return;
            }
            const auto navigationDirection = Ui::TryTranslateEditNavigation(intent);
            event.navigation = navigationDirection.has_value();
            if(navigationDirection){
                event.navigationDirection = *navigationDirection;
                if(entry->navigationInstanceGeneration == 0u || entry->displayed.textMode != Ui::EditTextMode::Multiline)
                    return;
                event.navigationViewportHeight = entry->displayed.placement.content.height;
            }
            else if(event.command.command == Ui::EditCommand::None)
                return;
        }
        if(!append(Move(event)))
            return;
    }
    else if((input.type == Ui::InputEventType::PrimaryDown || input.type == Ui::InputEventType::PrimaryUp || input.type == Ui::InputEventType::PointerMove) && (input.type != Ui::InputEventType::PointerMove || previousCapture.valid())){
        if(input.type == Ui::InputEventType::PrimaryDown){
            if(m_selectionSequence == Limit<u64>::s_Max)
                TerminateInvariant();
            m_activeSelectionSequence = ++m_selectionSequence;
            m_selectionSource = input.source;
        }
        else if(m_activeSelectionSequence == 0u || input.source != m_selectionSource)
            return;
        const Ui::WidgetId target = input.type == Ui::InputEventType::PrimaryDown ? m_context.input().focus() : previousCapture;
        if(input.type == Ui::InputEventType::PrimaryDown && m_context.input().capture() != target){
            m_clickTracker.cancel();
            return;
        }
        Entry* entry = find(target);
        if(!entry){
            m_clickTracker.cancel();
            return;
        }
        const auto byte = UiEditGeometry().hitCaret(
            entry->displayed,
            entry->owner,
            entry->popup,
            input.position
        );
        if(!byte){
            m_clickTracker.cancel();
            return;
        }
        const auto wordByte = UiEditGeometry().hitWord(
            entry->displayed,
            entry->owner,
            entry->popup,
            input.position
        );
        if(!wordByte){
            m_clickTracker.cancel();
            return;
        }
        Event event(m_arena);
        event.owner = entry->owner;
        event.focusGeneration = entry->focusGeneration;
        event.kind = UiEditBoxEventKind::Selection;
        event.selectionSequence = m_activeSelectionSequence;
        event.selectionSource = m_selectionSource;
        event.position = *byte;
        event.wordPosition = *wordByte;
        event.geometryRevision = entry->displayed.revision;
        event.geometryExternalRevision = entry->displayed.externalRevision;
        event.extend = input.extend;
        event.dragging = input.type != Ui::InputEventType::PrimaryDown;
        event.completed = input.type == Ui::InputEventType::PrimaryUp;
        if(input.type == Ui::InputEventType::PrimaryDown){
            const u64 clickTimestampMs = input.hoverCapable ? input.timestampMs : 0u;
            event.wordSelect = m_clickTracker.press(entry->owner, entry->popup, entry->displayed.revision, entry->displayed.externalRevision, input.position, clickTimestampMs, input.extend) == UiEditClickKind::Word;
        }
        else if(input.type == Ui::InputEventType::PrimaryUp)
            m_clickTracker.release(entry->owner);
        if(!append(Move(event)))
            return;
    }
    else if(input.type == Ui::InputEventType::PointerLeave || input.type == Ui::InputEventType::SecondaryDown)
        m_clickTracker.cancel();
    synchronizeFocus();
}

b8 UiEditBoxHost::character(const u32 unicode){
    if(rejectBorrowedMutation())
        return false;
    synchronizeFocus();
    if(!hasTextFocus())
        return false;
    if(m_session.token().valid())
        return true;
    if(!IsTextInputCodePoint(unicode))
        return true;
    const Entry& entry = *MakeNotNull(find(m_context.input().focus()));
    Event event(m_arena);
    event.kind = UiEditBoxEventKind::Character;
    event.owner = entry.owner;
    event.focusGeneration = entry.focusGeneration;
    Array<char, s_Utf8MaxSequenceBytes> bytes{};
    const auto count = EncodeUtf8CodePoint(unicode, bytes);
    if(!count)
        return true;
    event.native.text.assign(bytes.data(), *count);
    m_clickTracker.cancel();
    if(!append(Move(event)))
        return true;
    return true;
}

b8 UiEditBoxHost::pastePrimary(const Ui::Point position){
    if(rejectBorrowedMutation())
        return false;
    synchronizeFocus();
    const Ui::WidgetId target = m_context.input().hitTest(position);
    Entry* entry = find(target);
    if(!entry || !hasTextFocus() || m_context.input().focus() != target)
        return false;
    m_clickTracker.cancel();
    Event event(m_arena);
    event.owner = entry->owner;
    event.focusGeneration = entry->focusGeneration;
    event.kind = UiEditBoxEventKind::PastePrimary;
    return append(Move(event));
}

b8 UiEditBoxHost::prepareSubmit(const Ui::InputEvent& event)noexcept{
    if(rejectBorrowedMutation() || !m_context.input().canSubmitCommand(event))
        return false;
    const Entry* entry = find(m_context.input().focus());
    if(!entry || !hasTextFocus())
        return false;
    const Ui::InputCommandIntent intent{ event.command, event.extend, event.edit };
    if(Ui::TranslateEditCommand(intent, false, entry->displayed.textMode).command != Ui::EditCommand::Submit)
        return false;
    if(compositionActive(*entry)){
        m_context.input().cancelSubmitCommands(entry->widget.id);
        return false;
    }
    return true;
}

b8 UiEditBoxHost::compositionActive(const Entry& entry)const noexcept{
    b8 composing = entry.expected.m_expectedComposition.active;
    for(const Event& event : m_events){
        if(event.owner != entry.owner || event.focusGeneration != entry.focusGeneration)
            continue;
        if(event.kind == UiEditBoxEventKind::Native){
            if(event.native.kind == Core::TextInputEventKind::Preedit)
                composing = !event.native.text.empty();
            else if(event.native.kind == Core::TextInputEventKind::Cancelled || (event.native.kind == Core::TextInputEventKind::Commit && !event.native.text.empty()))
                composing = false;
        }
        else if(event.kind == UiEditBoxEventKind::Command && event.command.command == Ui::EditCommand::Cancel)
            composing = false;
    }
    return composing;
}

b8 UiEditBoxHost::append(Event&& event){
    if(!CanAppendUiEditBoxEvent(m_events.size(), Ui::s_InputMaxEvents, event.native.text.size(), m_queuedTextBytes, Core::s_TextInputMaxQueuedTextBytes)){
        reset();
        return false;
    }
    m_queuedTextBytes += event.native.text.size();
    m_events.push_back(Move(event));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Ui::EditBoxResult UiEditBoxHost::editBorrowed(const Ui::WidgetState& widget, const Ui::EditBoxOptions& options, const Ui::PopupToken& popup, Ui::EditModel& model, Ui::IEditActionSink* actions, NavigationBorrow* navigation){
    if(rejectBorrowedMutation())
        return {};
    if(navigation && model.textMode() != Ui::EditTextMode::Multiline)
        return {};
    if(actions)
        synchronizeFocus();
    m_borrowed = true;
    m_borrowRejected = false;
    ScopeExit release([this]()noexcept{ m_borrowed = false; });

    Ui::EditBoxResult result;
    result.valid = true;
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    const UiTextEditOwner owner{ widget.id, widget.declarationGeneration, model.instanceGeneration() };
    Entry* entry = find(widget.id);
    b8 abandon = false;
    if(!entry){
        if(m_entries.size() == s_UiEditBoxMaxEntries){
            result.valid = false;
            return result;
        }
        m_entries.emplace_back(m_arena);
        entry = &m_entries.back();
        entry->owner = owner;
        entry->expected.capture(model);
        entry->navigationResetPending = navigation != nullptr;
        abandon = actions != nullptr;
    }
    else{
        const u64 navigationInstance = navigation ? navigation->state.instanceGeneration() : 0u;
        const b8 rebound = entry->owner != owner || entry->popup != popup || entry->actionCapable != (actions != nullptr) || entry->navigationInstanceGeneration != navigationInstance;
        const b8 policyChanged = entry->enabled != options.enabled || entry->readOnly != options.readOnly;
        abandon = actions && (rebound || policyChanged);
        if(rebound || policyChanged || !entry->expected.matches(model)){
            m_context.input().cancelSubmitCommands(widget.id);
            entry->navigationResetPending = navigation != nullptr;
            if(m_session.owner() == entry->owner || (m_clipboard.pending() && m_clipboardOwner == entry->owner) || (m_primary.pending() && m_primaryOwner == entry->owner))
                cancelTransfers();
            discard(entry->owner);
            entry->owner = owner;
            entry->dragging = false;
            entry->wordDragging = false;
            m_clickTracker.cancel();
            model.cancelComposition();
            entry->expected.capture(model);
            if(rebound || policyChanged){
                entry->focused = false;
                entry->focusGeneration = 0u;
                entry->retiredFocusGeneration = 0u;
            }
        }
    }
    entry->widget = widget;
    entry->popup = popup;
    entry->seen = m_generation;
    entry->actionCapable = actions != nullptr;
    entry->navigationInstanceGeneration = navigation ? navigation->state.instanceGeneration() : 0u;
    entry->enabled = options.enabled;
    entry->readOnly = options.readOnly;
    if(actions && result.focused && !entry->focused){
        entry->focused = true;
        entry->focusGeneration = nextFocusGeneration();
    }
    const u64 beforeRevision = model.revision();
    const usize beforeAnchor = model.anchor();
    const usize beforeCaret = model.caret();
    if(m_borrowRejected){
        result.valid = false;
        return result;
    }
    if(navigation && entry->navigationResetPending){
        navigation->state.reset();
        entry->navigationResetPending = false;
    }
    if(abandon && !applyAction(options, Ui::EditAction::Abandon, *entry, model, result, *actions, navigation)){
        result.valid = false;
        discard(owner);
        return result;
    }
    if((!result.focused && !actions) || options.readOnly || !options.enabled){
        if(m_session.owner() == owner)
            cancelTransfers();
        model.cancelComposition();
    }
    b8 inputMethod = false;
    for(usize index = 0u; index < m_events.size();){
        if(m_events[index].owner != owner){
            ++index;
            continue;
        }
        Event event = Move(m_events[index]);
        m_queuedTextBytes -= event.native.text.size();
        m_events.erase(m_events.begin() + static_cast<isize>(index));
        if(actions && event.focusGeneration <= entry->retiredFocusGeneration)
            continue;
        if(options.enabled){
            if(!apply(options, event, *entry, model, result, actions, navigation)){
                result.valid = false;
                discard(owner);
                if(m_session.owner() == owner || (m_clipboard.pending() && m_clipboardOwner == owner) || (m_primary.pending() && m_primaryOwner == owner))
                    cancelTransfers();
                return result;
            }
            inputMethod = event.kind == UiEditBoxEventKind::Native;
        }
    }
    // Earlier edit intentions cancel transfers before a completion can mutate their original selection.
    if(m_clipboard.pending() && m_clipboardOwner == owner){
        if(!result.focused){
            if(!m_clipboard.cancel())
                TerminateInvariant();
        }
        else{
            const auto action = m_clipboard.action();
            const b8 mutates = action == Ui::EditClipboardAction::Cut || action == Ui::EditClipboardAction::Paste;
            if(mutates){
                if(m_session.owner() == owner && !m_session.cancel())
                    TerminateInvariant();
            }
            const auto completion = m_clipboard.drain(owner, model, options.readOnly);
            result.textChanged |= completion.textChanged;
            result.selectionChanged |= completion.selectionChanged;
            if(navigation && !m_borrowRejected && mutates && completion.status == UiEditClipboardStatus::Applied)
                navigation->state.reset();
        }
    }
    if(m_primary.pending() && m_primaryOwner == owner){
        const auto completion = m_primary.drain(owner, model, options.readOnly);
        result.textChanged |= completion.textChanged;
        result.selectionChanged |= completion.selectionChanged;
    }
    result.textChanged |= beforeRevision != model.revision();
    result.selectionChanged |= beforeAnchor != model.anchor() || beforeCaret != model.caret();
    result.focused = options.enabled && m_context.input().focus() == widget.id;
    result.preeditCaretVisible = entry->preeditCaretVisible;
    synchronizeSession(options, inputMethod, *entry, model);
    result.valid = !m_borrowRejected;
    entry->expected.capture(model);
    return result;
}

void UiEditBoxHost::synchronizeSession(const Ui::EditBoxOptions& options, const b8 inputMethod, Entry& entry, Ui::EditModel& model){
    if(!options.enabled || options.readOnly || m_context.input().focus() != entry.widget.id){
        if(m_session.owner() == entry.owner && !m_session.cancel())
            TerminateInvariant();
        model.cancelComposition();
        return;
    }
    if(!UiEditGeometry().sessionReady(entry.displayed, entry.owner, entry.popup))
        return;
    if(m_session.token().valid() && m_session.owner() != entry.owner && !m_session.cancel())
        TerminateInvariant();
    if(!m_session.token().valid()){
        model.cancelComposition();
        const auto admission = m_session.begin(entry.owner, nativeCaret(entry.displayed), model);
        if(admission != Core::TextInputAdmission::Accepted)
            return;
        m_lastNativeSequence = 0u;
        entry.rejectedNative = {};
    }
    else{
        const auto admission = m_session.adoptLocal(entry.owner, model, nativeCaret(entry.displayed), inputMethod ? Core::TextInputChangeCause::InputMethod : Core::TextInputChangeCause::Other);
        if(admission != Core::TextInputAdmission::Accepted){
            model.cancelComposition();
            if(!m_session.cancel())
                TerminateInvariant();
            return;
        }
    }
    m_nativePublished = model.stamp();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
