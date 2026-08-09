// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/kernel/system.h>

#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/kernel/renderer_private.h>

#include <core/graphics/gpu_timing.h>

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


// Records the non-publishing endpoint used when a later frame packet rejects after the prefix accepted. The graph
// lifecycle keeps the timing transaction alive only through an accepted recovery submission.
struct FrameRecoveryGraphTask{
    struct Payload{
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        bool retiresFrameTiming = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        return payload.frameTimingTransaction
            && (!payload.retiresFrameTiming || payload.frameTimingTransaction->recordEnd(commandList))
        ;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(
            payload.retiresFrameTiming
            && payload.frameTimingTransaction
            && !payload.frameTimingTransaction->confirmEndSubmission(false)
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: failed to retire frame recovery timing query"));
            payload.frameTimingTransaction->discard();
        }
    }

    static void discarded(Payload& payload){
        if(payload.frameTimingTransaction)
            payload.frameTimingTransaction->discard();
    }
};


// Shadow preparation owns the one Graphics packet that uploads bindless indirection and builds the current trace
// scene. Its exact final snapshot becomes the serial state base for the following graphics-prefix packet.
struct ShadowPrepareGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool deferredBindlessSlotsWereUploaded = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.renderer || !payload.targets || !payload.timingTicket)
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_PrepareArena);

        renderer.m_preparedShadowVisibilityReady = false;
        const bool deferredBindlessResourcesUploaded = renderer.m_deferredSystem.uploadDeferredBindlessFrameResources(
            commandList,
            *payload.targets
        );
        const bool shadowResourcesPrepared = deferredBindlessResourcesUploaded
            && renderer.m_raytracingSystem.prepareShadowVisibilityResources(
                commandList,
                *payload.targets,
                scratchArena,
                renderer.m_preparedShadowVisibilityReady
            )
        ;
        // Upload final slot indirection after all capacity growth settles.
        return shadowResourcesPrepared
            && renderer.m_raytracingSystem.uploadRayTraceMaterialContextSlots(commandList)
        ;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        static_cast<void>(token);
        if(payload.renderer)
            payload.renderer->m_raytracingSystem.finalizeSurfelResourceInitialization();
    }

    static void discarded(Payload& payload){
        if(payload.timingTicket)
            payload.timingTicket->discard();
        if(!payload.renderer)
            return;

        RendererSystem& renderer = *payload.renderer;
        // Failed preparation forces a safe cache rebuild; a previous key may name retired storage.
        renderer.m_rayTracingState.m_tlasStaticSceneHashValid = false;
        renderer.m_rayTracingState.m_sceneSwBvhStaticSceneHashValid = false;
        renderer.m_rayTracingState.m_hwShadowMaterialContextHashValid = false;
        renderer.m_rayTracingState.m_swShadowMaterialContextHashValid = false;
        renderer.m_preparedShadowVisibilityReady = false;
        if(payload.targets)
            payload.targets->bindless.slotsUploaded = payload.deferredBindlessSlotsWereUploaded;
        renderer.m_raytracingSystem.discardSurfelResourceInitialization();
    }
};


// Mesh-view setup is the first native graphics-prefix task.  Its dynamic upload barriers remain intrinsic to the
// upload, while the graph owns packet routing and the declared final ConstantBuffer state.
struct MeshViewSetupGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool* ready = nullptr;
        f32 meshViewAspectRatio = 1.f;
        bool shadowVisibilityRunsOnCompute = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.frameTimingTransaction
            || !payload.asyncPrefixTiming
            || !payload.timingTicket
            || !payload.ready
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        const bool recordsGraphicsFrameMarker =
            !payload.shadowVisibilityRunsOnCompute && RendererGpuTimingScope::s_Frame.valid()
        ;
        if(recordsGraphicsFrameMarker)
            commandList.beginMarker(RendererGpuTimingScope::s_Frame.markerLabel);

        const bool frameTimingStarted = payload.frameTimingTransaction->begin(
            RendererGpuTimingScope::s_Frame,
            renderer.m_graphics.getDevice(),
            commandList
        );
        if(payload.shadowVisibilityRunsOnCompute){
            payload.asyncPrefixTiming->emplace(
                renderer.m_graphics.gpuTiming(),
                RendererGpuTimingScope::s_AsyncPrefix,
                renderer.m_graphics.getDevice(),
                commandList
            );
            payload.asyncPrefixTiming->value().finishMarker();
        }

        *payload.ready = renderer.m_meshSystem.updateMeshViewBuffer(commandList, payload.meshViewAspectRatio);
        if(recordsGraphicsFrameMarker)
            commandList.endMarker();
        return frameTimingStarted;
    }
};


// Scene-shading setup follows mesh-view setup. It retains dynamic upload barriers internally while graph
// declarations establish the final states consumed by later prefix work.
struct SceneShadingSetupGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool* ready = nullptr;
        f32 meshViewAspectRatio = 1.f;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.renderer || !payload.timingTicket || !payload.ready)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        *payload.ready = payload.renderer->m_deferredSystem.updateSceneShadingBuffer(
            commandList,
            payload.meshViewAspectRatio
        );
        return true;
    }
};


// Deferred clear follows fully native setup. Its entry transitions are declaration-driven, so the recording body
// contains only clear commands and timing.
struct DeferredClearGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool clearSurfelIrradiance = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.renderer || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.renderer->m_deferredSystem.clearDeferredTargets(
            commandList,
            *payload.targets,
            payload.clearSurfelIrradiance
        );
        return true;
    }
};


// The opaque G-buffer keeps its existing recording body, but now runs after the native deferred clear. Its resource
// barriers and final state are therefore part of the graph packet.
struct GbufferGraphTask{
    struct Payload{
        RendererSystem* renderer = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool hasOpaqueCsgFrameWork = false;
        const bool* meshViewSetupReady = nullptr;
        const bool* sceneShadingSetupReady = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.renderer
            || !payload.targets
            || !payload.csgFrameState
            || !payload.timingTicket
            || !payload.meshViewSetupReady
            || !payload.sceneShadingSetupReady
        )
            return false;

        RendererSystem& renderer = *payload.renderer;
        DeferredFrameTargets& deferredTargets = *payload.targets;
        const CsgFrameState& csgFrameState = *payload.csgFrameState;
        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_RenderArena);

        MaterialPassDrawItemPartitions opaqueDrawItems{ scratchArena };
        InstanceGpuDataVector instanceData{ scratchArena };
        CsgFrameGpuData csgFrameData{ scratchArena };
#if defined(NWB_DEBUG)
        ECSRenderDetail::MaterialTypedInstanceRangeVector materialTypedRanges{ scratchArena };
#endif
        MaterialTypedByteDataVector materialTypedBytes{ scratchArena };

        Core::ViewportState deferredViewportState;
        deferredViewportState.addViewportAndScissorRect(deferredTargets.framebuffer->getFramebufferInfo().getViewport());

        const bool frameSetupReady =
            *payload.meshViewSetupReady
            && payload.sceneShadingSetupReady
            && *payload.sceneShadingSetupReady
        ;
        if(frameSetupReady){
            renderer.m_materialSystem.gatherMaterialPassDrawItems(
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                false,
                csgFrameState,
                opaqueDrawItems,
                instanceData,
                csgFrameData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes,
                RendererResourceLookupMode::PreparedOnly
            );
        }

        const Core::Rect opaqueCsgClearRect = csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height);
        if(payload.hasOpaqueCsgFrameWork)
            renderer.m_deferredSystem.clearCsgIntervalTargets(commandList, deferredTargets, opaqueCsgClearRect);

        const bool hasDeferredDrawItems = !opaqueDrawItems.empty();
        const bool deferredResourcesReady =
            hasDeferredDrawItems
            && renderer.m_materialSystem.materialPassDrawBuffersReady(instanceData, materialTypedBytes)
        ;
        const bool regularDrawResourcesReady =
            deferredResourcesReady
            && renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.regular)
        ;
        const bool csgResourcesReady =
            deferredResourcesReady
            && (opaqueDrawItems.csg.empty() || renderer.m_csgSystem.csgFrameBuffersReady(csgFrameData))
        ;
        const bool csgDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csg.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csg))
        ;
        const bool csgReceiverSurfaceDrawResourcesReady =
            csgResourcesReady
            && (opaqueDrawItems.csgReceiverSurface.empty() || renderer.m_materialSystem.materialPassDrawResourcesReady(opaqueDrawItems.csgReceiverSurface))
        ;
        const bool deferredUploadReady =
            deferredResourcesReady
            && renderer.m_materialSystem.uploadMaterialPassDrawBuffers(
                commandList,
                instanceData,
#if defined(NWB_DEBUG)
                materialTypedRanges,
#endif
                materialTypedBytes
            )
        ;
        if(deferredUploadReady){
            const bool csgUploadReady =
                csgResourcesReady
                && (opaqueDrawItems.csg.empty() || renderer.m_csgSystem.uploadCsgFrameBuffers(commandList, csgFrameData))
            ;
            const bool csgSampleStateReady =
                csgUploadReady
                && (!csgFrameData.hasWork() || renderer.m_csgSystem.uploadCsgIntervalSampleState(commandList, deferredTargets, csgFrameData))
            ;
            if(csgSampleStateReady && csgFrameData.hasWork())
                renderer.m_csgSystem.dispatchCsgIntervalPeels(commandList, deferredTargets, csgFrameData);
            const MaterialPassDrawContext opaqueDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::Opaque,
                nullptr,
                deferredViewportState
            };
            if(regularDrawResourcesReady && !opaqueDrawItems.regular.empty()){
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueRegular,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.regular);
            }

            Core::ViewportState csgIntervalViewportState;
            csgIntervalViewportState
                .addViewport(deferredTargets.framebuffer->getFramebufferInfo().getViewport())
                .addScissorRect(csgFrameData.workRegion.resolveRect(deferredTargets.width, deferredTargets.height))
            ;
            const MaterialPassDrawContext csgReceiverSurfaceDrawContext{
                commandList,
                deferredTargets.framebuffer.get(),
                MaterialPipelinePass::CsgReceiverSurface,
                nullptr,
                csgIntervalViewportState
            };
            if(csgSampleStateReady && csgReceiverSurfaceDrawResourcesReady && !opaqueDrawItems.csgReceiverSurface.empty()){
                Core::GpuTimingMeasure timing(
                    renderer.m_graphics.gpuTiming(),
                    RendererGpuTimingScope::s_OpaqueCsgReceiverSurface,
                    renderer.m_graphics.getDevice(),
                    commandList
                );
                renderer.m_materialSystem.renderMaterialPassDrawItems(
                    csgReceiverSurfaceDrawContext,
                    opaqueDrawItems.csgReceiverSurface
                );
            }
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                renderer.m_csgSystem.dispatchCsgReceiverSpanBuild(commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                renderer.m_csgSystem.dispatchCsgIntervalCombine(commandList, deferredTargets, csgFrameData);
            if(csgSampleStateReady && csgDrawResourcesReady){
                if(!opaqueDrawItems.csg.empty()){
                    Core::GpuTimingMeasure timing(
                        renderer.m_graphics.gpuTiming(),
                        RendererGpuTimingScope::s_OpaqueCsg,
                        renderer.m_graphics.getDevice(),
                        commandList
                    );
                    renderer.m_materialSystem.renderMaterialPassDrawItems(opaqueDrawContext, opaqueDrawItems.csg);
                }
                if(csgFrameData.hasWork() && csgReceiverSurfaceDrawResourcesReady)
                    renderer.m_csgSystem.renderCsgIntervalCaps(commandList, deferredTargets, csgFrameData);
            }
        }
        commandList.endRenderPass();
        return true;
    }
};


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_task_graph{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static Core::GpuQueueRequest GraphicsQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Graphics,
        Core::GpuQueuePreference::Graphics,
        false,
        false,
    };
}

