// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/material/renderer_pipeline_types.h>

#include <core/graphics/api.h>
#include <core/graphics/rhi/gpu_descriptor_heap.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct DeferredFrameTargets;

namespace ECSRenderDetail{
    struct CsgGraphResourceSnapshot;
    struct MeshFrameBindingSnapshot;
};

struct AvboitFrameTargets{
    u32 fullWidth = 0;
    u32 fullHeight = 0;
    u32 lowWidth = 0;
    u32 lowHeight = 0;
    u32 virtualSliceCount = 0;
    u32 physicalSliceCount = 0;
    Core::Format::Enum lowRasterFormat = Core::Format::UNKNOWN;
    Core::Format::Enum accumColorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum accumExtinctionFormat = Core::Format::UNKNOWN;
    Core::Format::Enum transmittanceFormat = Core::Format::UNKNOWN;
    // AVBOIT target-generation heap registrations.
    Core::GpuDescriptorHandle coverageBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle depthWarpBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle controlBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle extinctionBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle extinctionOverflowBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transmittanceTextureStorageDescriptor = Core::GpuDescriptorHandle::invalid();
    // Borrowed shared target-slot payload; AVBOIT does not own it.
    Core::GpuDescriptorHandle deferredSlotsBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::TextureHandle lowRasterTarget;
    Core::TextureHandle accumColor;
    Core::TextureHandle accumExtinction;
    Core::TextureHandle transmittanceTexture;
    Core::FramebufferHandle lowFramebuffer;
    Core::FramebufferHandle accumulationFramebuffer;
    Core::BufferHandle coverageBuffer;
    Core::BufferHandle depthWarpBuffer;
    Core::BufferHandle controlBuffer;
    Core::BufferHandle extinctionBuffer;
    Core::BufferHandle extinctionOverflowBuffer;
    [[nodiscard]] bool valid()const noexcept{
#if defined(NWB_DEBUG)
        return
            fullWidth > 0
            && fullHeight > 0
            && lowWidth > 0
            && lowHeight > 0
            && virtualSliceCount > 0
            && physicalSliceCount > 0
            && lowRasterFormat != Core::Format::UNKNOWN
            && accumColorFormat != Core::Format::UNKNOWN
            && accumExtinctionFormat != Core::Format::UNKNOWN
            && transmittanceFormat != Core::Format::UNKNOWN
            && lowRasterTarget != nullptr
            && accumColor != nullptr
            && accumExtinction != nullptr
            && transmittanceTexture != nullptr
            && lowFramebuffer != nullptr
            && accumulationFramebuffer != nullptr
            && coverageBuffer != nullptr
            && depthWarpBuffer != nullptr
            && controlBuffer != nullptr
            && extinctionBuffer != nullptr
            && extinctionOverflowBuffer != nullptr
            && coverageBufferDescriptor.valid()
            && depthWarpBufferDescriptor.valid()
            && controlBufferDescriptor.valid()
            && extinctionBufferDescriptor.valid()
            && extinctionOverflowBufferDescriptor.valid()
            && transmittanceTextureStorageDescriptor.valid()
            && deferredSlotsBufferDescriptor.valid()
        ;
#else
        return accumulationFramebuffer != nullptr;
#endif
    }
};
static_assert(sizeof(AvboitFrameTargets) == 232u, "AvboitFrameTargets should keep its compact CPU-only layout");

