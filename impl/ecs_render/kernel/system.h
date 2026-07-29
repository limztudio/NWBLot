// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/kernel/components.h>
#include <impl/ecs_render/material/material_instance.h>
#include <impl/ecs_render/shared/renderer_state.h>
#include <impl/ecs_render/kernel/subsystems.h>

#include <core/ecs/system.h>
#include <core/graphics/render_pass.h>
#include <core/telemetry/frame_graph_contributor.h>
#include <impl/assets/graphics/mesh/binding_slots.h>
#include <impl/assets_material/asset.h>
#include <impl/ecs_csg/frame_state.h>
#include <impl/ecs_csg/shape_registry.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class AssetManager;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_ASSETS_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Shader;
class Mesh;

namespace ECSRenderDetail{
#if defined(NWB_DEBUG)
    struct MaterialTypedInstanceRangeVector;
#endif
};


class RendererSystem final : public Core::ECS::ISystem, public Core::IRenderPass, public Core::Telemetry::IFrameGraphContributor{
    template<typename RendererT>
    friend class RendererSystemSubsystemBase;
    friend class RendererShaderSystem;
    friend class RendererMeshSystem;
    friend class RendererMaterialSystem;
    friend class RendererCsgSystem;
    friend class RendererDeferredSystem;
    friend class RendererAvboitSystem;
    friend class RendererRayTracingSystem;

public:
    using ShaderPathResolveCallback = RendererShaderPathResolveCallback;


public:
    RendererSystem(
        Core::Alloc::GlobalArena& arena,
        Core::ECS::World& world,
        Core::Graphics& graphics,
        Core::Assets::AssetManager& assetManager,
        ShaderPathResolveCallback shaderPathResolver
    );
    virtual ~RendererSystem()override;


public:
    virtual bool validateResources(u32 width, u32 height, u32 sampleCount)override;
    virtual void invalidateResources()override;

    virtual void update(Core::ECS::World& world, f32 delta)override;

