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
    using ShaderPathResolveCallback = Function<Expected<Name>(const Name&, AStringView, const Name&)>;
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
    void setGpuCommandRecordingMode(Ui::GpuCommandRecordingMode::Enum mode)noexcept;
    // Main-thread request; last request wins. Empty selects the engine default. Safe from a UI paint callback.
    void requestSkin(const Core::Assets::AssetRef<UiSkin>& skin)noexcept;
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selectedSkin()const noexcept{ return m_skinSelection.selected(); }
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requestedSkin()const noexcept{ return m_skinSelection.requestedChange(); }
    [[nodiscard]] bool skinRequestPending()const noexcept{ return m_skinSelection.changePending(); }
    [[nodiscard]] bool skinRequestFailed()const noexcept{ return m_skinSelection.changeFailed(); }
    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const noexcept override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void displayScaleChanged(f32 scaleX, f32 scaleY)override;


public:
    // Main-thread profile replacement copies bindings; an empty profile disables every default shortcut.
    [[nodiscard]] bool setInputBindings(const Ui::InputKeyBinding* bindings, usize count);
    void restoreDefaultInputBindings();
    // Projects map device controls to UI commands; each nonzero device/control pair identifies one held sequence.
    [[nodiscard]] bool commandInput(Ui::InputSource source, Ui::InputCommand::Enum command, i32 phase, bool extend = false);
    virtual bool keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    [[nodiscard]] virtual bool blocksKeyboardText()const noexcept override{ return m_blockCommandChars; }
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
    void routeInput(const Ui::InputEvent& event, bool* consumed = nullptr, bool* blockText = nullptr);
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
    // Native-key ownership survives focus transfer; the CPU router retains each resolved held intention.
    Array<u8, 512u> m_nativeKeyOwners{};
    u32 m_pressedButtons = 0u;
    u8 m_pointerOwner = 0u;
    bool m_blockNativeChars = false;
    bool m_blockCommandChars = false;
    u64 m_frameGeneration = 0u;
    u32 m_width = 0u;
    u32 m_height = 0u;
    bool m_resourcesReady = false;
    bool m_fontsReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

