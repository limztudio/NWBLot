// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"
#include "edit_box_host.h"
#include "frame_delta.h"
#include "skin_selection.h"

#include <impl/ecs_ui/toolkit/gpu/renderer.h>

#include <core/ecs/system.h>
#include <core/input/module.h>
#include <core/graphics/runtime/render_pass.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiLayerPresentation{
    enum Enum : u8{ Scene, Standalone };
};


struct UiFontBinding{
    Core::Assets::AssetRef<Font> font;
    Core::Assets::AssetRef<FontAtlas> atlas;
};


// ECS owns callback execution; the independent GPU renderer receives only frozen paint snapshots.
class UiLayerSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public Core::IInputEventHandler
{
private:
    struct LiveRoot{
        Core::ECS::EntityID entity;
        i32 order = 0;
    };


public:
    using ShaderPathResolveCallback = Function<bool(const Name&, AStringView, const Name&, Name&)>;
    using FontReferences = Vector<UiFontBinding, Core::Alloc::GlobalArena>;


public:
    UiLayerSystem(
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
        UiLayerPresentation::Enum presentation
    );
    virtual ~UiLayerSystem()override;


public:
    void setGpuCommandRecordingMode(Ui::GpuCommandRecordingMode::Enum mode);
    // Main-thread request; last request wins. Empty selects the engine default. Safe from a UI paint callback.
    void requestSkin(const Core::Assets::AssetRef<UiSkin>& skin);
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selectedSkin()const{ return m_skinSelection.selected(); }
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requestedSkin()const{ return m_skinSelection.requestedChange(); }
    [[nodiscard]] bool skinRequestPending()const{ return m_skinSelection.changePending(); }
    [[nodiscard]] bool skinRequestFailed()const{ return m_skinSelection.changeFailed(); }
    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void displayScaleChanged(f32 scaleX, f32 scaleY)override;


public:
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    virtual bool keyboardCharInput(u32 unicode, i32 mods)override;
    virtual bool mousePosUpdate(f64 xpos, f64 ypos)override;
    virtual bool mouseButtonUpdate(i32 button, i32 action, i32 mods)override;
    virtual bool mouseScrollUpdate(f64 xoffset, f64 yoffset)override;
    virtual void windowFocusUpdate(bool focused)override;
    virtual void pointerLeave()override;
    virtual void pointerCaptureLost()override;
    [[nodiscard]] bool wantsKeyboard()const;
    [[nodiscard]] bool wantsPointer()const;
    [[nodiscard]] bool wantsTextInput()const;


private:
    [[nodiscard]] bool collectRoots();
    void synchronizeInput();
    void routeInput(const Ui::InputEvent& event);
    void synchronizeNativeInput();
    [[nodiscard]] bool loadFonts(Core::Alloc::ScratchArena& scratchArena);


private:
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::InputDispatcher& m_input;
    Core::IClipboardService& m_clipboard;
    Core::ITextInputService& m_textInput;
    Core::Assets::AssetManager& m_assetManager;
    UiSkinSelection m_skinSelection;
    UiLayerPresentation::Enum m_presentation;
    UniquePtr<Core::Assets::IAsset> m_skinAsset;
    FontReferences m_fontRefs;
    Ui::PaintBuilder m_paint;
    Ui::TextService m_text;
    Ui::Context m_context;
    UiEditBoxHost m_editHost;
    Ui::Builder m_ui;
    Ui::PaintVector<LiveRoot> m_liveRoots;
    Ui::PaintVector<Ui::WidgetRoot> m_rootIdentities;
    Ui::GpuRenderer m_renderer;
    UiFrameDelta m_frameDelta;
    Ui::DisplayMetrics m_display;
    Ui::Point m_pointer;
    // One bounded native-key owner per held sequence; normalized navigation keys additionally use the CPU router.
    Array<u8, 512u> m_nativeKeyOwners{};
    u32 m_pressedButtons = 0u;
    u8 m_pointerOwner = 0u;
    bool m_blockNativeChars = false;
    u64 m_frameGeneration = 0u;
    u32 m_width = 0u;
    u32 m_height = 0u;
    bool m_resourcesReady = false;
    bool m_fontsReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