[[nodiscard]] static Core::GpuQueueRequest ComputeQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Compute,
        Core::GpuQueuePreference::Compute,
        true,
        true,
    };
}

[[nodiscard]] static Core::GpuQueueRequest TransferQueueRequest(){
    return Core::GpuQueueRequest{
        Core::GpuQueueCapability::Transfer,
        Core::GpuQueuePreference::Transfer,
        true,
        true,
    };
}


// This is the first late-native-recording task.  It is intentionally self-contained so the compiler may route it
// through the existing Graphics or Compute transport today and a distinct Transfer queue in a later phase.
struct LaggedLightingHistoryCopyTask{
    struct Payload{
        Core::TextureHandle sourceShadowVisibility;
        Core::TextureHandle sourceCausticIrradiance;
        Core::TextureHandle sourceSurfelIrradiance;
        Core::TextureHandle destinationShadowVisibility;
        Core::TextureHandle destinationCausticIrradiance;
        Core::TextureHandle destinationSurfelIrradiance;
        Core::QueueSubmissionToken* acceptedHistoryToken = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.sourceShadowVisibility
            || !payload.sourceCausticIrradiance
            || !payload.sourceSurfelIrradiance
            || !payload.destinationShadowVisibility
            || !payload.destinationCausticIrradiance
            || !payload.destinationSurfelIrradiance
        )
            return false;

        // Packet-boundary states are declared with the task and lowered by GpuNativePacketRecorder before this
        // thunk runs.  The copy body keeps only the work intrinsic to this task.
        for(u32 shadowSlot = 0u; shadowSlot < NWB_SCENE_SHADOW_SLOT_COUNT; ++shadowSlot){
            Core::TextureSlice shadowSlice;
            shadowSlice.setArraySlice(shadowSlot);
            commandList.copyTexture(
                payload.destinationShadowVisibility.get(),
                shadowSlice,
                payload.sourceShadowVisibility.get(),
                shadowSlice
            );
        }
        const Core::TextureSlice irradianceSlice;
        commandList.copyTexture(
            payload.destinationCausticIrradiance.get(),
            irradianceSlice,
            payload.sourceCausticIrradiance.get(),
            irradianceSlice
        );
        commandList.copyTexture(
            payload.destinationSurfelIrradiance.get(),
            irradianceSlice,
            payload.sourceSurfelIrradiance.get(),
            irradianceSlice
        );
        return true;
    }

    static void accepted(Payload& payload, const Core::QueueSubmissionToken& token){
        if(payload.acceptedHistoryToken)
            *payload.acceptedHistoryToken = token;
    }

    static void discarded(Payload& payload){
        if(payload.acceptedHistoryToken)
            *payload.acceptedHistoryToken = {};
    }
};


// The terminal graphics-prefix task normalizes the state exported to the following graph packets.
struct PostGbufferNormalizeGraphTask{
    struct Payload{
        RendererRayTracingSystem* raytracingSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncPrefixTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool shadowVisibilityRunsOnCompute = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.raytracingSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.raytracingSystem->normalizePostGbufferPacketResources(commandList, *payload.targets);
        if(payload.shadowVisibilityRunsOnCompute && payload.asyncPrefixTiming && *payload.asyncPrefixTiming){
            (*payload.asyncPrefixTiming)->finishTiming(commandList);
            payload.asyncPrefixTiming->reset();
        }
        return true;
    }
};


// AVBOIT remains explicitly staged because its raster/compute alternation is a real dependency chain.  The graph
// owns those packets now; manual state handoffs only seed native recording until automatic graph barriers arrive.
static void RestoreAvboitGbufferInputs(Core::CommandList& commandList, DeferredFrameTargets& targets){
    commandList.setTextureState(
        targets.albedo.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.normal.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.worldPosition.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
    commandList.setTextureState(
        targets.depth.get(),
        ECSRenderDetail::s_FramebufferSubresources,
        Core::ResourceStates::ShaderResource
    );
}


struct AvboitPreGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool clearTargets = false;
        bool hasTransparentRenderers = false;
        bool splitStages = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.clearTargets)
            payload.avboitSystem->clearAvboitTargets(commandList, payload.targets->avboit);
        if(payload.hasTransparentRenderers){
            if(payload.splitStages)
                payload.avboitSystem->renderAvboitPreDepthWarpPasses(
                    commandList,
                    *payload.targets,
                    *payload.csgFrameState
                );
            else
                payload.avboitSystem->renderAvboitPasses(commandList, *payload.targets, *payload.csgFrameState);
        }
        RestoreAvboitGbufferInputs(commandList, *payload.targets);
        return true;
    }
};


struct AvboitDepthWarpGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitDepthWarp(commandList, *payload.targets);
        return true;
    }
};


struct AvboitExtinctionGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->renderAvboitExtinctionPass(commandList, *payload.targets, *payload.csgFrameState);
        return true;
    }
};


struct AvboitIntegrationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        AvboitFrameTargets* targets = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->dispatchAvboitIntegration(commandList, *payload.targets);
        return true;
    }
};


struct AvboitAccumulationGraphTask{
    struct Payload{
        RendererAvboitSystem* avboitSystem = nullptr;
        DeferredFrameTargets* targets = nullptr;
        const CsgFrameState* csgFrameState = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(!payload.avboitSystem || !payload.targets || !payload.csgFrameState || !payload.timingTicket)
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        payload.avboitSystem->renderAvboitAccumulatePass(commandList, *payload.targets, *payload.csgFrameState);
        return true;
    }
};


struct DeferredPresentGraphTask{
    struct Payload{
        RendererDeferredSystem* deferredSystem = nullptr;
        Core::Graphics* graphics = nullptr;
        DeferredFrameTargets* targets = nullptr;
        Core::Framebuffer* presentationFramebuffer = nullptr;
        Core::GpuTimingFrameTransaction* frameTimingTransaction = nullptr;
        Optional<Core::GpuTimingMeasure>* asyncFinalTiming = nullptr;
        Core::GpuTimingSubmissionTicket* timingTicket = nullptr;
        bool shadowVisibilityRunsOnCompute = false;
    };

    [[nodiscard]] static bool record(
        const Payload& payload,
        Core::CommandList& commandList,
        const Core::GpuTaskRecordContext& context
    ){
        static_cast<void>(context);
        if(
            !payload.deferredSystem
            || !payload.graphics
            || !payload.targets
            || !payload.presentationFramebuffer
            || !payload.frameTimingTransaction
            || !payload.timingTicket
            || (payload.shadowVisibilityRunsOnCompute && !payload.asyncFinalTiming)
        )
            return false;

        Core::GpuTimingSubmissionTicket::RecordingScope timingRecording(*payload.timingTicket);
        if(payload.shadowVisibilityRunsOnCompute){
            payload.asyncFinalTiming->emplace(
                payload.graphics->gpuTiming(),
                RendererGpuTimingScope::s_AsyncFinal,
                payload.graphics->getDevice(),
                commandList
            );
            payload.asyncFinalTiming->value().finishMarker();
        }

        const bool presentRecorded = payload.deferredSystem->renderDeferredPresent(
            commandList,
            *payload.targets,
            payload.presentationFramebuffer
        );
        const bool frameTimingEnded = presentRecorded
            && payload.frameTimingTransaction->recordEnd(commandList)
        ;
        if(payload.shadowVisibilityRunsOnCompute && presentRecorded && payload.asyncFinalTiming->has_value()){
            payload.asyncFinalTiming->value().finishTiming(commandList);
            payload.asyncFinalTiming->reset();
        }
        return presentRecorded && frameTimingEnded;
    }
};


[[nodiscard]] static Core::GpuGraphResourceDesc TextureResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Texture)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc BufferResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::Buffer)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuGraphResourceDesc HazardDomainDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::HazardDomain)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource
){
    Core::GpuTaskResourceUse result = ReadUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] static Core::GpuTaskResourceUse WriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Write,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse WriteTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state
){
    Core::GpuTaskResourceUse result = WriteUse(resource, state);
    result.range.textureSubresources = subresources;
    return result;
}

