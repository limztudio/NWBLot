// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.adhd.h>
#include "paint_context.h"
#include "edit_box_host.h"
#include <impl/ecs_ui/frame_delta.adhd.h>
#include "skin_selection.h"

#include <impl/assets_scene/asset.h>
#include <impl/assets_ui_skin/subsystem.adhd.h>
#include <impl/ecs_ui/toolkit/input/pointer.adhd.h>
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

inline constexpr TStringView s_UiLayerSystemOwnerName = NWB_TEXT("UiLayerSystem");
inline constexpr usize s_UiNativeKeySlotCount = 512u;


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
        UiLayerPresentation::Enum presentation
    );
    virtual ~UiLayerSystem()noexcept override;


public:
    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept{ m_activeSceneSubsystems = subsystems; m_renderer.setActiveSceneSubsystems(subsystems); }
    void setGpuCommandRecordingMode(Ui::GpuCommandRecordingMode::Enum mode)noexcept;
    // Main-thread request; last request wins. Empty selects the engine default. Safe from a UI paint callback.
    void requestSkin(const Core::Assets::AssetRef<UiSkin>& skin)noexcept;
    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& selectedSkin()const noexcept{ return m_skinSelection.selected(); }

    [[nodiscard]] const Core::Assets::AssetRef<UiSkin>& requestedSkin()const noexcept{ return m_skinSelection.requestedChange(); }

    [[nodiscard]] b8 skinRequestPending()const noexcept{ return m_skinSelection.changePending(); }

    [[nodiscard]] b8 skinRequestFailed()const noexcept{ return m_skinSelection.changeFailed(); }

    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const noexcept override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }
    virtual void prepare(Core::ECS::World& world)override;
    [[nodiscard]] virtual Expected<void, Core::CpuTaskAdmissionError::Enum> update(Core::ECS::World& world, f32 delta)override;
    virtual b8 validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual b8 prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void displayScaleChanged(f32 scaleX, f32 scaleY)override;


public:
    // Main-thread profile replacement copies bindings; an empty profile disables every default shortcut.
    [[nodiscard]] b8 setInputBindings(const Ui::InputKeyBinding* bindings, usize count);
    void restoreDefaultInputBindings();
    // Main-thread ingress uses semantic commands and identified pointer sequences; device zero belongs to the native adapter.
    [[nodiscard]] b8 commandInput(Ui::InputSource source, Ui::InputCommand::Enum command, Ui::InputPhase::Enum phase, b8 extend = false);
    [[nodiscard]] b8 pointerInput(const Ui::PointerInput& input);
    // Main-thread provider termination cancels its active contact and retires its remaining held sources.
    void cancelDeviceInput(u64 device);
    virtual b8 keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    [[nodiscard]] virtual b8 blocksKeyboardText()const noexcept override{ return m_blockCommandChars; }
    virtual b8 keyboardCharInput(u32 unicode, i32 mods)override;
    virtual b8 mousePosUpdate(f64 xpos, f64 ypos)override;
    virtual b8 mouseButtonUpdate(i32 button, i32 action, i32 mods)override;
    virtual b8 mouseScrollUpdate(f64 xoffset, f64 yoffset)override;
    virtual void windowFocusUpdate(b8 focused)override;
    virtual void pointerLeave()override;
    virtual void pointerCaptureLost()override;
    [[nodiscard]] b8 wantsKeyboard()const noexcept;
    [[nodiscard]] b8 wantsPointer()const noexcept;
    [[nodiscard]] b8 wantsTextInput()const noexcept;


private:
    [[nodiscard]] b8 collectRoots();
    void synchronizeInput();
    void routeInput(const Ui::InputEvent& event, b8* consumed = nullptr, b8* blockText = nullptr);
    void routePointerInput(const Ui::PointerInput& input, b8* consumed = nullptr);
    void synchronizeNativeInput();
    [[nodiscard]] b8 loadFonts(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Core::Assets::AssetRef<UiSkin> basicSkin()const noexcept{
        if(!m_activeSceneSubsystems)
            return {};
        const UiSubsystem* const uiSubsystem = m_activeSceneSubsystems->findSubsystem<UiSubsystem>();
        return uiSubsystem ? uiSubsystem->basicSkin : Core::Assets::AssetRef<UiSkin>{};
    }
    [[nodiscard]] Core::Assets::AssetRef<UiSkin> resolveSkin(const Core::Assets::AssetRef<UiSkin>& ref)const noexcept{
        const Core::Assets::AssetRef<UiSkin> fallbackSkin = basicSkin();
        if(!fallbackSkin.valid())
            return {};
        if(!ref.valid() || ref == fallbackSkin)
            return fallbackSkin;
        return m_activeSceneSubsystems->findSceneAsset<UiSkin>(ref.name());
    }


private:
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::InputDispatcher& m_input;
    Core::IClipboardService& m_clipboard;
    Core::ITextInputService& m_textInput;
    Core::Assets::AssetManager& m_assetManager;
    ActiveSceneSubsystems m_defaultSceneSubsystems;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;
    UiSkinSelection m_skinSelection;
    UniquePtr<Core::Assets::IAsset> m_skinAsset;
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
    Array<u8, s_UiNativeKeySlotCount> m_nativeKeyOwners{};
    u32 m_pressedButtons = 0u;
    UiLayerPresentation::Enum m_presentation;
    u8 m_pointerOwner = 0u;
    b8 m_blockNativeChars = false;
    b8 m_blockCommandChars = false;
    u64 m_frameGeneration = 0u;
    u32 m_width = 0u;
    u32 m_height = 0u;
    b8 m_resourcesReady = false;
    b8 m_fontsReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