    virtual bool prepareResources(Core::Framebuffer* framebuffer)override;
    virtual void render(Core::Framebuffer* framebuffer)override;
    virtual bool appendFrameGraph(Core::Telemetry::FrameGraphBuilder& builder)override;
    [[nodiscard]] CsgShapeRegistry& csgShapeRegistry(){ return m_csgShapeRegistry; }
    [[nodiscard]] const CsgShapeRegistry& csgShapeRegistry()const{ return m_csgShapeRegistry; }

private:
    [[nodiscard]] bool ensureFrameCommandLists();
    [[nodiscard]] bool prepareGpuTimingScopes();
    [[nodiscard]] bool recordShadowPrepareCommandList(DeferredFrameTargets& deferredTargets);
    [[nodiscard]] Core::Alloc::GlobalArena& arena()noexcept{ return m_arena; }
    [[nodiscard]] Core::ECS::World& world()noexcept{ return m_world; }
    [[nodiscard]] Core::Graphics& graphics()noexcept{ return m_graphics; }
    [[nodiscard]] Core::Assets::AssetManager& assetManager()noexcept{ return m_assetManager; }
    [[nodiscard]] ShaderPathResolveCallback& shaderPathResolver()noexcept{ return m_shaderPathResolver; }
    [[nodiscard]] RendererMeshState& meshState()noexcept{ return m_meshState; }
    [[nodiscard]] RendererMaterialState& materialState()noexcept{ return m_materialState; }
    [[nodiscard]] RendererDrawState& drawState()noexcept{ return m_drawState; }
    [[nodiscard]] RendererCsgState& csgState()noexcept{ return m_csgState; }
    [[nodiscard]] RendererDeferredState& deferredState()noexcept{ return m_deferredState; }
    [[nodiscard]] RendererAvboitState& avboitState()noexcept{ return m_avboitState; }
    [[nodiscard]] RendererRayTracingState& rayTracingState()noexcept{ return m_rayTracingState; }
    [[nodiscard]] RendererShaderSystem& shaderSystem()noexcept{ return m_shaderSystem; }
    [[nodiscard]] RendererMeshSystem& meshSystem()noexcept{ return m_meshSystem; }
    [[nodiscard]] RendererMaterialSystem& materialSystem()noexcept{ return m_materialSystem; }
    [[nodiscard]] RendererCsgSystem& csgSystem()noexcept{ return m_csgSystem; }
    [[nodiscard]] RendererAvboitSystem& avboitSystem()noexcept{ return m_avboitSystem; }
    [[nodiscard]] RendererRayTracingSystem& raytracingSystem()noexcept{ return m_raytracingSystem; }

private:
    Core::Alloc::GlobalArena& m_arena;
    Core::ECS::World& m_world;
    Core::Graphics& m_graphics;
    Core::Assets::AssetManager& m_assetManager;
    ShaderPathResolveCallback m_shaderPathResolver;
    CsgShapeRegistry m_csgShapeRegistry;

private:
    RendererMeshState m_meshState;
    RendererMaterialState m_materialState;
    RendererDrawState m_drawState;
    RendererCsgState m_csgState;
    RendererDeferredState m_deferredState;
    RendererAvboitState m_avboitState;
    RendererRayTracingState m_rayTracingState;
    CsgFrameState m_preparedCsgFrameState;
    Core::CommandListResourceStateHandoff m_shadowPrepareStateHandoff;
    Core::CommandListResourceStateHandoff m_meshViewSetupStateHandoff;
    Core::CommandListResourceStateHandoff m_sceneShadingSetupStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredClearStateHandoff;
    Core::CommandListResourceStateHandoff m_frameSetupStateFanInHandoff;
    Core::CommandListResourceStateHandoff m_gbufferStateHandoff;
    Core::CommandListResourceStateHandoff m_postGbufferNormalizedStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowComputeInputStateHandoff;
    // Compute-only shadow scratch/history retains its state across frames. The exclusive visibility output follows
    // the separate Compute -> Graphics -> Compute ownership-return handoff below.
    Core::CommandListResourceStateHandoff m_shadowComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityGraphicsStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowVisibilityReturnStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowOwnershipRecoveryInputStateHandoff;
    Core::CommandListResourceStateHandoff m_shadowOwnershipRecoveryStateHandoff;
    // Both caustic producers can join the dedicated Compute lane. Their temporal scratch remains private to Compute,
    // while the resolved irradiance follows the same explicit Compute -> Graphics -> Compute ownership cycle as
    // shadowVisibility.
    Core::CommandListResourceStateHandoff m_causticsComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsComputeInputStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_causticsStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceGraphicsStateHandoff;
    Core::CommandListResourceStateHandoff m_causticIrradianceReturnStateHandoff;
    // Surfel GI is also entirely compute-dispatched, including its RayQuery trace variant. Its field/history stays
    // private to AsyncCompute; only the resolved full-resolution irradiance crosses to deferred lighting.
    Core::CommandListResourceStateHandoff m_surfelGiComputeBaseStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiComputeInputStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiComputePersistentStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelGiStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelIrradianceGraphicsStateHandoff;
    Core::CommandListResourceStateHandoff m_surfelIrradianceReturnStateHandoff;
    Core::CommandListResourceStateHandoff m_postGbufferFanInStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredLightingStateHandoff;
    Core::CommandListResourceStateHandoff m_deferredCompositeStateHandoff;
    // AVBOIT's raster occupancy/extinction/accumulation stages stay on Graphics, while the depth-warp and integration
    // dispatches run on AsyncCompute. All inter-stage work resources use concurrent sharing; these handoffs carry
    // state only and are submitted in strict Graphics -> Compute -> Graphics order.
    Core::CommandListResourceStateHandoff m_avboitPreStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitDepthWarpInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitDepthWarpStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitExtinctionInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitExtinctionStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitIntegrationInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitIntegrationStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitAccumulationInputStateHandoff;
    Core::CommandListResourceStateHandoff m_avboitStateHandoff;
    Core::CommandListHandle m_meshViewSetupCommandList;
    Core::CommandListHandle m_sceneShadingSetupCommandList;
    Core::CommandListHandle m_deferredClearCommandList;
    Core::CommandListHandle m_gbufferCommandList;
    Core::CommandListHandle m_postGbufferNormalizeCommandList;
    Core::CommandListHandle m_shadowVisibilityCommandList;
    Core::CommandListHandle m_shadowOwnershipRecoveryCommandList;
    // Empty Graphics packets that bracket the independently recorded effects submission for an aggregate queue-time
    // envelope. They are submitted only on the dedicated async-shadow schedule.
    Core::CommandListHandle m_asyncEffectsTimingBeginCommandList;
    Core::CommandListHandle m_asyncEffectsTimingEndCommandList;
    // Vulkan permits dispatchRays from a command pool with VK_QUEUE_COMPUTE_BIT support, so this sibling list serves
    // both caustic producers on the dedicated AsyncCompute lane.
    Core::CommandListHandle m_asyncCausticsCommandList;
    Core::CommandListHandle m_causticsCommandList;
    // Surfel GI uses only compute dispatches on both its SW-BVH and HW-RayQuery branches, so a dedicated compute
    // family can record it independently of the Graphics fallback list.
    Core::CommandListHandle m_asyncSurfelGiCommandList;
    Core::CommandListHandle m_surfelGiCommandList;
    Core::CommandListHandle m_deferredLightingCommandList;
    // The hybrid AVBOIT packet uses Graphics lists for raster phases and AsyncCompute lists for its two pure dispatches.
    Core::CommandListHandle m_avboitCommandList;
    Core::CommandListHandle m_asyncAvboitDepthWarpCommandList;
    Core::CommandListHandle m_avboitExtinctionCommandList;
    Core::CommandListHandle m_asyncAvboitIntegrationCommandList;
    Core::CommandListHandle m_avboitAccumulateCommandList;
    Core::CommandListHandle m_deferredCompositeCommandList;
    Core::CommandListHandle m_shadowPrepareCommandList;
    bool m_preparedCsgFrameStateValid = false;
    bool m_preparedHasTransparentRenderers = false;
    bool m_preparedShadowVisibilityReady = false;
    // A Compute release without a successfully accepted Graphics acquire is not recoverable by guessing. The
    // Graphics owner is asked to end this device generation, then resources are rebuilt before rendering resumes.
    bool m_asyncShadowOwnershipRecoveryFailed = false;

private:
    RendererShaderSystem m_shaderSystem;
    RendererMeshSystem m_meshSystem;
    RendererMaterialSystem m_materialSystem;
    RendererCsgSystem m_csgSystem;
    RendererDeferredSystem m_deferredSystem;
    RendererAvboitSystem m_avboitSystem;
    RendererRayTracingSystem m_raytracingSystem;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

