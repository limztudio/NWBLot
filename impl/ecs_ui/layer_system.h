// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_ui/components.adhd.h>
#include <impl/ecs_ui/layer_types.adhd.h>
#include <impl/ecs_ui/paint_context.h>
#include <impl/ecs_ui/edit_box_host.h>
#include <impl/ecs_ui/skin_selection.adhd.h>

#include <impl/assets_scene/asset.h>
#include <impl/assets_ui_skin/types.adhd.h>
#include <impl/ecs_ui/toolkit/input/pointer.adhd.h>
#include <impl/ecs_ui/toolkit/gpu/renderer.h>

#include <core/ecs/system.h>
#include <core/input/module.h>
#include <core/graphics/runtime/render_pass.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr TStringView s_UiLayerSystemOwnerName = NWB_TEXT("UiLayerSystem");

// ECS owns callback execution; the independent GPU renderer receives only frozen paint snapshots.
class UiLayerSystem final
    : public Core::ECS::ISystem
    , public Core::IRenderPass
    , public Core::IInputEventHandler
{
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
    void setActiveSceneSubsystems(const ActiveSceneSubsystems* subsystems)noexcept;
    void setGpuCommandRecordingMode(const Ui::GpuCommandRecordingMode::Enum mode)noexcept{ m_renderer.setCommandRecordingMode(mode); }

    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const noexcept override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }
    virtual void prepare([[maybe_unused]] Core::ECS::World& world)override{
        synchronizeInput();
    }
    [[nodiscard]] virtual Expected<void, Core::CpuTaskAdmissionError::Enum> update(Core::ECS::World& world, f32 delta)override;
    virtual b8 validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual b8 prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void displayScaleChanged(f32 scaleX, f32 scaleY)override;


public:
    // Main-thread profile replacement copies bindings; an empty profile disables every default shortcut.
    [[nodiscard]] b8 setInputBindings(Span<const Ui::InputKeyBinding> bindings);
    virtual ::NWB::Core::KeyboardInputResult keyboardUpdate(i32 key, i32 scancode, i32 action, i32 mods)override;
    virtual b8 keyboardCharInput(u32 unicode, i32 mods)override;
    virtual b8 mousePosUpdate(f64 xpos, f64 ypos)override;
    virtual b8 mouseButtonUpdate(i32 button, i32 action, i32 mods)override;
    virtual b8 mouseScrollUpdate(f64 xoffset, f64 yoffset)override;
    virtual void windowFocusUpdate(b8 focused)override;
    virtual void pointerLeave()override;
    virtual void pointerCaptureLost()override;
    [[nodiscard]] b8 wantsKeyboard()const noexcept{ return m_resourcesReady && m_context.input().wantsCommands(); }
    [[nodiscard]] b8 wantsPointer()const noexcept;


private:
    void collectRoots();
    void synchronizeInput();
    void routeInput(const Ui::InputEvent& event, b8* blockText = nullptr);
    void routePointerInput(const Ui::PointerInput& input, b8* consumed = nullptr);
    void synchronizeNativeInput();
    void abandonPendingFrame(b8 resetUi = false)noexcept;
    void resetNormalizedInput();
    void resetNativeInputState()noexcept;
    void logInputRoutingOverflows(const Ui::InputRoutingResult& result)const;
    [[nodiscard]] b8 loadFonts(Core::Alloc::ScratchArena& scratchArena);
    [[nodiscard]] Core::Assets::AssetRef<UiSkin> basicSkin()const noexcept;
    [[nodiscard]] Core::Assets::AssetRef<UiSkin> resolveSkin(const Core::Assets::AssetRef<UiSkin>& ref)const noexcept;


private:
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::InputDispatcher& m_input;
    Core::IClipboardService& m_clipboard;
    Core::ITextInputService& m_textInput;
    Core::Assets::AssetManager& m_assetManager;
    ActiveSceneSubsystems m_defaultSceneSubsystems;
    const ActiveSceneSubsystems* m_activeSceneSubsystems = nullptr;
    UiSkinSelectionState m_skinSelection;
    UniquePtr<Core::Assets::IAsset> m_skinAsset;
    Ui::PaintBuilder m_paint;
    Ui::TextService m_text;
    Ui::Context m_context;
    UiEditBoxHost m_editHost;
    Ui::Builder m_ui;
    Ui::PaintVector<UiLiveRoot> m_liveRoots;
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

