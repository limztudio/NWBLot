// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ui/paint.h>

#include <core/assets/manager.h>
#include <core/graphics/runtime/runtime.h>
#include <core/task/gpu/output_layer_contributor.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct GpuRendererState;


// Main-thread owner. Resource invalidation follows the caller's joined GPU/graph teardown boundary.
class GpuRenderer final : public Core::IGpuTaskGraphOutputLayerContributor, NoCopy{
public:
    using ShaderPathResolveCallback = Function<bool(const Name&, AStringView, const Name&, Name&)>;


public:
    GpuRenderer(
        Core::Alloc::GlobalArena& arena,
        Core::GraphicsRuntime& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~GpuRenderer()override;


public:
    [[nodiscard]] bool validateResources(u32 width, u32 height);
    void invalidateResources();
    [[nodiscard]] bool setSkin(const Core::Assets::AssetRef<UiSkin>& identity, const UiSkin& skin, u64 skinGeneration);
    // Rejection leaves the caller's snapshot unmoved. One immutable pending generation is admitted at a time.
    [[nodiscard]] bool submit(DrawSnapshot&& snapshot);
    [[nodiscard]] bool hasPendingFrame()const;
    [[nodiscard]] u64 lastAcceptedGeneration()const;
    [[nodiscard]] Core::PresentationReceiptStatus::Enum lastAcceptedPresentationStatus()const;
    [[nodiscard]] bool renderStandalone(const Core::AcquiredPresentationFrame& frame);


public:
    [[nodiscard]] virtual bool prepareTaskGraphOutputLayer(const Core::AcquiredPresentationFrame& frame)override;
    [[nodiscard]] virtual bool declareTaskGraphOutputLayer(Core::GpuTaskGraph& graph, Core::GpuTaskGraphOutputLayer& outLayer)override;
    virtual void acceptTaskGraphOutputLayer(u64 frameGeneration, const Core::QueueSubmissionToken& submissionToken)override;


private:
    NotNullUniquePtr<GpuRendererState, ArenaDeleter<GpuRendererState, Core::Alloc::GlobalArena>> m_state;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_UI_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