struct MaterialPassDrawContext{
    Core::CommandList& commandList;
    const DeferredFrameTargets& deferredTargets;
    Core::Framebuffer* framebuffer = nullptr;
    MaterialPipelinePass::Enum pass = MaterialPipelinePass::Opaque;
    const AvboitFrameTargets* avboitTargets = nullptr;
    const Core::ViewportState& viewportState;
    // Prepared graph tasks declare the receiver-event images before the material thunk records. Compatibility
    // callers leave this false and retain their direct heap-image state setup.
    bool csgReceiverSurfaceImageStatesGraphOwned = false;
    // The opaque graph splits interval combine from its following material/cap sample task. That task lowers the
    // required UAV handoff before this thunk records; direct and AVBOIT compatibility callers retain their bridge.
    bool csgIntervalSampleImageStatesGraphOwned = false;
    // The shared graph declares receiver/cutter SRVs and clip/interval-sample CBVs before prepared CSG thunks
    // record. Direct and unprepared callers retain the native heap-buffer setup by leaving this false.
    bool csgClipBufferStatesGraphOwned = false;
    // Prepared graph tasks also declare the shared mesh-view CBV and material instance/typed SRVs. A frozen stream
    // can independently retain every selected mesh-source SRV, while direct/unprepared draws keep local setup.
    bool materialFrameStatesGraphOwned = false;
    // The graph may also retain and declare every source buffer selected by this frozen draw stream. Generated
    // emulation vertices remain local by default; an alias-free opaque split opts into the trailing graph handoff.
    bool materialGeometryStatesGraphOwned = false;
    // A split compute-emulation producer/raster pair receives this mesh's generated-vertex output in its required
    // entry state from the graph (UAV for the producer and VertexBuffer for the raster consumer). Compatibility
    // callers leave this false and retain the per-item native UAV-to-VertexBuffer handoff.
    bool emulationOutputEntryStateGraphOwned = false;
    // Prepared CSG draws consume the root-captured descriptor/buffer tuple. Regular and compatibility paths leave
    // this null because no CSG heap binding is part of their draw contract.
    const ECSRenderDetail::CsgGraphResourceSnapshot* csgResources = nullptr;
    // Graph-owned material recording consumes the root-captured frame buffers and descriptor generation. Native
    // compatibility callers leave this null and explicitly retain the live Mesh-owned binding path.
    const ECSRenderDetail::MeshFrameBindingSnapshot* frameBindings = nullptr;
};


// Ray tracing gathers refractive emission targets before Deferred ranks the current scene lights. The root module
// brokers this compact per-frame contract so neither feature reaches into the other's private state.
struct RayTracingLightingClassificationInput{
    u32 refractiveInstanceCount = 0u;
};
static_assert(sizeof(RayTracingLightingClassificationInput) == sizeof(u32));

struct RayTracingLightingClassification{
    u32 causticLightCount = 0u;
    u32 softShadowSlotMask = 0u;
};
static_assert(sizeof(RayTracingLightingClassification) == sizeof(u32) * 2u);

// Deferred owns these buffers, while the root module freezes their current generation for all later graph imports
// and ray-tracing task payloads. Owning handles keep deferred replacement from invalidating an accepted packet.
struct DeferredLightingGraphResources{
    Core::BufferHandle sceneShadingBuffer;
    Core::BufferHandle lightBuffer;

    [[nodiscard]] bool valid()const noexcept{ return sceneShadingBuffer && lightBuffer; }
};


// Heap-slot indirection for the ray-tracing material context.
struct RayTraceMaterialContextSlots{
    u32 sceneBvhNodes = 0u;
    u32 sceneInstances = 0u;
    u32 instanceMaterial = 0u;
    u32 materialTyped = 0u;

    u32 meshInstances = 0u;
    u32 _reserved0 = 0u;
    u32 _reserved1 = 0u;
    u32 _reserved2 = 0u;
};
static_assert(sizeof(RayTraceMaterialContextSlots) == sizeof(u32) * 8u, "Ray-trace material-context slots must stay two uint4 lanes");

// Nine std140 uint4 lanes of ordinary-pass heap slots.
struct DeferredBindlessResourceSlots{
    u32 gbufferBaseColor = 0u;
    u32 gbufferNormal = 0u;
    u32 gbufferWorldPosition = 0u;
    u32 gbufferDepth = 0u;

    u32 shadowVisibility = 0u;
    u32 causticIrradiance = 0u;
    u32 surfelIrradiance = 0u;
    u32 sampler = 0u;

    u32 opaqueColor = 0u;
    u32 avboitAccumColor = 0u;
    u32 avboitAccumExtinction = 0u;
    u32 opaqueColorStorage = 0u;

    u32 avboitTransmittance = 0u;
    u32 avboitLinearSampler = 0u;
    u32 sceneShading = 0u;
    u32 lightList = 0u;

    u32 avboitCoverage = 0u;
    u32 avboitDepthWarp = 0u;
    u32 avboitControl = 0u;
    u32 avboitExtinction = 0u;

    u32 avboitExtinctionOverflow = 0u;
    u32 avboitTransmittanceStorage = 0u;
    // Compute composite output sampled by the Graphics presentation blit.
    u32 compositeColor = 0u;
    u32 compositeColorStorage = 0u;

    // CSG typed Texture2DArray aliases use the global StorageImage heap.
    u32 csgCapBackNormal = 0u;
    u32 csgIntervalDepth = 0u;
    u32 csgIntervalId = 0u;
    u32 csgReceiverEventData = 0u;

