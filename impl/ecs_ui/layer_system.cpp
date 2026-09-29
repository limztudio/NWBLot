// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "layer_system.h"

#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>

#include <global/simplemath.h>


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
    const FontReferences& fonts,
    const UiLayerPresentation::Enum presentation)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_world(world)
    , m_graphics(graphics)
    , m_input(input)
    , m_clipboard(clipboard)
    , m_textInput(textInput)
    , m_assetManager(assetManager)
    , m_skinRef(skin)
    , m_presentation(presentation)
    , m_fontRefs(fonts, arena)
    , m_paint(arena)
    , m_text(arena)
    , m_context(arena)
    , m_editHost(arena, m_context, textInput, clipboard)
    , m_ui(arena, m_context, m_paint, m_text)
    , m_liveRoots(arena)
    , m_rootIdentities(arena)
    , m_renderer(arena, graphics, assetManager, Move(shaderPathResolver))
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

UiLayerSystem::~UiLayerSystem(){
    m_input.removeHandler(*this);
    m_graphics.clearTaskGraphOutputLayerContributor(m_renderer);
}

void UiLayerSystem::setGpuCommandRecordingMode(const Ui::GpuCommandRecordingMode::Enum mode){
    m_renderer.setCommandRecordingMode(mode);
}

bool UiLayerSystem::validateResources(const u32 width, const u32 height, const u32 sampleCount){
    static_cast<void>(sampleCount);
    m_resourcesReady = false;
    m_width = width;
    m_height = height;
    if(width == 0u || height == 0u)
        return true;

    if(!m_skinAsset){
        const UiSkin* skin = m_assetManager.loadTypedSync<UiSkin>(
            m_skinRef.name(), m_skinAsset, MakeNotNull(NWB_TEXT("UiLayerSystem")), MakeNotNull("UI skin")
        );
        if(!skin)
            return false;
    }
    const UiSkin* skin = Core::Assets::CastAsset<UiSkin>(m_skinAsset.get());
    NWB_FATAL_ASSERT(skin);
    if(!m_fontsReady){
        Core::Alloc::ScratchArena scratchArena(Name("impl/ecs_ui/load_fonts"));
        if(!loadFonts(scratchArena))
            return false;
    }
    if(!m_renderer.validateResources(width, height) || !m_renderer.setSkin(m_skinRef, *skin, m_skinGeneration))
        return false;

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
    m_editHost.reset();
    m_context.abandonFrame();
    m_ui.reset();
    m_pressedButtons = 0u;
    m_pointerOwner = 0u;
    m_renderer.invalidateResources();
}

bool UiLayerSystem::prepareResources(Core::Framebuffer* framebuffer){
    if(!framebuffer || !m_resourcesReady)
        return false;
    const bool prepared = m_renderer.prepareTaskGraphOutputLayer(m_graphics.acquiredPresentationFrame());
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
    m_context.abandonFrame();
    m_editHost.reset();
    m_ui.reset();
    m_display = { static_cast<f32>(m_width) / scaleX, static_cast<f32>(m_height) / scaleY, scaleX, scaleY };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