[[nodiscard]] static Core::GpuGraphResourceDesc AccelStructResourceDesc(const Name& identity, const AStringView label){
    Core::GpuGraphResourceDesc desc;
    desc
        .setIdentity(identity)
        .setMarkerLabel(label)
        .setType(Core::GpuGraphResourceType::AccelStruct)
    ;
    return desc;
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadWriteUse(
    const Core::GpuGraphResourceId resource,
    const Core::ResourceStates::Mask state
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::ReadWrite,
    };
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void RendererSystem::buildFrameRecoveryTaskGraph(
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    const bool retiresFrameTiming,
    const bool waitsForAsyncProducer
){
    using namespace __hidden_renderer_task_graph;

    m_frameRecoveryTaskGraphValid = false;
    m_frameRecoveryTask = {};
    m_frameRecoveryAsyncCompletion = {};
    m_frameRecoveryTaskGraph.reset();
    m_frameRecoveryTaskGraphAnalysis.reset();
    m_frameRecoveryTaskGraphQueueAssignments.reset();
    m_frameRecoveryCompiledGraph.reset();
    m_frameRecoveryRecordedGraph.reset(m_frameRecoveryCompiledGraph);
    m_frameRecoverySubmissionTransaction.reset(m_frameRecoveryCompiledGraph);

    const Core::GpuGraphResourceId recoveryDomain = m_frameRecoveryTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.frame_recovery.timing"), "Frame Recovery Timing")
    );
    if(!recoveryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import frame-recovery graph resources"));
        return;
    }

    if(waitsForAsyncProducer){
        Core::GpuExternalCompletionDesc asyncCompletionDesc;
        asyncCompletionDesc
            .setIdentity(Name("render.frame_recovery.async_complete"))
            .setMarkerLabel("Latest Async Producer Complete")
        ;
        m_frameRecoveryAsyncCompletion = m_frameRecoveryTaskGraph.importExternalCompletion(asyncCompletionDesc);
        if(!m_frameRecoveryAsyncCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import frame-recovery async completion"));
            return;
        }
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadWriteUse(recoveryDomain, Core::ResourceStates::Common),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.frame_recovery"))
        .setMarkerLabel("Frame Recovery")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(
            waitsForAsyncProducer ? &m_frameRecoveryAsyncCompletion : nullptr,
            waitsForAsyncProducer ? 1u : 0u
        )
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_frameRecoveryTask = m_frameRecoveryTaskGraph.addTask<ECSRenderDetail::FrameRecoveryGraphTask>(
        desc,
        ECSRenderDetail::FrameRecoveryGraphTask::Payload{
            .frameTimingTransaction = &frameTimingTransaction,
            .retiresFrameTiming = retiresFrameTiming,
        }
    );
    if(!m_frameRecoveryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare frame-recovery task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_frameRecoveryTaskGraph,
        m_frameRecoveryTaskGraphAnalysis,
        topology,
        m_frameRecoveryTaskGraphQueueAssignments,
        m_frameRecoveryCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile frame-recovery task graph"));
        return;
    }
    m_frameRecoveryRecordedGraph.reset(m_frameRecoveryCompiledGraph);
    m_frameRecoverySubmissionTransaction.reset(m_frameRecoveryCompiledGraph);
    m_frameRecoveryTaskGraphValid = true;
}


void RendererSystem::buildShadowPrepareTaskGraph(
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_shadowPrepareTaskGraphValid = false;
    m_shadowPrepareTask = {};
    m_shadowPrepareTaskGraph.reset();
    m_shadowPrepareTaskGraphAnalysis.reset();
    m_shadowPrepareTaskGraphQueueAssignments.reset();
    m_shadowPrepareCompiledGraph.reset();
    m_shadowPrepareRecordedGraph.reset(m_shadowPrepareCompiledGraph);
    m_shadowPrepareSubmissionTransaction.reset(m_shadowPrepareCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    Core::GpuGraphResourceDesc bindlessSlotsDesc = BufferResourceDesc(
        Name("render.shadow_prepare.bindless_slots"),
        "Deferred Bindless Slots"
    );
    // The accepted upload leaves this selector in ConstantBuffer. A rejected graph task restores slotsUploaded,
    // so this dynamic initial state is the actual serial Graphics state rather than the allocation-time default.
    bindlessSlotsDesc.setInitialState(
        deferredTargets.bindless.slotsUploaded
            ? Core::ResourceStates::ConstantBuffer
            : deferredTargets.bindless.slotsBuffer->getDescription().initialState
    );
    const Core::GpuGraphResourceId bindlessSlots = m_shadowPrepareTaskGraph.importBuffer(
        deferredTargets.bindless.slotsBuffer,
        bindlessSlotsDesc
    );
    const Core::GpuGraphResourceId preparationDomain = m_shadowPrepareTaskGraph.importHazardDomain(
        HazardDomainDesc(
            Name("render.shadow_prepare.dynamic_resources"),
            "Dynamic Trace Preparation Resources"
        )
    );
    if(!bindlessSlots.valid() || !preparationDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-preparation graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        // This final state is consumed by every following bindless reader. Additional trace buffers are allocated or
        // replaced while the task records, so their exact final states are exported from the native packet snapshot.
        ReadWriteUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        ReadWriteUse(preparationDomain, Core::ResourceStates::Common),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_prepare"))
        .setMarkerLabel("Shadow Preparation")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_shadowPrepareTask = m_shadowPrepareTaskGraph.addTask<ECSRenderDetail::ShadowPrepareGraphTask>(
        desc,
        ECSRenderDetail::ShadowPrepareGraphTask::Payload{
            .renderer = this,
            .targets = &deferredTargets,
            .timingTicket = &timingTicket,
            .deferredBindlessSlotsWereUploaded = deferredTargets.bindless.slotsUploaded,
        }
    );
    if(!m_shadowPrepareTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow-preparation task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_shadowPrepareTaskGraph,
        m_shadowPrepareTaskGraphAnalysis,
        topology,
        m_shadowPrepareTaskGraphQueueAssignments,
        m_shadowPrepareCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile shadow-preparation task graph"));
        return;
    }
    m_shadowPrepareRecordedGraph.reset(m_shadowPrepareCompiledGraph);
    m_shadowPrepareSubmissionTransaction.reset(m_shadowPrepareCompiledGraph);
    m_shadowPrepareTaskGraphValid = true;
}


void RendererSystem::buildGraphicsPrefixTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool hasOpaqueCsgFrameWork,
    const f32 meshViewAspectRatio,
    const bool shadowVisibilityRunsOnCompute,
    const bool surfelGiRunsOnCompute,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncPrefixTiming,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_graphicsPrefixTaskGraphValid = false;
    m_graphicsPrefixMeshViewSetupTask = {};
    m_graphicsPrefixSceneShadingSetupTask = {};
    m_graphicsPrefixDeferredClearTask = {};
    m_graphicsPrefixGbufferTask = {};
    m_graphicsPrefixTask = {};
    m_graphicsPrefixMeshViewSetupReady = false;
    m_graphicsPrefixSceneShadingSetupReady = false;
    m_graphicsPrefixTaskGraph.reset();
    m_graphicsPrefixTaskGraphAnalysis.reset();
    m_graphicsPrefixTaskGraphQueueAssignments.reset();
    m_graphicsPrefixCompiledGraph.reset();
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);

    if(
        !deferredTargets.valid()
        || !m_drawState.m_meshViewBuffer
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_graphicsPrefixTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.graphics_prefix.albedo"),
        "Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.graphics_prefix.normal"),
        "Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.graphics_prefix.world_position"),
        "World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.graphics_prefix.depth"),
        "Depth"
    );
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.graphics_prefix.opaque_color"),
        "Opaque Color"
    );
    const bool clearSurfelIrradiance = !surfelGiRunsOnCompute && deferredTargets.surfelIrradiance != nullptr;
    Core::GpuGraphResourceId surfelIrradiance;
    if(clearSurfelIrradiance){
        surfelIrradiance = importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.graphics_prefix.surfel_irradiance"),
            "Surfel Irradiance"
        );
    }
    const Core::GpuGraphResourceId sceneShadingBuffer = m_graphicsPrefixTaskGraph.importBuffer(
        m_deferredState.m_sceneShadingBuffer,
        BufferResourceDesc(Name("render.graphics_prefix.scene_shading"), "Scene Shading")
    );
    const Core::GpuGraphResourceId lightBuffer = m_graphicsPrefixTaskGraph.importBuffer(
        m_deferredState.m_lightBuffer,
        BufferResourceDesc(Name("render.graphics_prefix.lights"), "Lights")
    );
    const Core::GpuGraphResourceId meshViewBuffer = m_graphicsPrefixTaskGraph.importBuffer(
        m_drawState.m_meshViewBuffer,
        BufferResourceDesc(Name("render.graphics_prefix.mesh_view"), "Mesh View")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !opaqueColor.valid()
        || (clearSurfelIrradiance && !surfelIrradiance.valid())
        || !sceneShadingBuffer.valid()
        || !lightBuffer.valid()
        || !meshViewBuffer.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse meshViewSetupResourceUses[] = {
        WriteUse(meshViewBuffer, Core::ResourceStates::ConstantBuffer),
    };
    Core::GpuTaskSchedulingHint meshViewSetupScheduling;
    meshViewSetupScheduling.cost = Core::GpuTaskCostHint::Medium;
    meshViewSetupScheduling.forceSubmissionBoundary = false;
    meshViewSetupScheduling.allowPacketMerge = true;
    Core::GpuTaskDesc meshViewSetupDesc;
    meshViewSetupDesc
        .setIdentity(Name("render.graphics_prefix.mesh_view_setup"))
        .setMarkerLabel("Mesh View Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(meshViewSetupScheduling)
        .setResourceUses(meshViewSetupResourceUses, LengthOf(meshViewSetupResourceUses))
    ;
    m_graphicsPrefixMeshViewSetupTask = m_graphicsPrefixTaskGraph.addTask<ECSRenderDetail::MeshViewSetupGraphTask>(
        meshViewSetupDesc,
        ECSRenderDetail::MeshViewSetupGraphTask::Payload{
            .renderer = this,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = &timingTicket,
            .ready = &m_graphicsPrefixMeshViewSetupReady,
            .meshViewAspectRatio = meshViewAspectRatio,
            .shadowVisibilityRunsOnCompute = shadowVisibilityRunsOnCompute,
        }
    );
    if(!m_graphicsPrefixMeshViewSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare mesh-view setup task"));
        return;
    }

    const Core::GpuTaskResourceUse sceneShadingSetupResourceUses[] = {
        WriteUse(sceneShadingBuffer, Core::ResourceStates::ConstantBuffer),
        WriteUse(lightBuffer, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint sceneShadingSetupScheduling;
    sceneShadingSetupScheduling.cost = Core::GpuTaskCostHint::Tiny;
    sceneShadingSetupScheduling.forceSubmissionBoundary = false;
    sceneShadingSetupScheduling.allowPacketMerge = true;
    sceneShadingSetupScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc sceneShadingSetupDesc;
    sceneShadingSetupDesc
        .setIdentity(Name("render.graphics_prefix.scene_shading_setup"))
        .setMarkerLabel("Scene Shading Setup")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(sceneShadingSetupScheduling)
        .setDependencies(&m_graphicsPrefixMeshViewSetupTask, 1u)
        .setResourceUses(sceneShadingSetupResourceUses, LengthOf(sceneShadingSetupResourceUses))
    ;
    m_graphicsPrefixSceneShadingSetupTask = m_graphicsPrefixTaskGraph.addTask<ECSRenderDetail::SceneShadingSetupGraphTask>(
        sceneShadingSetupDesc,
        ECSRenderDetail::SceneShadingSetupGraphTask::Payload{
            .renderer = this,
            .timingTicket = &timingTicket,
            .ready = &m_graphicsPrefixSceneShadingSetupReady,
            .meshViewAspectRatio = meshViewAspectRatio,
        }
    );
    if(!m_graphicsPrefixSceneShadingSetupTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare scene-shading setup task"));
        return;
    }

    {
        Core::Alloc::ScratchArena clearResourceScratch(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> clearResourceUses{ clearResourceScratch };
        clearResourceUses.reserve(6u);
        clearResourceUses.push_back(WriteTextureUse(
            albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            opaqueColor,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        clearResourceUses.push_back(WriteTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::CopyDest
        ));
        if(clearSurfelIrradiance){
            clearResourceUses.push_back(WriteTextureUse(
                surfelIrradiance,
                ECSRenderDetail::s_FramebufferSubresources,
                Core::ResourceStates::CopyDest
            ));
        }

        Core::GpuTaskSchedulingHint clearScheduling;
        clearScheduling.cost = Core::GpuTaskCostHint::Tiny;
        clearScheduling.forceSubmissionBoundary = false;
        clearScheduling.allowPacketMerge = true;
        clearScheduling.mergeWithPrevious = true;
        Core::GpuTaskDesc clearDesc;
        clearDesc
            .setIdentity(Name("render.graphics_prefix.deferred_clear"))
            .setMarkerLabel("Deferred Clear")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(clearScheduling)
            .setDependencies(&m_graphicsPrefixSceneShadingSetupTask, 1u)
            .setResourceUses(clearResourceUses.data(), clearResourceUses.size())
        ;
        m_graphicsPrefixDeferredClearTask = m_graphicsPrefixTaskGraph.addTask<ECSRenderDetail::DeferredClearGraphTask>(
            clearDesc,
            ECSRenderDetail::DeferredClearGraphTask::Payload{
                .renderer = this,
                .targets = &deferredTargets,
                .timingTicket = &timingTicket,
                .clearSurfelIrradiance = clearSurfelIrradiance,
            }
        );
    }
    if(!m_graphicsPrefixDeferredClearTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-clear task"));
        return;
    }

    const Core::GpuTaskResourceUse gbufferResourceUses[] = {
        ReadUse(meshViewBuffer, Core::ResourceStates::ConstantBuffer),
        WriteUse(albedo, Core::ResourceStates::RenderTarget),
        WriteUse(normal, Core::ResourceStates::RenderTarget),
        WriteUse(worldPosition, Core::ResourceStates::RenderTarget),
        WriteUse(depth, Core::ResourceStates::DepthWrite),
    };
    Core::GpuTaskSchedulingHint gbufferScheduling;
    gbufferScheduling.cost = Core::GpuTaskCostHint::Medium;
    gbufferScheduling.forceSubmissionBoundary = false;
    gbufferScheduling.allowPacketMerge = true;
    gbufferScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc gbufferDesc;
    gbufferDesc
        .setIdentity(Name("render.graphics_prefix.gbuffer"))
        .setMarkerLabel("Opaque G-Buffer")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(gbufferScheduling)
        .setDependencies(&m_graphicsPrefixDeferredClearTask, 1u)
        .setResourceUses(gbufferResourceUses, LengthOf(gbufferResourceUses))
    ;
    m_graphicsPrefixGbufferTask = m_graphicsPrefixTaskGraph.addTask<ECSRenderDetail::GbufferGraphTask>(
        gbufferDesc,
        ECSRenderDetail::GbufferGraphTask::Payload{
            .renderer = this,
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .timingTicket = &timingTicket,
            .hasOpaqueCsgFrameWork = hasOpaqueCsgFrameWork,
            .meshViewSetupReady = &m_graphicsPrefixMeshViewSetupReady,
            .sceneShadingSetupReady = &m_graphicsPrefixSceneShadingSetupReady,
        }
    );
    if(!m_graphicsPrefixGbufferTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare opaque G-buffer task"));
        return;
    }

    const Core::GpuTaskResourceUse normalizeResourceUses[] = {
        ReadUse(meshViewBuffer, Core::ResourceStates::ConstantBuffer),
        ReadUse(normal, Core::ResourceStates::ShaderResource),
        ReadUse(worldPosition, Core::ResourceStates::ShaderResource),
        ReadUse(depth, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint normalizeScheduling;
    normalizeScheduling.cost = Core::GpuTaskCostHint::Tiny;
    normalizeScheduling.forceSubmissionBoundary = false;
    normalizeScheduling.allowPacketMerge = true;
    normalizeScheduling.mergeWithPrevious = true;
    Core::GpuTaskDesc normalizeDesc;
    normalizeDesc
        .setIdentity(Name("render.graphics_prefix.normalize"))
        .setMarkerLabel("Post-G-Buffer Normalize")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(normalizeScheduling)
        .setDependencies(&m_graphicsPrefixGbufferTask, 1u)
        .setResourceUses(normalizeResourceUses, LengthOf(normalizeResourceUses))
    ;
    m_graphicsPrefixTask = m_graphicsPrefixTaskGraph.addTask<PostGbufferNormalizeGraphTask>(
        normalizeDesc,
        PostGbufferNormalizeGraphTask::Payload{
            .raytracingSystem = &m_raytracingSystem,
            .targets = &deferredTargets,
            .asyncPrefixTiming = &asyncPrefixTiming,
            .timingTicket = &timingTicket,
            .shadowVisibilityRunsOnCompute = shadowVisibilityRunsOnCompute,
        }
    );
    if(!m_graphicsPrefixTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare post-G-buffer normalization task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_graphicsPrefixTaskGraph,
        m_graphicsPrefixTaskGraphAnalysis,
        topology,
        m_graphicsPrefixTaskGraphQueueAssignments,
        m_graphicsPrefixCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile graphics-prefix task graph"));
        return;
    }
    m_graphicsPrefixRecordedGraph.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixSubmissionTransaction.reset(m_graphicsPrefixCompiledGraph);
    m_graphicsPrefixTaskGraphValid = true;
}


void RendererSystem::buildLaggedLightingHistoryTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    const DeferredFrameTargets& deferredTargets
){
    using namespace __hidden_renderer_task_graph;

    m_laggedLightingHistoryTaskGraphValid = false;
    m_laggedLightingHistoryTask = {};
    m_laggedLightingPresentationCompletion = {};
    m_laggedLightingHistoryTaskGraph.reset();
    m_laggedLightingHistoryTaskGraphAnalysis.reset();
    m_laggedLightingHistoryTaskGraphQueueAssignments.reset();
    m_laggedLightingHistoryCompiledGraph.reset();
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    if(!schedule.capturesLaggedLightingHistory())
        return;

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    if(!dedicatedAsyncCompute)
        return;

    const DeferredLaggedLightingHistoryResources& history = deferredTargets.laggedLightingHistory;
    if(!history.valid())
        return;
    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_laggedLightingHistoryTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        deferredTargets.shadowVisibility,
        Name("render.lagged_history_copy.shadow_visibility"),
        "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.lagged_history_copy.caustic_irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        deferredTargets.surfelIrradiance,
        Name("render.lagged_history_copy.surfel_irradiance"),
        "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId historyShadowVisibility = importTexture(
        history.shadowVisibility,
        Name("render.lagged_history_copy.history_shadow_visibility"),
        "History Shadow Visibility"
    );
    const Core::GpuGraphResourceId historyCausticIrradiance = importTexture(
        history.causticIrradiance,
        Name("render.lagged_history_copy.history_caustic_irradiance"),
        "History Caustic Irradiance"
    );
    const Core::GpuGraphResourceId historySurfelIrradiance = importTexture(
        history.surfelIrradiance,
        Name("render.lagged_history_copy.history_surfel_irradiance"),
        "History Surfel Irradiance"
    );
    if(
        !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !historyShadowVisibility.valid()
        || !historyCausticIrradiance.valid()
        || !historySurfelIrradiance.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history-copy resources"));
        return;
    }

    Core::GpuExternalCompletionDesc presentationCompletionDesc;
    presentationCompletionDesc
        .setIdentity(Name("render.lagged_history_copy.presentation_complete"))
        .setMarkerLabel("Final Presentation Complete")
    ;
    m_laggedLightingPresentationCompletion = m_laggedLightingHistoryTaskGraph.importExternalCompletion(
        presentationCompletionDesc
    );
    if(!m_laggedLightingPresentationCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import final presentation completion for history copy"));
        return;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(shadowVisibility, Core::ResourceStates::CopySource),
        ReadUse(causticIrradiance, Core::ResourceStates::CopySource),
        ReadUse(surfelIrradiance, Core::ResourceStates::CopySource),
        WriteUse(historyShadowVisibility, Core::ResourceStates::CopyDest),
        WriteUse(historyCausticIrradiance, Core::ResourceStates::CopyDest),
        WriteUse(historySurfelIrradiance, Core::ResourceStates::CopyDest),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.lagged_history_copy"))
        .setMarkerLabel("Lagged Lighting History Copy")
        .setQueue(TransferQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_laggedLightingPresentationCompletion, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_laggedLightingHistoryTask = m_laggedLightingHistoryTaskGraph.addTask<LaggedLightingHistoryCopyTask>(
        desc,
        LaggedLightingHistoryCopyTask::Payload{
            .sourceShadowVisibility = deferredTargets.shadowVisibility,
            .sourceCausticIrradiance = deferredTargets.causticIrradiance,
            .sourceSurfelIrradiance = deferredTargets.surfelIrradiance,
            .destinationShadowVisibility = history.shadowVisibility,
            .destinationCausticIrradiance = history.causticIrradiance,
            .destinationSurfelIrradiance = history.surfelIrradiance,
            .acceptedHistoryToken = &m_laggedLightingHistorySubmissionToken,
        }
    );
    if(!m_laggedLightingHistoryTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare lagged-lighting history-copy task"));
        return;
    }

    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_laggedLightingHistoryTaskGraph,
        m_laggedLightingHistoryTaskGraphAnalysis,
        topology,
        m_laggedLightingHistoryTaskGraphQueueAssignments,
        m_laggedLightingHistoryCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile lagged-lighting history-copy graph"));
        return;
    }
    m_laggedLightingHistoryRecordedGraph.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistorySubmissionTransaction.reset(m_laggedLightingHistoryCompiledGraph);
    m_laggedLightingHistoryTaskGraphValid = true;
}


void RendererSystem::buildShadowVisibilityTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_shadowVisibilityPrefixCompletion = {};
    m_shadowVisibilityTaskGraph.reset();
    m_shadowVisibilityTaskGraphAnalysis.reset();
    m_shadowVisibilityTaskGraphQueueAssignments.reset();
    m_shadowVisibilityCompiledGraph.reset();
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_shadowVisibilityTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_shadowVisibilityTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.shadow_visibility.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.shadow_visibility.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.shadow_visibility.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        deferredTargets.shadowVisibility,
        Name("render.shadow_visibility.output"),
        "Shadow Visibility"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.shadow_visibility.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_shadowVisibilityTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.shadow_visibility.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !normal.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-visibility graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.shadow_visibility.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_shadowVisibilityPrefixCompletion = m_shadowVisibilityTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_shadowVisibilityPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for shadow visibility"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(40u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    // Hybrid transparent shadows multiply onto the opaque result, so this remains a read/write declaration even
    // when the hardware-only path overwrites it.
    resourceUses.push_back(ReadWriteUse(shadowVisibility, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadWriteTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        if(!texture)
            return true;
        const Core::GpuGraphResourceId resource = importTexture(texture, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, Core::ResourceStates::UnorderedAccess));
        return true;
    };
    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalReadWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadWriteUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        appendOptionalReadWriteTexture(
            deferredTargets.shadowCoarseTransmittance,
            Name("render.shadow_visibility.coarse_transmittance"),
            "Shadow Coarse Transmittance"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfA,
            Name("render.shadow_visibility.soft_half_a"),
            "Shadow Soft Half A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftHalfB,
            Name("render.shadow_visibility.soft_half_b"),
            "Shadow Soft Half B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometry,
            Name("render.shadow_visibility.soft_geometry"),
            "Shadow Soft Geometry"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowSoftGeometryPrev,
            Name("render.shadow_visibility.soft_geometry_previous"),
            "Previous Shadow Soft Geometry"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistA,
            Name("render.shadow_visibility.history_a"),
            "Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowHistB,
            Name("render.shadow_visibility.history_b"),
            "Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsA,
            Name("render.shadow_visibility.moments_a"),
            "Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.shadowMomentsB,
            Name("render.shadow_visibility.moments_b"),
            "Shadow Moments B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentSoftHalf,
            Name("render.shadow_visibility.transparent_soft_half"),
            "Transparent Shadow Soft Half"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistA,
            Name("render.shadow_visibility.transparent_history_a"),
            "Transparent Shadow History A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentHistB,
            Name("render.shadow_visibility.transparent_history_b"),
            "Transparent Shadow History B"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsA,
            Name("render.shadow_visibility.transparent_moments_a"),
            "Transparent Shadow Moments A"
        )
        && appendOptionalReadWriteTexture(
            deferredTargets.transparentMomentsB,
            Name("render.shadow_visibility.transparent_moments_b"),
            "Transparent Shadow Moments B"
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.shadow_visibility.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.shadow_visibility.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneBvhNodeBuffer,
            Name("render.shadow_visibility.scene_bvh_nodes"),
            "Scene BVH Nodes",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_sceneInstanceBuffer,
            Name("render.shadow_visibility.scene_instances"),
            "Scene Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.shadow_visibility.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.shadow_visibility.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.shadow_visibility.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.shadow_visibility.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsBuffer,
            Name("render.shadow_visibility.edge_stats"),
            "Shadow Edge Statistics",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_swShadowEdgeStatsReadback,
            Name("render.shadow_visibility.edge_stats_readback"),
            "Shadow Edge Statistics Readback",
            Core::ResourceStates::CopyDest
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeCounterBuffer,
            Name("render.shadow_visibility.edge_counter"),
            "Shadow Edge Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowEdgeListBuffer,
            Name("render.shadow_visibility.edge_list"),
            "Shadow Edge List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalReadWriteBuffer(
            m_rayTracingState.m_swShadowIndirectArgsBuffer,
            Name("render.shadow_visibility.indirect_args"),
            "Shadow Indirect Arguments",
            Core::ResourceStates::UnorderedAccess
        )
    ;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_shadowVisibilityTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.shadow_visibility.tlas"), "Scene TLAS")
        );
        const Core::GpuGraphResourceId tlasBackingBuffer = importBuffer(
            m_rayTracingState.m_tlas->getBackingBufferHandle(),
            Name("render.shadow_visibility.tlas_backing"),
            "Scene TLAS Backing"
        );
        optionalResourcesImported = optionalResourcesImported && tlas.valid() && tlasBackingBuffer.valid();
        if(tlas.valid() && tlasBackingBuffer.valid()){
            resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            // setAccelStructState lowers through this buffer; importing it explicitly keeps declaration-driven
            // external state seeding and ownership handoff complete.
            resourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a shadow-visibility dynamic resource"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.shadow_visibility"))
        .setMarkerLabel("Shadow Visibility")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_shadowVisibilityPrefixCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_shadowVisibilityTask = m_raytracingSystem.declareShadowVisibilityTask(
        m_shadowVisibilityTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        hardwareShadowSupported,
        timingTicket
    );
    if(!m_shadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow-visibility graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_shadowVisibilityTaskGraph,
        m_shadowVisibilityTaskGraphAnalysis,
        topology,
        m_shadowVisibilityTaskGraphQueueAssignments,
        m_shadowVisibilityCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile shadow-visibility task graph"));
        return;
    }
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilityTaskGraphValid = true;
}


void RendererSystem::buildHardwareCausticsTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const bool waitsForLaggedLightingHistory,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_hardwareCausticsTaskGraphValid = false;
    m_hardwareCausticsTask = {};
    m_hardwareCausticsPrefixCompletion = {};
    m_hardwareCausticsLaggedHistoryCompletion = {};
    m_hardwareCausticsTaskGraph.reset();
    m_hardwareCausticsTaskGraphAnalysis.reset();
    m_hardwareCausticsTaskGraphQueueAssignments.reset();
    m_hardwareCausticsCompiledGraph.reset();
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);

    if(!input.hardwareCaustics || !deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_hardwareCausticsTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_hardwareCausticsTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.hardware_caustics.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.hardware_caustics.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId causticAccumulator = importTexture(
        deferredTargets.causticAccumulator,
        Name("render.hardware_caustics.accumulator"),
        "Caustic Accumulator"
    );
    const Core::GpuGraphResourceId causticHistory = importTexture(
        deferredTargets.causticHistory,
        Name("render.hardware_caustics.history"),
        "Caustic History"
    );
    const Core::GpuGraphResourceId causticResolveHalf = importTexture(
        deferredTargets.causticResolveHalf,
        Name("render.hardware_caustics.resolve_half"),
        "Caustic Resolve Half"
    );
    const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
        deferredTargets.causticResolveGeometry,
        Name("render.hardware_caustics.resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.hardware_caustics.irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.hardware_caustics.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_hardwareCausticsTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.hardware_caustics.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !depth.valid()
        || !causticAccumulator.valid()
        || !causticHistory.valid()
        || !causticResolveHalf.valid()
        || !causticResolveGeometry.valid()
        || !causticIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.hardware_caustics.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_hardwareCausticsPrefixCompletion = m_hardwareCausticsTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_hardwareCausticsPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for hardware caustics"));
        return;
    }
    if(waitsForLaggedLightingHistory){
        Core::GpuExternalCompletionDesc laggedHistoryCompletionDesc;
        laggedHistoryCompletionDesc
            .setIdentity(Name("render.hardware_caustics.lagged_history_complete"))
            .setMarkerLabel("Lagged Lighting History Complete")
        ;
        m_hardwareCausticsLaggedHistoryCompletion = m_hardwareCausticsTaskGraph.importExternalCompletion(
            laggedHistoryCompletionDesc
        );
        if(!m_hardwareCausticsLaggedHistoryCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-history completion for hardware caustics"));
            return;
        }
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    resourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.hardware_caustics.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.hardware_caustics.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_drawState.m_meshViewBuffer,
            Name("render.hardware_caustics.mesh_view"),
            "Mesh View",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceMaterialBuffer,
            Name("render.hardware_caustics.instance_material"),
            "Shadow Instance Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowMaterialTypedBuffer,
            Name("render.hardware_caustics.material_typed"),
            "Shadow Typed Materials",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_shadowInstanceBuffer,
            Name("render.hardware_caustics.shadow_instances"),
            "Shadow Instances",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.hardware_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.hardware_caustics.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
    ;
    if(m_rayTracingState.m_tlas){
        const Core::GpuGraphResourceId tlas = m_hardwareCausticsTaskGraph.importAccelStruct(
            m_rayTracingState.m_tlas,
            AccelStructResourceDesc(Name("render.hardware_caustics.tlas"), "Scene TLAS")
        );
        const Core::GpuGraphResourceId tlasBackingBuffer = importBuffer(
            m_rayTracingState.m_tlas->getBackingBufferHandle(),
            Name("render.hardware_caustics.tlas_backing"),
            "Scene TLAS Backing"
        );
        optionalResourcesImported = optionalResourcesImported && tlas.valid() && tlasBackingBuffer.valid();
        if(tlas.valid() && tlasBackingBuffer.valid()){
            resourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
            resourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
        }
    }
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
        return;
    }

    Core::GpuExternalCompletionId externalDependencies[2] = {
        m_hardwareCausticsPrefixCompletion,
        m_hardwareCausticsLaggedHistoryCompletion,
    };
    const usize externalDependencyCount = waitsForLaggedLightingHistory ? LengthOf(externalDependencies) : 1u;
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.hardware_caustics"))
        .setMarkerLabel("Hardware Caustics")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, externalDependencyCount)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_hardwareCausticsTask = m_raytracingSystem.declareHardwareCausticsTask(
        m_hardwareCausticsTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        timingTicket
    );
    if(!m_hardwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_hardwareCausticsTaskGraph,
        m_hardwareCausticsTaskGraphAnalysis,
        topology,
        m_hardwareCausticsTaskGraphQueueAssignments,
        m_hardwareCausticsCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile hardware-caustics task graph"));
        return;
    }
    m_hardwareCausticsRecordedGraph.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsSubmissionTransaction.reset(m_hardwareCausticsCompiledGraph);
    m_hardwareCausticsTaskGraphValid = true;
}


void RendererSystem::buildSoftwareCausticsTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_softwareCausticsTaskGraphValid = false;
    m_softwareCausticsTask = {};
    m_softwareCausticsShadowVisibilityCompletion = {};
    m_softwareCausticsTaskGraph.reset();
    m_softwareCausticsTaskGraphAnalysis.reset();
    m_softwareCausticsTaskGraphQueueAssignments.reset();
    m_softwareCausticsCompiledGraph.reset();
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);

    // The software producer owns the complete non-hardware path on both a dedicated Compute queue and its
    // compiler-selected Graphics fallback.
    if(input.hardwareCaustics || !deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_softwareCausticsTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_softwareCausticsTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.software_caustics.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.software_caustics.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId causticAccumulator = importTexture(
        deferredTargets.causticAccumulator,
        Name("render.software_caustics.accumulator"),
        "Caustic Accumulator"
    );
    const Core::GpuGraphResourceId causticHistory = importTexture(
        deferredTargets.causticHistory,
        Name("render.software_caustics.history"),
        "Caustic History"
    );
    const Core::GpuGraphResourceId causticResolveHalf = importTexture(
        deferredTargets.causticResolveHalf,
        Name("render.software_caustics.resolve_half"),
        "Caustic Resolve Half"
    );
    const Core::GpuGraphResourceId causticResolveGeometry = importTexture(
        deferredTargets.causticResolveGeometry,
        Name("render.software_caustics.resolve_geometry"),
        "Caustic Resolve Geometry"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        deferredTargets.causticIrradiance,
        Name("render.software_caustics.irradiance"),
        "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.software_caustics.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_softwareCausticsTaskGraph.importHazardDomain(
        HazardDomainDesc(
            Name("render.software_caustics.scene_geometry"),
            "Software BVH Scene Geometry"
        )
    );
    if(
        !worldPosition.valid()
        || !depth.valid()
        || !causticAccumulator.valid()
        || !causticHistory.valid()
        || !causticResolveHalf.valid()
        || !causticResolveGeometry.valid()
        || !causticIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import software-caustics graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc shadowVisibilityCompletionDesc;
    shadowVisibilityCompletionDesc
        .setIdentity(Name("render.software_caustics.shadow_visibility_complete"))
        .setMarkerLabel("Shadow Visibility Complete")
    ;
    m_softwareCausticsShadowVisibilityCompletion = m_softwareCausticsTaskGraph.importExternalCompletion(
        shadowVisibilityCompletionDesc
    );
    if(!m_softwareCausticsShadowVisibilityCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-visibility completion for software caustics"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(depth, Core::ResourceStates::DepthRead));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(causticIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.software_caustics.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.software_caustics.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_causticEmissionTargetBuffer,
            Name("render.software_caustics.emission_targets"),
            "Caustic Emission Targets",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_drawState.m_meshViewBuffer,
            Name("render.software_caustics.mesh_view"),
            "Mesh View",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.software_caustics.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a software-caustics dynamic resource"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_softwareCausticsShadowVisibilityCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_softwareCausticsTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_softwareCausticsTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        timingTicket
    );
    if(!m_softwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software-caustics graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_softwareCausticsTaskGraph,
        m_softwareCausticsTaskGraphAnalysis,
        topology,
        m_softwareCausticsTaskGraphQueueAssignments,
        m_softwareCausticsCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile software-caustics task graph"));
        return;
    }
    m_softwareCausticsRecordedGraph.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsSubmissionTransaction.reset(m_softwareCausticsCompiledGraph);
    m_softwareCausticsTaskGraphValid = true;
}


void RendererSystem::buildSurfelGiTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_surfelGiTaskGraphValid = false;
    m_surfelGiTask = {};
    m_surfelGiEffectsCompletion = {};
    m_surfelGiTaskGraph.reset();
    m_surfelGiTaskGraphAnalysis.reset();
    m_surfelGiTaskGraphQueueAssignments.reset();
    m_surfelGiCompiledGraph.reset();
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.valid())
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_surfelGiTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_surfelGiTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.surfel_gi.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.surfel_gi.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId surfelIrradianceHalf = importTexture(
        deferredTargets.surfelIrradianceHalf,
        Name("render.surfel_gi.irradiance_half"),
        "Surfel Irradiance Half"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        deferredTargets.surfelIrradiance,
        Name("render.surfel_gi.irradiance"),
        "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.surfel_gi.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_surfelGiTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(
        !worldPosition.valid()
        || !normal.valid()
        || !surfelIrradianceHalf.valid()
        || !surfelIrradiance.valid()
        || !bindlessSlots.valid()
        || !sceneGeometryDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel-GI graph resources"));
        return;
    }

    const bool waitsForSoftwareCaustics = !input.hardwareCaustics;
    Core::GpuExternalCompletionDesc effectsCompletionDesc;
    effectsCompletionDesc
        .setIdentity(
            waitsForSoftwareCaustics
                ? Name("render.surfel_gi.software_caustics_complete")
                : Name("render.surfel_gi.shadow_visibility_complete")
        )
        .setMarkerLabel(waitsForSoftwareCaustics ? "Software Caustics Complete" : "Shadow Visibility Complete")
    ;
    m_surfelGiEffectsCompletion = m_surfelGiTaskGraph.importExternalCompletion(effectsCompletionDesc);
    if(!m_surfelGiEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import effects completion for surfel GI"));
        return;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));

    const auto appendOptionalReadBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(ReadUse(resource, state));
        return true;
    };
    const auto appendOptionalWriteBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label, const Core::ResourceStates::Mask state){
        if(!buffer)
            return true;
        const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
        if(!resource.valid())
            return false;
        resourceUses.push_back(WriteUse(resource, state));
        return true;
    };
    const bool optionalResourcesImported =
        appendOptionalReadBuffer(
            m_deferredState.m_sceneShadingBuffer,
            Name("render.surfel_gi.scene_shading"),
            "Scene Shading",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_deferredState.m_lightBuffer,
            Name("render.surfel_gi.lights"),
            "Lights",
            Core::ResourceStates::ShaderResource
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalReadBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.surfel_gi.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolBuffer,
            Name("render.surfel_gi.pool"),
            "Surfel Pool",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadBuffer,
            Name("render.surfel_gi.cell_heads"),
            "Surfel Cell Heads",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterBuffer,
            Name("render.surfel_gi.counter"),
            "Surfel Counter",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
            Name("render.surfel_gi.trace_args"),
            "Surfel Trace Arguments",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelFreeListBuffer,
            Name("render.surfel_gi.free_list"),
            "Surfel Free List",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelPoolSnapshotBuffer,
            Name("render.surfel_gi.pool_snapshot"),
            "Surfel Pool Snapshot",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
            Name("render.surfel_gi.cell_head_snapshot"),
            "Surfel Cell Head Snapshot",
            Core::ResourceStates::UnorderedAccess
        )
        && appendOptionalWriteBuffer(
            m_rayTracingState.m_surfelCounterReadback,
            Name("render.surfel_gi.counter_readback"),
            "Surfel Counter Readback",
            Core::ResourceStates::CopyDest
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a surfel-GI dynamic resource domain"));
        return;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.surfel_gi"))
        .setMarkerLabel("Surfel GI")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_surfelGiEffectsCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_surfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_surfelGiTaskGraph,
        desc,
        deferredTargets,
        timingTicket
    );
    if(!m_surfelGiTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare surfel-GI graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_surfelGiTaskGraph,
        m_surfelGiTaskGraphAnalysis,
        topology,
        m_surfelGiTaskGraphQueueAssignments,
        m_surfelGiCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile surfel-GI task graph"));
        return;
    }
    m_surfelGiRecordedGraph.reset(m_surfelGiCompiledGraph);
    m_surfelGiSubmissionTransaction.reset(m_surfelGiCompiledGraph);
    m_surfelGiTaskGraphValid = true;
}