    u32 csgReceiverEventCount = 0u;
    u32 csgReceiverSpanData = 0u;
    u32 csgReceiverSpanCount = 0u;
    u32 csgRemovedIntervalDepth = 0u;

    u32 csgRemovedIntervalCapNormal = 0u;
    u32 csgRemovedIntervalData = 0u;
    u32 csgRemovedIntervalCount = 0u;
    u32 _csgPad = 0u;
};
static_assert(sizeof(DeferredBindlessResourceSlots) == sizeof(u32) * 36u, "Deferred bindless slots must match nine std140 uint4 lanes");

// Deferred target generation owns these heap registrations and retires them before target release.
struct DeferredBindlessFrameResources{
    Core::BufferHandle slotsBuffer;
    DeferredBindlessResourceSlots slots;
    // Shared global-heap UniformBuffer slot payload.
    Core::GpuDescriptorHandle slotsBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferBaseColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferNormal = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferWorldPosition = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle gbufferDepth = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowVisibility = Core::GpuDescriptorHandle::invalid();
    // Writable view of the sampled shadow visibility array.
    Core::GpuDescriptorHandle shadowVisibilityStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticIrradiance = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticIrradianceStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradiance = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradianceStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradianceHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradianceHalfStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle sampler = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle opaqueColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle opaqueColorStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle compositeColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle compositeColorStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitAccumColor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitAccumExtinction = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitTransmittance = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle avboitLinearSampler = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle sceneShading = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle lightList = Core::GpuDescriptorHandle::invalid();
    // Typed uint accumulator plus floating-point caustic resolve views.
    Core::GpuDescriptorHandle causticAccumulator = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticAccumulatorStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticHistory = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticHistoryStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveHalfStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveGeometry = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticResolveGeometryStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowCoarseTransmittanceStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftGeometry = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftGeometryStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftGeometryPrev = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftGeometryPrevStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfAStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowSoftHalfBStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistAStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowHistBStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsAStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowMomentsBStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentSoftHalf = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentSoftHalfStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistAStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentHistBStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsA = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsAStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsB = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle transparentMomentsBStorage = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgCapBackNormal = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgIntervalDepth = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgIntervalId = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgReceiverEventData = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgReceiverEventCount = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgReceiverSpanData = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgReceiverSpanCount = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgRemovedIntervalDepth = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgRemovedIntervalCapNormal = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgRemovedIntervalData = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle csgRemovedIntervalCount = Core::GpuDescriptorHandle::invalid();
    bool slotsUploaded = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            slotsBuffer != nullptr
            && slotsBufferDescriptor.valid()
            && gbufferBaseColor.valid()
            && gbufferNormal.valid()
            && gbufferWorldPosition.valid()
            && gbufferDepth.valid()
            && shadowVisibility.valid()
            && shadowVisibilityStorage.valid()
            && causticIrradiance.valid()
            && causticIrradianceStorage.valid()
            && surfelIrradiance.valid()
            && surfelIrradianceStorage.valid()
            && surfelIrradianceHalf.valid()
            && surfelIrradianceHalfStorage.valid()
            && sampler.valid()
            && opaqueColor.valid()
            && opaqueColorStorage.valid()
            && compositeColor.valid()
            && compositeColorStorage.valid()
            && avboitAccumColor.valid()
            && avboitAccumExtinction.valid()
            && avboitTransmittance.valid()
            && avboitLinearSampler.valid()
            && sceneShading.valid()
            && lightList.valid()
            && causticAccumulator.valid()
            && causticAccumulatorStorage.valid()
            && causticHistory.valid()
            && causticHistoryStorage.valid()
            && causticResolveHalf.valid()
            && causticResolveHalfStorage.valid()
            && causticResolveGeometry.valid()
            && causticResolveGeometryStorage.valid()
            && shadowCoarseTransmittanceStorage.valid()
            && shadowSoftGeometry.valid()
            && shadowSoftGeometryStorage.valid()
            && shadowSoftGeometryPrev.valid()
            && shadowSoftGeometryPrevStorage.valid()
            && shadowSoftHalfA.valid()
            && shadowSoftHalfAStorage.valid()
            && shadowSoftHalfB.valid()
            && shadowSoftHalfBStorage.valid()
            && shadowHistA.valid()
            && shadowHistAStorage.valid()
            && shadowHistB.valid()
            && shadowHistBStorage.valid()
            && shadowMomentsA.valid()
            && shadowMomentsAStorage.valid()
            && shadowMomentsB.valid()
            && shadowMomentsBStorage.valid()
            && transparentSoftHalf.valid()
            && transparentSoftHalfStorage.valid()
            && transparentHistA.valid()
            && transparentHistAStorage.valid()
            && transparentHistB.valid()
            && transparentHistBStorage.valid()
            && transparentMomentsA.valid()
            && transparentMomentsAStorage.valid()
            && transparentMomentsB.valid()
            && transparentMomentsBStorage.valid()
            && csgCapBackNormal.valid()
            && csgIntervalDepth.valid()
            && csgIntervalId.valid()
            && csgReceiverEventData.valid()
            && csgReceiverEventCount.valid()
            && csgReceiverSpanData.valid()
            && csgReceiverSpanCount.valid()
            && csgRemovedIntervalDepth.valid()
            && csgRemovedIntervalCapNormal.valid()
            && csgRemovedIntervalData.valid()
            && csgRemovedIntervalCount.valid()
        ;
    }
};

