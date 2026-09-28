// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "components.h"

#include <impl/ui/gpu/renderer.h>

#include <core/ecs/system.h>
#include <core/graphics/runtime/render_pass.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace UiLayerPresentation{
    enum Enum : u8{ Scene, Standalone };
};


// ECS owns callback execution; the independent GPU renderer receives only frozen paint snapshots.
class UiLayerSystem final : public Core::ECS::ISystem, public Core::IRenderPass{
public:
    using ShaderPathResolveCallback = Function<bool(const Name&, AStringView, const Name&, Name&)>;


public:
    UiLayerSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::GraphicsRuntime& graphics,
        Core::IClipboardService& clipboard,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver,
        const Core::Assets::AssetRef<UiSkin>& skin,
        UiLayerPresentation::Enum presentation
    );
    virtual ~UiLayerSystem()override;


public:
    [[nodiscard]] virtual Core::CpuTaskOptions taskOptions()const override{
        return { .cost = Core::CpuTaskCost::Light, .target = Core::CpuTaskTarget::MainThread };
    }
    virtual void update(Core::ECS::World& world, f32 delta)override;
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;
    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual void displayScaleChanged(f32 scaleX, f32 scaleY)override;


private:
    Core::ECS::World& m_world;
    Core::GraphicsRuntime& m_graphics;
    Core::IClipboardService& m_clipboard;
    Core::Assets::AssetManager& m_assetManager;
    Core::Assets::AssetRef<UiSkin> m_skinRef;
    UiLayerPresentation::Enum m_presentation;
    UniquePtr<Core::Assets::IAsset> m_skinAsset;
    Ui::PaintBuilder m_paint;
    Ui::GpuRenderer m_renderer;
    Ui::DisplayMetrics m_display;
    u64 m_frameGeneration = 0u;
    u64 m_skinGeneration = 1u;
    u32 m_width = 0u;
    u32 m_height = 0u;
    bool m_resourcesReady = false;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