void RendererSystem::buildAvboitTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool clearTargets,
    const bool hasTransparentRenderers,
    Core::GpuTimingSubmissionTicket& preTimingTicket,
    Core::GpuTimingSubmissionTicket& depthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& extinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& integrationTimingTicket,
    Core::GpuTimingSubmissionTicket& accumulationTimingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_avboitTaskGraphValid = false;
    m_avboitPreTask = {};
    m_avboitDepthWarpTask = {};
    m_avboitExtinctionTask = {};
    m_avboitIntegrationTask = {};
    m_avboitAccumulationTask = {};
    m_avboitPrefixCompletion = {};
    m_avboitTaskGraph.reset();
    m_avboitTaskGraphAnalysis.reset();
    m_avboitTaskGraphQueueAssignments.reset();
    m_avboitCompiledGraph.reset();
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    const bool splitStages = schedule.usesAsyncAvboit() && dedicatedAsyncCompute;
    if(!splitStages){
        depthWarpTimingTicket.discard();
        extinctionTimingTicket.discard();
        integrationTimingTicket.discard();
        accumulationTimingTicket.discard();
    }
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || hasTransparentRenderers != input.hasTransparentRenderers
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_avboitTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_avboitTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.avboit.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.avboit.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.avboit.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.avboit.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId lowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId accumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId accumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId transmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    const Core::GpuGraphResourceId coverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    const Core::GpuGraphResourceId depthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Core::GpuGraphResourceId control = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    const Core::GpuGraphResourceId extinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId extinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        deferredTargets.bindless.slotsBuffer,
        Name("render.avboit.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId materialDomain = m_avboitTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    const Core::GpuGraphResourceId csgDomain = m_avboitTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !lowRaster.valid()
        || !accumColor.valid()
        || !accumExtinction.valid()
        || !transmittance.valid()
        || !coverage.valid()
        || !depthWarp.valid()
        || !control.valid()
        || !extinction.valid()
        || !extinctionOverflow.valid()
        || !bindlessSlots.valid()
        || !materialDomain.valid()
        || !csgDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import AVBOIT graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc prefixCompletionDesc;
    prefixCompletionDesc
        .setIdentity(Name("render.avboit.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_avboitPrefixCompletion = m_avboitTaskGraph.importExternalCompletion(prefixCompletionDesc);
    if(!m_avboitPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for AVBOIT"));
        return;
    }

    const Core::GpuTaskResourceUse preResourceUses[] = {
        ReadUse(albedo),
        ReadUse(normal),
        ReadUse(worldPosition),
        ReadUse(depth),
        ReadWriteUse(lowRaster, Core::ResourceStates::RenderTarget),
        ReadWriteUse(accumColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(accumExtinction, Core::ResourceStates::RenderTarget),
        ReadWriteUse(transmittance, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(coverage, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(depthWarp, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(control, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(extinction, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(extinctionOverflow, Core::ResourceStates::UnorderedAccess),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        ReadUse(materialDomain),
        // Transparent CSG interval construction mutates its backing domain before occupancy consumes it.
        ReadWriteUse(csgDomain, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint graphicsScheduling;
    graphicsScheduling.cost = Core::GpuTaskCostHint::Large;
    graphicsScheduling.forceSubmissionBoundary = true;
    graphicsScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc preDesc;
    preDesc
        .setIdentity(Name("render.avboit.pre"))
        .setMarkerLabel(splitStages ? "AVBOIT Pre" : "AVBOIT")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(graphicsScheduling)
        .setExternalDependencies(&m_avboitPrefixCompletion, 1u)
        .setResourceUses(preResourceUses, LengthOf(preResourceUses))
    ;
    m_avboitPreTask = m_avboitTaskGraph.addTask<AvboitPreGraphTask>(
        preDesc,
        AvboitPreGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .timingTicket = &preTimingTicket,
            .clearTargets = clearTargets,
            .hasTransparentRenderers = hasTransparentRenderers,
            .splitStages = splitStages,
        }
    );
    if(!m_avboitPreTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT pre graph task"));
        return;
    }

    if(splitStages){
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(coverage),
            ReadWriteUse(depthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(control, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        Core::GpuTaskSchedulingHint computeScheduling;
        computeScheduling.cost = Core::GpuTaskCostHint::Medium;
        computeScheduling.forceSubmissionBoundary = true;
        computeScheduling.allowPacketMerge = false;
        const Core::GpuTaskId preDependency[] = { m_avboitPreTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(ComputeQueueRequest())
            .setScheduling(computeScheduling)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_avboitDepthWarpTask = m_avboitTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &depthWarpTimingTicket,
            }
        );
        if(!m_avboitDepthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT depth-warp graph task"));
            return;
        }

        const Core::GpuTaskResourceUse extinctionResourceUses[] = {
            // The low-resolution framebuffer is rebound for the no-color-write extinction raster stage.
            ReadUse(lowRaster, Core::ResourceStates::RenderTarget),
            ReadUse(depthWarp),
            ReadUse(control),
            ReadWriteUse(extinction, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(extinctionOverflow, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(materialDomain),
            ReadUse(csgDomain),
        };
        const Core::GpuTaskId depthWarpDependency[] = { m_avboitDepthWarpTask };
        Core::GpuTaskDesc extinctionDesc;
        extinctionDesc
            .setIdentity(Name("render.avboit.extinction"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(graphicsScheduling)
            .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
            .setResourceUses(extinctionResourceUses, LengthOf(extinctionResourceUses))
        ;
        m_avboitExtinctionTask = m_avboitTaskGraph.addTask<AvboitExtinctionGraphTask>(
            extinctionDesc,
            AvboitExtinctionGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .csgFrameState = &csgFrameState,
                .timingTicket = &extinctionTimingTicket,
            }
        );
        if(!m_avboitExtinctionTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT extinction graph task"));
            return;
        }

        const Core::GpuTaskResourceUse integrationResourceUses[] = {
            ReadUse(extinction),
            ReadUse(control),
            ReadUse(extinctionOverflow),
            ReadWriteUse(transmittance, Core::ResourceStates::UnorderedAccess),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId extinctionDependency[] = { m_avboitExtinctionTask };
        Core::GpuTaskDesc integrationDesc;
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(ComputeQueueRequest())
            .setScheduling(computeScheduling)
            .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
            .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
        ;
        m_avboitIntegrationTask = m_avboitTaskGraph.addTask<AvboitIntegrationGraphTask>(
            integrationDesc,
            AvboitIntegrationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &integrationTimingTicket,
            }
        );
        if(!m_avboitIntegrationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT integration graph task"));
            return;
        }

        const Core::GpuTaskResourceUse accumulationResourceUses[] = {
            ReadUse(depth),
            ReadUse(transmittance),
            ReadUse(depthWarp),
            ReadUse(control),
            ReadWriteUse(accumColor, Core::ResourceStates::RenderTarget),
            ReadWriteUse(accumExtinction, Core::ResourceStates::RenderTarget),
            ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(materialDomain),
            ReadUse(csgDomain),
        };
        const Core::GpuTaskId integrationDependency[] = { m_avboitIntegrationTask };
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(graphicsScheduling)
            .setDependencies(integrationDependency, LengthOf(integrationDependency))
            .setResourceUses(accumulationResourceUses, LengthOf(accumulationResourceUses))
        ;
        m_avboitAccumulationTask = m_avboitTaskGraph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            AvboitAccumulationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets,
                .csgFrameState = &csgFrameState,
                .timingTicket = &accumulationTimingTicket,
            }
        );
        if(!m_avboitAccumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare AVBOIT accumulation graph task"));
            return;
        }
    }

    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_avboitTaskGraph,
        m_avboitTaskGraphAnalysis,
        topology,
        m_avboitTaskGraphQueueAssignments,
        m_avboitCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile AVBOIT task graph"));
        return;
    }
    m_avboitRecordedGraph.reset(m_avboitCompiledGraph);
    m_avboitSubmissionTransaction.reset(m_avboitCompiledGraph);
    m_avboitTaskGraphValid = true;
}


void RendererSystem::buildDeferredLightingTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredLightingTaskGraphValid = false;
    m_deferredLightingTask = {};
    m_deferredLightingAvboitCompletion = {};
    m_deferredLightingSurfelGiCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    const bool useLaggedLightingHistory = schedule.usesLaggedLightingHistory();
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
        || (useLaggedLightingHistory && (!history || !history->valid()))
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId albedo = importTexture(
        deferredTargets.albedo,
        Name("render.deferred_lighting.albedo"),
        "G-Buffer Albedo"
    );
    const Core::GpuGraphResourceId normal = importTexture(
        deferredTargets.normal,
        Name("render.deferred_lighting.normal"),
        "G-Buffer Normal"
    );
    const Core::GpuGraphResourceId worldPosition = importTexture(
        deferredTargets.worldPosition,
        Name("render.deferred_lighting.world_position"),
        "G-Buffer World Position"
    );
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.deferred_lighting.depth"),
        "G-Buffer Depth"
    );
    const Core::GpuGraphResourceId shadowVisibility = importTexture(
        history ? history->shadowVisibility : deferredTargets.shadowVisibility,
        Name("render.deferred_lighting.shadow_visibility"),
        history ? "Lagged Shadow Visibility" : "Shadow Visibility"
    );
    const Core::GpuGraphResourceId causticIrradiance = importTexture(
        history ? history->causticIrradiance : deferredTargets.causticIrradiance,
        Name("render.deferred_lighting.caustic_irradiance"),
        history ? "Lagged Caustic Irradiance" : "Caustic Irradiance"
    );
    const Core::GpuGraphResourceId surfelIrradiance = importTexture(
        history ? history->surfelIrradiance : deferredTargets.surfelIrradiance,
        Name("render.deferred_lighting.surfel_irradiance"),
        history ? "Lagged Surfel Irradiance" : "Surfel Irradiance"
    );
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_lighting.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId sceneShading = importBuffer(
        m_deferredState.m_sceneShadingBuffer,
        Name("render.deferred_lighting.scene_shading"),
        "Scene Shading"
    );
    const Core::GpuGraphResourceId lights = importBuffer(
        m_deferredState.m_lightBuffer,
        Name("render.deferred_lighting.lights"),
        "Lights"
    );
    const Core::GpuGraphResourceId bindlessSlots = importBuffer(
        history ? history->slotsBuffer : deferredTargets.bindless.slotsBuffer,
        Name("render.deferred_lighting.bindless_slots"),
        history ? "Lagged Deferred Bindless Slots" : "Deferred Bindless Slots"
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !bindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc avboitCompletionDesc;
    avboitCompletionDesc
        .setIdentity(Name("render.deferred_lighting.avboit_complete"))
        .setMarkerLabel("AVBOIT Complete")
    ;
    m_deferredLightingAvboitCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        avboitCompletionDesc
    );
    if(!m_deferredLightingAvboitCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import AVBOIT completion for deferred lighting"));
        return;
    }

    Core::GpuExternalCompletionDesc dependentEffectsCompletionDesc;
    dependentEffectsCompletionDesc
        .setIdentity(
            useLaggedLightingHistory
                ? Name("render.deferred_lighting.lagged_history_complete")
                : Name("render.deferred_lighting.surfel_gi_complete")
        )
        .setMarkerLabel(useLaggedLightingHistory ? "Lagged Lighting History Complete" : "Surfel GI Complete")
    ;
    Core::GpuExternalCompletionId dependentEffectsCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        dependentEffectsCompletionDesc
    );
    if(!dependentEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting dependent completion"));
        return;
    }
    if(useLaggedLightingHistory)
        m_deferredLightingHistoryCompletion = dependentEffectsCompletion;
    else
        m_deferredLightingSurfelGiCompletion = dependentEffectsCompletion;

    const Core::GpuExternalCompletionId externalDependencies[] = {
        m_deferredLightingAvboitCompletion,
        dependentEffectsCompletion,
    };
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadTextureUse(albedo, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(normal, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(worldPosition, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(depth, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer),
        ReadUse(lights),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteTextureUse(opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.avoidQueueCrossing = useLaggedLightingHistory;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, LengthOf(externalDependencies))
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredLightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        timingTicket
    );
    if(!m_deferredLightingTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-lighting task graph"));
        return;
    }
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingTaskGraphValid = true;
}


void RendererSystem::buildDeferredCompositeTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredCompositeTaskGraphValid = false;
    m_deferredCompositeTask = {};
    m_deferredCompositeLightingCompletion = {};
    m_deferredCompositeTaskGraph.reset();
    m_deferredCompositeTaskGraphAnalysis.reset();
    m_deferredCompositeTaskGraphQueueAssignments.reset();
    m_deferredCompositeCompiledGraph.reset();
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);

    if(!deferredTargets.valid() || !deferredTargets.bindless.slotsBuffer)
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredCompositeTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId opaqueColor = importTexture(
        deferredTargets.opaqueColor,
        Name("render.deferred_composite.opaque_color"),
        "Opaque Color"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.deferred_composite.avboit_accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId avboitAccumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.deferred_composite.avboit_accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId bindlessSlots = m_deferredCompositeTaskGraph.importBuffer(
        deferredTargets.bindless.slotsBuffer,
        BufferResourceDesc(Name("render.deferred_composite.bindless_slots"), "Deferred Bindless Slots")
    );
    if(
        !opaqueColor.valid()
        || !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !compositeColor.valid()
        || !bindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc lightingCompletionDesc;
    lightingCompletionDesc
        .setIdentity(Name("render.deferred_composite.deferred_lighting_complete"))
        .setMarkerLabel("Deferred Lighting Complete")
    ;
    m_deferredCompositeLightingCompletion = m_deferredCompositeTaskGraph.importExternalCompletion(
        lightingCompletionDesc
    );
    if(!m_deferredCompositeLightingCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting completion for composite"));
        return;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(opaqueColor),
        ReadUse(avboitAccumColor),
        ReadUse(avboitAccumExtinction),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.avoidQueueCrossing = schedule.usesLaggedLightingHistory();
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(&m_deferredCompositeLightingCompletion, 1u)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredCompositeTask = m_deferredSystem.declareDeferredCompositeTask(
        m_deferredCompositeTaskGraph,
        desc,
        deferredTargets,
        timingTicket
    );
    if(!m_deferredCompositeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredCompositeTaskGraph,
        m_deferredCompositeTaskGraphAnalysis,
        topology,
        m_deferredCompositeTaskGraphQueueAssignments,
        m_deferredCompositeCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-composite task graph"));
        return;
    }
    m_deferredCompositeRecordedGraph.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeSubmissionTransaction.reset(m_deferredCompositeCompiledGraph);
    m_deferredCompositeTaskGraphValid = true;
}


void RendererSystem::buildDeferredPresentTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    Core::Framebuffer* const presentationFramebuffer,
    const bool waitsForSurfelGi,
    const bool shadowVisibilityRunsOnCompute,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredPresentTaskGraphValid = false;
    m_deferredPresentTask = {};
    m_deferredPresentCompositeCompletion = {};
    m_deferredPresentSurfelGiCompletion = {};
    m_deferredPresentTaskGraph.reset();
    m_deferredPresentTaskGraphAnalysis.reset();
    m_deferredPresentTaskGraphQueueAssignments.reset();
    m_deferredPresentCompiledGraph.reset();
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !presentationFramebuffer
        || waitsForSurfelGi != schedule.usesLaggedLightingHistory()
    )
        return;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredPresentTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_present.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId bindlessSlots = m_deferredPresentTaskGraph.importBuffer(
        deferredTargets.bindless.slotsBuffer,
        BufferResourceDesc(Name("render.deferred_present.bindless_slots"), "Deferred Bindless Slots")
    );
    const Core::GpuGraphResourceId backbuffer = m_deferredPresentTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.deferred_present.backbuffer"), "Presentation Back Buffer")
    );
    if(!compositeColor.valid() || !bindlessSlots.valid() || !backbuffer.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc compositeCompletionDesc;
    compositeCompletionDesc
        .setIdentity(Name("render.deferred_present.deferred_composite_complete"))
        .setMarkerLabel("Deferred Composite Complete")
    ;
    m_deferredPresentCompositeCompletion = m_deferredPresentTaskGraph.importExternalCompletion(
        compositeCompletionDesc
    );
    if(!m_deferredPresentCompositeCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite completion for present"));
        return;
    }

    Core::GpuExternalCompletionId externalDependencies[2] = { m_deferredPresentCompositeCompletion };
    usize externalDependencyCount = 1u;
    if(waitsForSurfelGi){
        Core::GpuExternalCompletionDesc surfelGiCompletionDesc;
        surfelGiCompletionDesc
            .setIdentity(Name("render.deferred_present.surfel_gi_complete"))
            .setMarkerLabel("Surfel GI Complete")
        ;
        m_deferredPresentSurfelGiCompletion = m_deferredPresentTaskGraph.importExternalCompletion(
            surfelGiCompletionDesc
        );
        if(!m_deferredPresentSurfelGiCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import surfel-GI completion for present"));
            return;
        }
        externalDependencies[externalDependencyCount++] = m_deferredPresentSurfelGiCompletion;
    }

    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(bindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Medium;
    scheduling.avoidQueueCrossing = waitsForSurfelGi;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(scheduling)
        .setExternalDependencies(externalDependencies, externalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredPresentTask = m_deferredPresentTaskGraph.addTask<DeferredPresentGraphTask>(
        desc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &deferredTargets,
            .presentationFramebuffer = presentationFramebuffer,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &timingTicket,
            .shadowVisibilityRunsOnCompute = shadowVisibilityRunsOnCompute,
        }
    );
    if(!m_deferredPresentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return;
    }

    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const Core::GpuQueueCapability::Mask graphicsQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Graphics)
        | static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuQueueCapability::Mask computeQueueCapabilities = static_cast<Core::GpuQueueCapability::Mask>(
        static_cast<u8>(Core::GpuQueueCapability::Compute)
        | static_cast<u8>(Core::GpuQueueCapability::Transfer)
    );
    const Core::GpuPhysicalQueueInfo queues[] = {
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 0u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Graphics,
            .capabilities = graphicsQueueCapabilities,
            .familyIndex = graphicsFamilyIndex,
            .queueIndex = 0u,
            .dedicated = false,
        },
        Core::GpuPhysicalQueueInfo{
            .id = Core::GpuPhysicalQueueId{ 1u, m_taskGraphDeviceGeneration },
            .queueClass = Core::CommandQueue::Compute,
            .capabilities = computeQueueCapabilities,
            .familyIndex = computeFamilyIndex,
            .queueIndex = 0u,
            .dedicated = true,
        },
    };
    const Core::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = dedicatedAsyncCompute ? LengthOf(queues) : 1u,
    };
    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    const Core::GpuTaskGraphCompiler compiler;
    if(!compiler.compile(
        m_deferredPresentTaskGraph,
        m_deferredPresentTaskGraphAnalysis,
        topology,
        m_deferredPresentTaskGraphQueueAssignments,
        m_deferredPresentCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred-present task graph"));
        return;
    }
    m_deferredPresentRecordedGraph.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentSubmissionTransaction.reset(m_deferredPresentCompiledGraph);
    m_deferredPresentTaskGraphValid = true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