// Optional immutable one-frame-lagged lighting history.
struct DeferredLaggedLightingHistoryResources{
    Core::TextureHandle shadowVisibility;
    Core::TextureHandle causticIrradiance;
    Core::TextureHandle surfelIrradiance;
    Core::BufferHandle slotsBuffer;
    DeferredBindlessResourceSlots slots;
    Core::GpuDescriptorHandle slotsBufferDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle shadowVisibilityDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle causticIrradianceDescriptor = Core::GpuDescriptorHandle::invalid();
    Core::GpuDescriptorHandle surfelIrradianceDescriptor = Core::GpuDescriptorHandle::invalid();
    // Prevents accepting history from a recycled target generation.
    u64 generation = 0u;
    bool slotsUploaded = false;

    [[nodiscard]] bool valid()const noexcept{
        return
            shadowVisibility != nullptr
            && causticIrradiance != nullptr
            && surfelIrradiance != nullptr
            && slotsBuffer != nullptr
            && slotsBufferDescriptor.valid()
            && shadowVisibilityDescriptor.valid()
            && causticIrradianceDescriptor.valid()
            && surfelIrradianceDescriptor.valid()
            && generation != 0u
        ;
    }
};

struct DeferredFrameTargets{
    u32 width = 0;
    u32 height = 0;
    Core::Format::Enum albedoFormat = Core::Format::UNKNOWN;
    Core::Format::Enum normalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum worldPositionFormat = Core::Format::UNKNOWN;
    Core::Format::Enum opaqueColorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum compositeColorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum depthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowVisibilityFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticIrradianceFormat = Core::Format::UNKNOWN;
    Core::Format::Enum surfelIrradianceFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticAccumulatorFormat = Core::Format::UNKNOWN;
    Core::Format::Enum causticHistoryFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgCapNormalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgIntervalDepthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgIntervalIdFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverEventDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverEventCountFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverSpanDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgReceiverSpanCountFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalDepthFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalCapNormalFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalDataFormat = Core::Format::UNKNOWN;
    Core::Format::Enum csgRemovedIntervalCountFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowCoarseTransmittanceFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowSoftFormat = Core::Format::UNKNOWN;
    Core::Format::Enum shadowSoftGeometryFormat = Core::Format::UNKNOWN;
    u32 csgPeelLayerCount = 0u;
    u32 csgReceiverEventLayerCount = 0u;
    u32 csgReceiverSpanLayerCount = 0u;
    u32 csgRemovedIntervalLayerCount = 0u;
    Core::TextureHandle albedo;
    Core::TextureHandle normal;
    Core::TextureHandle worldPosition;
    Core::TextureHandle csgCapBackNormal;
    Core::TextureHandle csgIntervalDepth;
    Core::TextureHandle csgIntervalId;
    Core::TextureHandle csgReceiverEventData;
    Core::TextureHandle csgReceiverEventCount;
    Core::TextureHandle csgReceiverSpanData;
    Core::TextureHandle csgReceiverSpanCount;
    Core::TextureHandle csgRemovedIntervalDepth;
    Core::TextureHandle csgRemovedIntervalCapNormal;
    Core::TextureHandle csgRemovedIntervalData;
    Core::TextureHandle csgRemovedIntervalCount;
    Core::TextureHandle opaqueColor;
    // Compute output presented by a Graphics blit.
    Core::TextureHandle compositeColor;
    Core::TextureHandle depth;
    // Full-resolution per-light visibility; HW opaque and SW transparent paths combine here.
    Core::TextureHandle shadowVisibility;
    // Half-resolution adaptive transparent-shadow scratch; null when disabled.
    Core::TextureHandle shadowCoarseTransmittance;
    // Half-resolution opaque soft-shadow ping-pong and geometry cache.
    Core::TextureHandle shadowSoftHalfA;
    Core::TextureHandle shadowSoftHalfB;
    Core::TextureHandle shadowSoftGeometry;
    // Reprojected opaque soft-shadow history and moments; reset with the visibility target.
    Core::TextureHandle shadowHistA;
    Core::TextureHandle shadowHistB;
    Core::TextureHandle shadowMomentsA;
    Core::TextureHandle shadowMomentsB;
    Core::TextureHandle shadowSoftGeometryPrev;
    // Separate transparent history is denoised before multiplying onto opaque visibility.
    Core::TextureHandle transparentSoftHalf;
    Core::TextureHandle transparentHistA;
    Core::TextureHandle transparentHistB;
    Core::TextureHandle transparentMomentsA;
    Core::TextureHandle transparentMomentsB;
    // Additive caustic targets; black is the no-producer identity.
    Core::TextureHandle causticIrradiance;
    // Screen-space surfel GI avoids sharing the writable pool with deferred lighting.
    Core::TextureHandle surfelIrradiance;
    // Half-resolution surfel GI gather output.
    Core::TextureHandle surfelIrradianceHalf;
    Core::TextureHandle causticAccumulator;
    // Half-resolution caustic a-trous ping-pong.
    Core::TextureHandle causticHistory;
    Core::TextureHandle causticResolveHalf;
    // Half-resolution caustic geometry cache.
    Core::TextureHandle causticResolveGeometry;
    Core::FramebufferHandle framebuffer;
    DeferredBindlessFrameResources bindless;
    DeferredLaggedLightingHistoryResources laggedLightingHistory;
    AvboitFrameTargets avboit;

    [[nodiscard]] bool csgIntervalTargetsValid()const noexcept{
        return
            csgCapNormalFormat != Core::Format::UNKNOWN
            && csgIntervalDepthFormat != Core::Format::UNKNOWN
            && csgIntervalIdFormat != Core::Format::UNKNOWN
            && csgReceiverEventDataFormat != Core::Format::UNKNOWN
            && csgReceiverEventCountFormat != Core::Format::UNKNOWN
            && csgReceiverSpanDataFormat != Core::Format::UNKNOWN
            && csgReceiverSpanCountFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDepthFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCapNormalFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalDataFormat != Core::Format::UNKNOWN
            && csgRemovedIntervalCountFormat != Core::Format::UNKNOWN
            && csgPeelLayerCount > 0u
            && csgReceiverEventLayerCount > 0u
            && csgReceiverSpanLayerCount > 0u
            && csgRemovedIntervalLayerCount > 0u
            && csgCapBackNormal != nullptr
            && csgIntervalDepth != nullptr
            && csgIntervalId != nullptr
            && csgReceiverEventData != nullptr
            && csgReceiverEventCount != nullptr
            && csgReceiverSpanData != nullptr
            && csgReceiverSpanCount != nullptr
            && csgRemovedIntervalDepth != nullptr
            && csgRemovedIntervalCapNormal != nullptr
            && csgRemovedIntervalData != nullptr
            && csgRemovedIntervalCount != nullptr
        ;
    }

    [[nodiscard]] bool valid()const noexcept{
#if defined(NWB_DEBUG)
        return
            width > 0
            && height > 0
            && albedoFormat != Core::Format::UNKNOWN
            && normalFormat != Core::Format::UNKNOWN
            && worldPositionFormat != Core::Format::UNKNOWN
            && opaqueColorFormat != Core::Format::UNKNOWN
            && compositeColorFormat != Core::Format::UNKNOWN
            && depthFormat != Core::Format::UNKNOWN
            && csgIntervalTargetsValid()
            && albedo != nullptr
            && normal != nullptr
            && worldPosition != nullptr
            && opaqueColor != nullptr
            && compositeColor != nullptr
            && depth != nullptr
            && framebuffer != nullptr
            && bindless.valid()
            && avboit.valid()
        ;
#else
        return framebuffer != nullptr;
#endif
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

