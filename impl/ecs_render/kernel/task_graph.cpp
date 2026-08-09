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
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    return Core::GpuTaskResourceUse{
        .resource = resource,
        .range = {},
        .requiredState = state,
        .access = Core::GpuTaskResourceAccess::Read,
        .hasIndependentStateSource = hasIndependentStateSource,
    };
}

[[nodiscard]] static Core::GpuTaskResourceUse ReadTextureUse(
    const Core::GpuGraphResourceId resource,
    const Core::TextureSubresourceSet& subresources,
    const Core::ResourceStates::Mask state = Core::ResourceStates::ShaderResource,
    const bool hasIndependentStateSource = false
){
    Core::GpuTaskResourceUse result = ReadUse(resource, state, hasIndependentStateSource);
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


void RendererSystem::buildShadowVisibilityTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const bool hardwareShadowSupported,
    Core::GpuTimingSubmissionTicket& shadowVisibilityTimingTicket,
    Core::GpuTimingSubmissionTicket& softwareCausticsTimingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_shadowVisibilityTaskGraphValid = false;
    m_shadowVisibilityTask = {};
    m_softwareCausticsTask = {};
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
        shadowVisibilityTimingTicket
    );
    if(!m_shadowVisibilityTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare shadow-visibility graph task"));
        return;
    }
    if(!hardwareShadowSupported && !declareSoftwareCausticsTask(
        input,
        deferredTargets,
        shadowVisibilityPrepared,
        m_shadowVisibilityTask,
        softwareCausticsTimingTicket
    ))
        return;
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile shadow-visibility/software-caustics task graph"));
        return;
    }
    m_shadowVisibilityRecordedGraph.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilitySubmissionTransaction.reset(m_shadowVisibilityCompiledGraph);
    m_shadowVisibilityTaskGraphValid = true;
}


bool RendererSystem::declareSoftwareCausticsTask(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const bool shadowVisibilityPrepared,
    const Core::GpuTaskId& shadowVisibilityTask,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_softwareCausticsTask = {};

    // This is the direct successor in the software-effects graph. A distinct Compute family remains an optional
    // compiler assignment; on other devices the same packet routes through Graphics.
    if(
        input.hardwareCaustics
        || !shadowVisibilityTask.valid()
        || !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
    )
        return false;

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
    const Core::GpuGraphResourceId depth = importTexture(
        deferredTargets.depth,
        Name("render.shadow_visibility.depth"),
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
        Name("render.shadow_visibility.bindless_slots"),
        "Deferred Bindless Slots"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_shadowVisibilityTaskGraph.importHazardDomain(
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
        return false;
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
            Name("render.shadow_visibility.material_context_slots"),
            "Ray Trace Material Context Slots",
            Core::ResourceStates::ConstantBuffer
        )
    ;
    if(!optionalResourcesImported){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a software-caustics dynamic resource"));
        return false;
    }

    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    const Core::GpuTaskId dependencies[] = { shadowVisibilityTask };
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.software_caustics"))
        .setMarkerLabel("Software Caustics")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(dependencies, LengthOf(dependencies))
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_softwareCausticsTask = m_raytracingSystem.declareSoftwareCausticsTask(
        m_shadowVisibilityTaskGraph,
        desc,
        deferredTargets,
        shadowVisibilityPrepared,
        timingTicket
    );
    if(!m_softwareCausticsTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare software-caustics graph task"));
        return false;
    }
    return true;
}


bool RendererSystem::declareDeferredSurfelGiTask(
    DeferredFrameTargets& deferredTargets,
    const Core::GpuGraphResourceId worldPosition,
    const Core::GpuGraphResourceId normal,
    const Core::GpuGraphResourceId surfelIrradiance,
    const Core::GpuGraphResourceId currentBindlessSlots,
    const Core::GpuGraphResourceId sceneShading,
    const Core::GpuGraphResourceId lights,
    const Core::GpuGraphResourceId materialContextSlots,
    const Core::GpuExternalCompletionId effectsCompletion,
    Core::GpuTimingSubmissionTicket& timingTicket
){
    using namespace __hidden_renderer_task_graph;

    m_deferredSurfelGiTask = {};
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !worldPosition.valid()
        || !normal.valid()
        || !surfelIrradiance.valid()
        || !currentBindlessSlots.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !effectsCompletion.valid()
    )
        return false;

    const auto importTexture = [&](const Core::TextureHandle& texture, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importTexture(texture, TextureResourceDesc(identity, label));
    };
    const auto importBuffer = [&](const Core::BufferHandle& buffer, const Name& identity, const AStringView label){
        return m_deferredLightingTaskGraph.importBuffer(buffer, BufferResourceDesc(identity, label));
    };
    const Core::GpuGraphResourceId surfelIrradianceHalf = importTexture(
        deferredTargets.surfelIrradianceHalf,
        Name("render.surfel_gi.irradiance_half"),
        "Surfel Irradiance Half"
    );
    const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.surfel_gi.scene_geometry"), "Scene Acceleration and Geometry")
    );
    if(!surfelIrradianceHalf.valid() || !sceneGeometryDomain.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred surfel-GI graph resources"));
        return false;
    }

    Core::Alloc::ScratchArena scratchArena(RendererArenaScope::s_TaskGraphArena);
    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> resourceUses{ scratchArena };
    resourceUses.reserve(20u);
    resourceUses.push_back(ReadUse(worldPosition));
    resourceUses.push_back(ReadUse(normal));
    resourceUses.push_back(ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(WriteUse(surfelIrradianceHalf, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(WriteUse(surfelIrradiance, Core::ResourceStates::UnorderedAccess));
    resourceUses.push_back(ReadUse(sceneShading, Core::ResourceStates::ConstantBuffer));
    resourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource));
    resourceUses.push_back(ReadUse(sceneGeometryDomain));
    if(materialContextSlots.valid())
        resourceUses.push_back(ReadUse(materialContextSlots, Core::ResourceStates::ConstantBuffer));

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
            m_rayTracingState.m_surfelConstants,
            Name("render.surfel_gi.constants"),
            "Surfel Constants",
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
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a deferred surfel-GI dynamic resource domain"));
        return false;
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
        .setExternalDependencies(&effectsCompletion, 1u)
        .setResourceUses(resourceUses.data(), resourceUses.size())
    ;
    m_deferredSurfelGiTask = m_raytracingSystem.declareSurfelGiTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        timingTicket
    );
    if(!m_deferredSurfelGiTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred surfel-GI graph task"));
        return false;
    }
    return true;
}


void RendererSystem::buildDeferredLightingTaskGraph(
    const ECSRenderDetail::GpuTaskGraphFrameScheduleInput& input,
    DeferredFrameTargets& deferredTargets,
    const CsgFrameState& csgFrameState,
    const bool clearAvboitTargets,
    const bool hasTransparentRenderers,
    const bool shadowVisibilityPrepared,
    Core::Framebuffer* const presentationFramebuffer,
    const bool shadowVisibilityRunsOnCompute,
    Core::GpuTimingFrameTransaction& frameTimingTransaction,
    Optional<Core::GpuTimingMeasure>& asyncFinalTiming,
    Core::GpuTimingSubmissionTicket& avboitPreTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitDepthWarpTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitExtinctionTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitIntegrationTimingTicket,
    Core::GpuTimingSubmissionTicket& avboitAccumulationTimingTicket,
    Core::GpuTimingSubmissionTicket& surfelGiTimingTicket,
    Core::GpuTimingSubmissionTicket& hardwareCausticsTimingTicket,
    Core::GpuTimingSubmissionTicket& lightingTimingTicket,
    Core::GpuTimingSubmissionTicket& compositeTimingTicket,
    Core::GpuTimingSubmissionTicket& presentTimingTicket,
    const bool includeLaggedLightingHistoryCapture
){
    using namespace __hidden_renderer_task_graph;

    m_deferredLightingTaskGraphValid = false;
    m_deferredSurfelGiTask = {};
    m_deferredHardwareCausticsTask = {};
    m_deferredAvboitPreTask = {};
    m_deferredAvboitDepthWarpTask = {};
    m_deferredAvboitExtinctionTask = {};
    m_deferredAvboitIntegrationTask = {};
    m_deferredAvboitAccumulationTask = {};
    m_deferredLightingTask = {};
    m_deferredCompositeTask = {};
    m_deferredPresentTask = {};
    m_deferredLaggedLightingHistoryTask = {};
    m_deferredSurfelGiEffectsCompletion = {};
    m_deferredHardwareCausticsPrefixCompletion = {};
    m_deferredLightingPrefixCompletion = {};
    m_deferredLightingHistoryCompletion = {};
    m_deferredLightingTaskGraph.reset();
    m_deferredLightingTaskGraphAnalysis.reset();
    m_deferredLightingTaskGraphQueueAssignments.reset();
    m_deferredLightingCompiledGraph.reset();
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);

    const ECSRenderDetail::GpuTaskGraphFrameSchedule schedule(input);
    const bool useLaggedLightingHistory = schedule.usesLaggedLightingHistory();
    const auto& device = graphics().getDevice();
    const u32 graphicsFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Graphics);
    const u32 computeFamilyIndex = device.getQueueFamilyIndex(Core::CommandQueue::Compute);
    const bool dedicatedAsyncCompute = input.dedicatedAsyncCompute
        && computeFamilyIndex != Limit<u32>::s_Max
        && computeFamilyIndex != graphicsFamilyIndex
    ;
    const bool splitAvboitStages = !useLaggedLightingHistory && schedule.usesAsyncAvboit() && dedicatedAsyncCompute;
    if(!splitAvboitStages){
        avboitDepthWarpTimingTicket.discard();
        avboitExtinctionTimingTicket.discard();
        avboitIntegrationTimingTicket.discard();
        avboitAccumulationTimingTicket.discard();
    }
    const bool declaresHardwareCaustics = input.hardwareCaustics;
    const bool lightingDependsOnHardwareCaustics = declaresHardwareCaustics && !useLaggedLightingHistory;
    const bool capturesLaggedLightingHistory = includeLaggedLightingHistoryCapture
        && schedule.capturesLaggedLightingHistory()
        && dedicatedAsyncCompute
    ;
    const DeferredLaggedLightingHistoryResources* const history = useLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    const DeferredLaggedLightingHistoryResources* const captureHistory = capturesLaggedLightingHistory
        ? &deferredTargets.laggedLightingHistory
        : nullptr
    ;
    if(
        !deferredTargets.valid()
        || !deferredTargets.bindless.valid()
        || !m_deferredState.m_sceneShadingBuffer
        || !m_deferredState.m_lightBuffer
        || !presentationFramebuffer
        || hasTransparentRenderers != input.hasTransparentRenderers
        || (useLaggedLightingHistory && (!history || !history->valid()))
        || (capturesLaggedLightingHistory && (!captureHistory || !captureHistory->valid()))
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
    const Core::GpuGraphResourceId currentSurfelIrradiance = !history
        ? surfelIrradiance
        : importTexture(
            deferredTargets.surfelIrradiance,
            Name("render.deferred_surfel_gi.current_irradiance"),
            "Surfel Irradiance"
        )
    ;
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
    const Core::GpuGraphResourceId currentBindlessSlots =
        !history || deferredTargets.bindless.slotsBuffer.get() == history->slotsBuffer.get()
            ? bindlessSlots
            : importBuffer(
                deferredTargets.bindless.slotsBuffer,
                Name("render.deferred_composite.bindless_slots"),
                "Deferred Bindless Slots"
            )
    ;
    const Core::GpuGraphResourceId materialContextSlots = m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer
        ? importBuffer(
            m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer,
            Name("render.deferred.material_context_slots"),
            "Ray Trace Material Context Slots"
        )
        : Core::GpuGraphResourceId{}
    ;
    const Core::GpuGraphResourceId hardwareCausticIrradiance =
        !declaresHardwareCaustics || !history
            ? causticIrradiance
            : importTexture(
                deferredTargets.causticIrradiance,
                Name("render.hardware_caustics.irradiance"),
                "Caustic Irradiance"
            )
    ;
    // The optional history copy is declared in this graph after Present, but records only after its accepted
    // producer snapshots exist. Active lighting samples the history resources above, so reuse those exact graph
    // identities for copy destinations and import only the current producer images that are otherwise absent.
    Core::GpuGraphResourceId historyCopyShadowVisibility;
    Core::GpuGraphResourceId historyCopyCausticIrradiance;
    Core::GpuGraphResourceId historyCopySurfelIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationShadowVisibility;
    Core::GpuGraphResourceId historyCopyDestinationCausticIrradiance;
    Core::GpuGraphResourceId historyCopyDestinationSurfelIrradiance;
    if(capturesLaggedLightingHistory){
        historyCopyShadowVisibility = history
            ? importTexture(
                deferredTargets.shadowVisibility,
                Name("render.lagged_history_copy.shadow_visibility"),
                "Shadow Visibility"
            )
            : shadowVisibility
        ;
        historyCopyCausticIrradiance = !history
            ? causticIrradiance
            : (declaresHardwareCaustics
                ? hardwareCausticIrradiance
                : importTexture(
                    deferredTargets.causticIrradiance,
                    Name("render.lagged_history_copy.caustic_irradiance"),
                    "Caustic Irradiance"
                )
            )
        ;
        historyCopySurfelIrradiance = history
            ? currentSurfelIrradiance
            : surfelIrradiance
        ;
        historyCopyDestinationShadowVisibility = history
            ? shadowVisibility
            : importTexture(
                captureHistory->shadowVisibility,
                Name("render.lagged_history_copy.history_shadow_visibility"),
                "History Shadow Visibility"
            )
        ;
        historyCopyDestinationCausticIrradiance = history
            ? causticIrradiance
            : importTexture(
                captureHistory->causticIrradiance,
                Name("render.lagged_history_copy.history_caustic_irradiance"),
                "History Caustic Irradiance"
            )
        ;
        historyCopyDestinationSurfelIrradiance = history
            ? surfelIrradiance
            : importTexture(
                captureHistory->surfelIrradiance,
                Name("render.lagged_history_copy.history_surfel_irradiance"),
                "History Surfel Irradiance"
            )
        ;
    }
    // AVBOIT shares the deferred graph's G-buffer and current bindless imports. Its private targets remain
    // distinct resources, while the compiler owns every producer/consumer state seed through Lighting and
    // Composite on both the live and active-lagged routes.
    const Core::GpuGraphResourceId avboitLowRaster = importTexture(
        deferredTargets.avboit.lowRasterTarget,
        Name("render.avboit.low_raster"),
        "AVBOIT Low Raster"
    );
    const Core::GpuGraphResourceId avboitAccumColor = importTexture(
        deferredTargets.avboit.accumColor,
        Name("render.avboit.accum_color"),
        "AVBOIT Accumulated Color"
    );
    const Core::GpuGraphResourceId avboitAccumExtinction = importTexture(
        deferredTargets.avboit.accumExtinction,
        Name("render.avboit.accum_extinction"),
        "AVBOIT Accumulated Extinction"
    );
    const Core::GpuGraphResourceId avboitTransmittance = importTexture(
        deferredTargets.avboit.transmittanceTexture,
        Name("render.avboit.transmittance"),
        "AVBOIT Transmittance"
    );
    const Core::GpuGraphResourceId avboitCoverage = importBuffer(
        deferredTargets.avboit.coverageBuffer,
        Name("render.avboit.coverage"),
        "AVBOIT Coverage"
    );
    const Core::GpuGraphResourceId avboitDepthWarp = importBuffer(
        deferredTargets.avboit.depthWarpBuffer,
        Name("render.avboit.depth_warp"),
        "AVBOIT Depth Warp"
    );
    const Core::GpuGraphResourceId avboitControl = importBuffer(
        deferredTargets.avboit.controlBuffer,
        Name("render.avboit.control"),
        "AVBOIT Control"
    );
    const Core::GpuGraphResourceId avboitExtinction = importBuffer(
        deferredTargets.avboit.extinctionBuffer,
        Name("render.avboit.extinction"),
        "AVBOIT Extinction"
    );
    const Core::GpuGraphResourceId avboitExtinctionOverflow = importBuffer(
        deferredTargets.avboit.extinctionOverflowBuffer,
        Name("render.avboit.extinction_overflow"),
        "AVBOIT Extinction Overflow"
    );
    const Core::GpuGraphResourceId avboitMaterialDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.material_domain"), "Transparent Materials and Geometry")
    );
    const Core::GpuGraphResourceId avboitCsgDomain = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.avboit.csg_domain"), "Transparent CSG Intervals")
    );
    if(
        !albedo.valid()
        || !normal.valid()
        || !worldPosition.valid()
        || !depth.valid()
        || !shadowVisibility.valid()
        || !causticIrradiance.valid()
        || !surfelIrradiance.valid()
        || !currentSurfelIrradiance.valid()
        || !opaqueColor.valid()
        || !sceneShading.valid()
        || !lights.valid()
        || !bindlessSlots.valid()
        || !currentBindlessSlots.valid()
        || (m_rayTracingState.m_rayTraceMaterialContextSlotsBuffer && !materialContextSlots.valid())
        || (declaresHardwareCaustics && !hardwareCausticIrradiance.valid())
        || (capturesLaggedLightingHistory && (
            !historyCopyShadowVisibility.valid()
            || !historyCopyCausticIrradiance.valid()
            || !historyCopySurfelIrradiance.valid()
            || !historyCopyDestinationShadowVisibility.valid()
            || !historyCopyDestinationCausticIrradiance.valid()
            || !historyCopyDestinationSurfelIrradiance.valid()
        ))
        || !avboitLowRaster.valid()
        || !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !avboitTransmittance.valid()
        || !avboitCoverage.valid()
        || !avboitDepthWarp.valid()
        || !avboitControl.valid()
        || !avboitExtinction.valid()
        || !avboitExtinctionOverflow.valid()
        || !avboitMaterialDomain.valid()
        || !avboitCsgDomain.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-lighting graph resources"));
        return;
    }

    Core::GpuExternalCompletionDesc lightingPrefixCompletionDesc;
    lightingPrefixCompletionDesc
        .setIdentity(Name("render.deferred_lighting.graphics_prefix_complete"))
        .setMarkerLabel("Graphics Prefix Complete")
    ;
    m_deferredLightingPrefixCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        lightingPrefixCompletionDesc
    );
    if(!m_deferredLightingPrefixCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for deferred AVBOIT"));
        return;
    }

    Core::GpuExternalCompletionDesc surfelGiEffectsCompletionDesc;
    surfelGiEffectsCompletionDesc
        .setIdentity(Name("render.deferred_surfel_gi.effects_complete"))
        .setMarkerLabel("Shadow Effects Complete")
    ;
    m_deferredSurfelGiEffectsCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
        surfelGiEffectsCompletionDesc
    );
    if(!m_deferredSurfelGiEffectsCompletion.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import shadow-effects completion for deferred surfel GI"));
        return;
    }
    if(useLaggedLightingHistory){
        Core::GpuExternalCompletionDesc lightingHistoryCompletionDesc;
        lightingHistoryCompletionDesc
            .setIdentity(Name("render.deferred_lighting.lagged_history_complete"))
            .setMarkerLabel("Lagged Lighting History Complete")
        ;
        m_deferredLightingHistoryCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            lightingHistoryCompletionDesc
        );
        if(!m_deferredLightingHistoryCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import lagged-lighting history completion"));
            return;
        }
    }

    // Surfel GI remains the terminal Shadow/Software effect, but its completion is now the first deferred packet
    // boundary. Declaring it before hardware/AVBOIT preserves the established effects -> surfel -> suffix order.
    if(!declareDeferredSurfelGiTask(
        deferredTargets,
        worldPosition,
        normal,
        currentSurfelIrradiance,
        currentBindlessSlots,
        sceneShading,
        lights,
        materialContextSlots,
        m_deferredSurfelGiEffectsCompletion,
        surfelGiTimingTicket
    ))
        return;

    // Hardware Caustics belongs to this graph so the live irradiance producer/consumer transition is compiler-owned.
    // It is declared before Lighting: declaration order establishes the live current-irradiance RAW edge, while the
    // lagged route uses distinct current/history targets and intentionally has no Hardware-to-Lighting dependency.
    if(declaresHardwareCaustics){
        Core::GpuExternalCompletionDesc hardwarePrefixCompletionDesc;
        hardwarePrefixCompletionDesc
            .setIdentity(Name("render.hardware_caustics.graphics_prefix_complete"))
            .setMarkerLabel("Graphics Prefix Complete")
        ;
        m_deferredHardwareCausticsPrefixCompletion = m_deferredLightingTaskGraph.importExternalCompletion(
            hardwarePrefixCompletionDesc
        );
        if(!m_deferredHardwareCausticsPrefixCompletion.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import graphics-prefix completion for hardware caustics"));
            return;
        }

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
        const Core::GpuGraphResourceId sceneGeometryDomain = m_deferredLightingTaskGraph.importHazardDomain(
            HazardDomainDesc(Name("render.hardware_caustics.scene_geometry"), "Scene Acceleration and Geometry")
        );
        if(
            !causticAccumulator.valid()
            || !causticHistory.valid()
            || !causticResolveHalf.valid()
            || !causticResolveGeometry.valid()
            || !sceneGeometryDomain.valid()
        ){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import hardware-caustics graph resources"));
            return;
        }

        Core::Alloc::ScratchArena hardwareCausticsScratchArena(RendererArenaScope::s_TaskGraphArena);
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> hardwareResourceUses{ hardwareCausticsScratchArena };
        hardwareResourceUses.reserve(20u);
        hardwareResourceUses.push_back(ReadUse(
            worldPosition,
            Core::ResourceStates::ShaderResource,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(depth, Core::ResourceStates::ShaderResource));
        hardwareResourceUses.push_back(ReadUse(
            currentBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer,
            true
        ));
        hardwareResourceUses.push_back(ReadUse(lights, Core::ResourceStates::ShaderResource, true));
        hardwareResourceUses.push_back(ReadUse(sceneGeometryDomain));
        hardwareResourceUses.push_back(ReadWriteUse(causticAccumulator, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticHistory, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticResolveHalf, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(ReadWriteUse(causticResolveGeometry, Core::ResourceStates::UnorderedAccess));
        hardwareResourceUses.push_back(WriteUse(hardwareCausticIrradiance, Core::ResourceStates::UnorderedAccess));

        const auto appendOptionalReadBuffer = [&](
            const Core::BufferHandle& buffer,
            const Name& identity,
            const AStringView label,
            const Core::ResourceStates::Mask state
        ){
            if(!buffer)
                return true;
            const Core::GpuGraphResourceId resource = importBuffer(buffer, identity, label);
            if(!resource.valid())
                return false;
            hardwareResourceUses.push_back(ReadUse(resource, state));
            return true;
        };
        bool optionalResourcesImported =
            appendOptionalReadBuffer(
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
        ;
        if(materialContextSlots.valid()){
            hardwareResourceUses.push_back(ReadUse(
                materialContextSlots,
                Core::ResourceStates::ConstantBuffer,
                true
            ));
        }
        if(m_rayTracingState.m_tlas){
            const Core::GpuGraphResourceId tlas = m_deferredLightingTaskGraph.importAccelStruct(
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
                hardwareResourceUses.push_back(ReadUse(tlas, Core::ResourceStates::AccelStructRead));
                hardwareResourceUses.push_back(ReadUse(tlasBackingBuffer, Core::ResourceStates::AccelStructRead));
            }
        }
        if(!optionalResourcesImported){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import a hardware-caustics dynamic resource"));
            return;
        }

        const Core::GpuExternalCompletionId hardwareExternalDependencies[2] = {
            m_deferredHardwareCausticsPrefixCompletion,
            m_deferredLightingHistoryCompletion,
        };
        const usize hardwareExternalDependencyCount = useLaggedLightingHistory
            ? LengthOf(hardwareExternalDependencies)
            : 1u
        ;
        Core::GpuTaskSchedulingHint hardwareScheduling;
        hardwareScheduling.cost = Core::GpuTaskCostHint::Large;
        hardwareScheduling.forceSubmissionBoundary = true;
        hardwareScheduling.allowPacketMerge = false;
        Core::GpuTaskDesc hardwareDesc;
        hardwareDesc
            .setIdentity(Name("render.hardware_caustics"))
            .setMarkerLabel("Hardware Caustics")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(hardwareScheduling)
            .setExternalDependencies(hardwareExternalDependencies, hardwareExternalDependencyCount)
            .setResourceUses(hardwareResourceUses.data(), hardwareResourceUses.size())
        ;
        m_deferredHardwareCausticsTask = m_raytracingSystem.declareHardwareCausticsTask(
            m_deferredLightingTaskGraph,
            hardwareDesc,
            deferredTargets,
            shadowVisibilityPrepared,
            hardwareCausticsTimingTicket
        );
        if(!m_deferredHardwareCausticsTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare hardware-caustics graph task"));
            return;
        }
    }

    const Core::GpuTaskResourceUse avboitPreResourceUses[] = {
        ReadUse(albedo),
        ReadUse(normal, Core::ResourceStates::ShaderResource, true),
        ReadUse(worldPosition, Core::ResourceStates::ShaderResource, true),
        ReadUse(depth),
        ReadWriteUse(avboitLowRaster, Core::ResourceStates::RenderTarget),
        ReadWriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget),
        ReadWriteUse(avboitAccumExtinction, Core::ResourceStates::RenderTarget),
        ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(avboitCoverage, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(avboitExtinction, Core::ResourceStates::UnorderedAccess),
        ReadWriteUse(avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess),
        ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer, true),
        ReadUse(avboitMaterialDomain),
        ReadWriteUse(avboitCsgDomain, Core::ResourceStates::ShaderResource),
    };
    Core::GpuTaskSchedulingHint avboitGraphicsScheduling;
    avboitGraphicsScheduling.cost = Core::GpuTaskCostHint::Large;
    avboitGraphicsScheduling.forceSubmissionBoundary = true;
    avboitGraphicsScheduling.allowPacketMerge = false;
    Core::GpuTaskDesc avboitPreDesc;
    avboitPreDesc
        .setIdentity(Name("render.avboit.pre"))
        .setMarkerLabel(splitAvboitStages ? "AVBOIT Pre" : "AVBOIT")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(avboitGraphicsScheduling)
        .setExternalDependencies(&m_deferredLightingPrefixCompletion, 1u)
        .setResourceUses(avboitPreResourceUses, LengthOf(avboitPreResourceUses))
    ;
    m_deferredAvboitPreTask = m_deferredLightingTaskGraph.addTask<AvboitPreGraphTask>(
        avboitPreDesc,
        AvboitPreGraphTask::Payload{
            .avboitSystem = &m_avboitSystem,
            .targets = &deferredTargets,
            .csgFrameState = &csgFrameState,
            .timingTicket = &avboitPreTimingTicket,
            .clearTargets = clearAvboitTargets,
            .hasTransparentRenderers = hasTransparentRenderers,
            .splitStages = splitAvboitStages,
        }
    );
    if(!m_deferredAvboitPreTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT pre graph task"));
        return;
    }

    if(splitAvboitStages){
        const Core::GpuTaskResourceUse depthWarpResourceUses[] = {
            ReadUse(avboitCoverage),
            ReadWriteUse(avboitDepthWarp, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitControl, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        Core::GpuTaskSchedulingHint avboitComputeScheduling;
        avboitComputeScheduling.cost = Core::GpuTaskCostHint::Medium;
        avboitComputeScheduling.forceSubmissionBoundary = true;
        avboitComputeScheduling.allowPacketMerge = false;
        const Core::GpuTaskId preDependency[] = { m_deferredAvboitPreTask };
        Core::GpuTaskDesc depthWarpDesc;
        depthWarpDesc
            .setIdentity(Name("render.avboit.depth_warp"))
            .setMarkerLabel("AVBOIT Depth Warp")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setDependencies(preDependency, LengthOf(preDependency))
            .setResourceUses(depthWarpResourceUses, LengthOf(depthWarpResourceUses))
        ;
        m_deferredAvboitDepthWarpTask = m_deferredLightingTaskGraph.addTask<AvboitDepthWarpGraphTask>(
            depthWarpDesc,
            AvboitDepthWarpGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitDepthWarpTimingTicket,
            }
        );
        if(!m_deferredAvboitDepthWarpTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT depth-warp graph task"));
            return;
        }

        const Core::GpuTaskResourceUse extinctionResourceUses[] = {
            ReadUse(avboitLowRaster, Core::ResourceStates::RenderTarget),
            ReadUse(avboitDepthWarp),
            ReadUse(avboitControl),
            ReadWriteUse(avboitExtinction, Core::ResourceStates::UnorderedAccess),
            ReadWriteUse(avboitExtinctionOverflow, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(avboitMaterialDomain),
            ReadUse(avboitCsgDomain),
        };
        const Core::GpuTaskId depthWarpDependency[] = { m_deferredAvboitDepthWarpTask };
        Core::GpuTaskDesc extinctionDesc;
        extinctionDesc
            .setIdentity(Name("render.avboit.extinction"))
            .setMarkerLabel("AVBOIT Extinction")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(avboitGraphicsScheduling)
            .setDependencies(depthWarpDependency, LengthOf(depthWarpDependency))
            .setResourceUses(extinctionResourceUses, LengthOf(extinctionResourceUses))
        ;
        m_deferredAvboitExtinctionTask = m_deferredLightingTaskGraph.addTask<AvboitExtinctionGraphTask>(
            extinctionDesc,
            AvboitExtinctionGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .csgFrameState = &csgFrameState,
                .timingTicket = &avboitExtinctionTimingTicket,
            }
        );
        if(!m_deferredAvboitExtinctionTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT extinction graph task"));
            return;
        }

        const Core::GpuTaskResourceUse integrationResourceUses[] = {
            ReadUse(avboitExtinction),
            ReadUse(avboitControl),
            ReadUse(avboitExtinctionOverflow),
            ReadWriteUse(avboitTransmittance, Core::ResourceStates::UnorderedAccess),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
        };
        const Core::GpuTaskId extinctionDependency[] = { m_deferredAvboitExtinctionTask };
        Core::GpuTaskDesc integrationDesc;
        integrationDesc
            .setIdentity(Name("render.avboit.integration"))
            .setMarkerLabel("AVBOIT Integration")
            .setQueue(ComputeQueueRequest())
            .setScheduling(avboitComputeScheduling)
            .setDependencies(extinctionDependency, LengthOf(extinctionDependency))
            .setResourceUses(integrationResourceUses, LengthOf(integrationResourceUses))
        ;
        m_deferredAvboitIntegrationTask = m_deferredLightingTaskGraph.addTask<AvboitIntegrationGraphTask>(
            integrationDesc,
            AvboitIntegrationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets.avboit,
                .timingTicket = &avboitIntegrationTimingTicket,
            }
        );
        if(!m_deferredAvboitIntegrationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT integration graph task"));
            return;
        }

        const Core::GpuTaskResourceUse accumulationResourceUses[] = {
            ReadUse(depth),
            ReadUse(avboitTransmittance),
            ReadUse(avboitDepthWarp),
            ReadUse(avboitControl),
            ReadWriteUse(avboitAccumColor, Core::ResourceStates::RenderTarget),
            ReadWriteUse(avboitAccumExtinction, Core::ResourceStates::RenderTarget),
            ReadUse(currentBindlessSlots, Core::ResourceStates::ConstantBuffer),
            ReadUse(avboitMaterialDomain),
            ReadUse(avboitCsgDomain),
        };
        const Core::GpuTaskId integrationDependency[] = { m_deferredAvboitIntegrationTask };
        Core::GpuTaskDesc accumulationDesc;
        accumulationDesc
            .setIdentity(Name("render.avboit.accumulation"))
            .setMarkerLabel("AVBOIT Accumulation")
            .setQueue(GraphicsQueueRequest())
            .setScheduling(avboitGraphicsScheduling)
            .setDependencies(integrationDependency, LengthOf(integrationDependency))
            .setResourceUses(accumulationResourceUses, LengthOf(accumulationResourceUses))
        ;
        m_deferredAvboitAccumulationTask = m_deferredLightingTaskGraph.addTask<AvboitAccumulationGraphTask>(
            accumulationDesc,
            AvboitAccumulationGraphTask::Payload{
                .avboitSystem = &m_avboitSystem,
                .targets = &deferredTargets,
                .csgFrameState = &csgFrameState,
                .timingTicket = &avboitAccumulationTimingTicket,
            }
        );
        if(!m_deferredAvboitAccumulationTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred AVBOIT accumulation graph task"));
            return;
        }
    }
    const Core::GpuTaskId avboitFinalTask = splitAvboitStages
        ? m_deferredAvboitAccumulationTask
        : m_deferredAvboitPreTask
    ;

    const Core::GpuExternalCompletionId laggedLightingExternalDependencies[] = {
        m_deferredLightingPrefixCompletion,
        m_deferredLightingHistoryCompletion,
    };
    const Core::GpuExternalCompletionId* const lightingExternalDependencies = useLaggedLightingHistory
        ? laggedLightingExternalDependencies
        : nullptr
    ;
    const usize lightingExternalDependencyCount = useLaggedLightingHistory
        ? LengthOf(laggedLightingExternalDependencies)
        : 0u
    ;
    // Live Lighting joins Surfel GI, AVBOIT, and Hardware Caustics through internal graph edges. Active lagged
    // Lighting instead reads history and stays independent from the current-frame Surfel/Hardware producers.
    const Core::GpuTaskId lightingDependencies[] = {
        m_deferredSurfelGiTask,
        avboitFinalTask,
        m_deferredHardwareCausticsTask,
    };
    const usize lightingDependencyCount = useLaggedLightingHistory
        ? 0u
        : (lightingDependsOnHardwareCaustics ? LengthOf(lightingDependencies) : 2u)
    ;
    // Active lagged Lighting receives these shared read-only states directly from the accepted prefix source. This
    // lets it avoid importing the recorded snapshots from Hardware Caustics and AVBOIT Pre while it reads history.
    const bool laggedReadsHaveIndependentStateSources = useLaggedLightingHistory;
    const Core::GpuTaskResourceUse resourceUses[] = {
        ReadTextureUse(
            albedo,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            normal,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            worldPosition,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(
            depth,
            ECSRenderDetail::s_FramebufferSubresources,
            Core::ResourceStates::ShaderResource,
            laggedReadsHaveIndependentStateSources
        ),
        ReadTextureUse(shadowVisibility, ECSRenderDetail::s_ShadowVisibilitySubresources),
        ReadTextureUse(causticIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadTextureUse(surfelIrradiance, ECSRenderDetail::s_FramebufferSubresources),
        ReadUse(
            sceneShading,
            Core::ResourceStates::ConstantBuffer,
            laggedReadsHaveIndependentStateSources
        ),
        ReadUse(lights, Core::ResourceStates::ShaderResource, laggedReadsHaveIndependentStateSources),
        ReadUse(
            bindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            laggedReadsHaveIndependentStateSources
        ),
        WriteTextureUse(opaqueColor, ECSRenderDetail::s_FramebufferSubresources, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Large;
    scheduling.forceSubmissionBoundary = true;
    scheduling.allowPacketMerge = false;
    Core::GpuTaskDesc desc;
    desc
        .setIdentity(Name("render.deferred_lighting"))
        .setMarkerLabel("Deferred Lighting")
        .setQueue(ComputeQueueRequest())
        .setScheduling(scheduling)
        .setDependencies(lightingDependencies, lightingDependencyCount)
        .setExternalDependencies(lightingExternalDependencies, lightingExternalDependencyCount)
        .setResourceUses(resourceUses, LengthOf(resourceUses))
    ;
    m_deferredLightingTask = m_deferredSystem.declareDeferredLightingTask(
        m_deferredLightingTaskGraph,
        desc,
        deferredTargets,
        useLaggedLightingHistory,
        lightingTimingTicket
    );
    if(!m_deferredLightingTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-lighting graph task"));
        return;
    }

    // Composite remains a distinct packet and joins both graph-owned AVBOIT and Lighting. It retains the current
    // bindless selector in lagged mode rather than inheriting Lighting's history selector.
    const Core::GpuGraphResourceId compositeColor = importTexture(
        deferredTargets.compositeColor,
        Name("render.deferred_composite.composite_color"),
        "Composite Color"
    );
    const Core::GpuGraphResourceId compositeBindlessSlots = currentBindlessSlots;
    if(
        !avboitAccumColor.valid()
        || !avboitAccumExtinction.valid()
        || !compositeColor.valid()
        || !compositeBindlessSlots.valid()
    ){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-composite graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse compositeResourceUses[] = {
        ReadUse(opaqueColor),
        ReadUse(avboitAccumColor),
        ReadUse(avboitAccumExtinction),
        ReadUse(
            compositeBindlessSlots,
            Core::ResourceStates::ConstantBuffer,
            useLaggedLightingHistory
        ),
        WriteUse(compositeColor, Core::ResourceStates::UnorderedAccess),
    };
    Core::GpuTaskSchedulingHint compositeScheduling;
    compositeScheduling.cost = Core::GpuTaskCostHint::Medium;
    compositeScheduling.avoidQueueCrossing = schedule.usesLaggedLightingHistory();
    compositeScheduling.forceSubmissionBoundary = true;
    compositeScheduling.allowPacketMerge = false;
    const Core::GpuTaskId compositeDependencies[] = {
        m_deferredLightingTask,
        avboitFinalTask,
    };
    Core::GpuTaskDesc compositeDesc;
    compositeDesc
        .setIdentity(Name("render.deferred_composite"))
        .setMarkerLabel("Deferred Composite")
        .setQueue(ComputeQueueRequest())
        .setScheduling(compositeScheduling)
        .setDependencies(compositeDependencies, LengthOf(compositeDependencies))
        .setResourceUses(compositeResourceUses, LengthOf(compositeResourceUses))
    ;
    m_deferredCompositeTask = m_deferredSystem.declareDeferredCompositeTask(
        m_deferredLightingTaskGraph,
        compositeDesc,
        deferredTargets,
        compositeTimingTicket
    );
    if(!m_deferredCompositeTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-composite graph task"));
        return;
    }

    const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importHazardDomain(
        HazardDomainDesc(Name("render.deferred_present.backbuffer"), "Presentation Back Buffer")
    );
    if(!backbuffer.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not import deferred-present graph resources"));
        return;
    }

    const Core::GpuTaskResourceUse presentResourceUses[] = {
        ReadUse(compositeColor),
        ReadUse(compositeBindlessSlots, Core::ResourceStates::ConstantBuffer),
        WriteUse(backbuffer, Core::ResourceStates::Present),
    };
    Core::GpuTaskSchedulingHint presentScheduling;
    presentScheduling.cost = Core::GpuTaskCostHint::Medium;
    presentScheduling.avoidQueueCrossing = useLaggedLightingHistory;
    presentScheduling.forceSubmissionBoundary = true;
    presentScheduling.allowPacketMerge = false;
    const Core::GpuTaskId presentDependencies[] = {
        m_deferredCompositeTask,
        m_deferredSurfelGiTask,
    };
    const usize presentDependencyCount = useLaggedLightingHistory ? LengthOf(presentDependencies) : 1u;
    Core::GpuTaskDesc presentDesc;
    presentDesc
        .setIdentity(Name("render.deferred_present"))
        .setMarkerLabel("Deferred Present")
        .setQueue(GraphicsQueueRequest())
        .setScheduling(presentScheduling)
        .setDependencies(presentDependencies, presentDependencyCount)
        .setResourceUses(presentResourceUses, LengthOf(presentResourceUses))
    ;
    m_deferredPresentTask = m_deferredLightingTaskGraph.addTask<DeferredPresentGraphTask>(
        presentDesc,
        DeferredPresentGraphTask::Payload{
            .deferredSystem = &m_deferredSystem,
            .graphics = &m_graphics,
            .targets = &deferredTargets,
            .presentationFramebuffer = presentationFramebuffer,
            .frameTimingTransaction = &frameTimingTransaction,
            .asyncFinalTiming = &asyncFinalTiming,
            .timingTicket = &presentTimingTicket,
            .shadowVisibilityRunsOnCompute = shadowVisibilityRunsOnCompute,
        }
    );
    if(!m_deferredPresentTask.valid()){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred-present graph task"));
        return;
    }

    if(capturesLaggedLightingHistory){
        const Core::GpuTaskResourceUse historyCopyResourceUses[] = {
            ReadUse(historyCopyShadowVisibility, Core::ResourceStates::CopySource),
            ReadUse(historyCopyCausticIrradiance, Core::ResourceStates::CopySource),
            ReadUse(historyCopySurfelIrradiance, Core::ResourceStates::CopySource),
            WriteUse(historyCopyDestinationShadowVisibility, Core::ResourceStates::CopyDest),
            WriteUse(historyCopyDestinationCausticIrradiance, Core::ResourceStates::CopyDest),
            WriteUse(historyCopyDestinationSurfelIrradiance, Core::ResourceStates::CopyDest),
        };
        Core::GpuTaskSchedulingHint historyCopyScheduling;
        historyCopyScheduling.cost = Core::GpuTaskCostHint::Medium;
        historyCopyScheduling.forceSubmissionBoundary = true;
        historyCopyScheduling.allowPacketMerge = false;
        const Core::GpuTaskId historyCopyDependencies[] = { m_deferredPresentTask };
        Core::GpuTaskDesc historyCopyDesc;
        historyCopyDesc
            .setIdentity(Name("render.lagged_history_copy"))
            .setMarkerLabel("Lagged Lighting History Copy")
            .setQueue(TransferQueueRequest())
            .setScheduling(historyCopyScheduling)
            .setDependencies(historyCopyDependencies, LengthOf(historyCopyDependencies))
            .setResourceUses(historyCopyResourceUses, LengthOf(historyCopyResourceUses))
        ;
        m_deferredLaggedLightingHistoryTask = m_deferredLightingTaskGraph.addTask<LaggedLightingHistoryCopyTask>(
            historyCopyDesc,
            LaggedLightingHistoryCopyTask::Payload{
                .sourceShadowVisibility = deferredTargets.shadowVisibility,
                .sourceCausticIrradiance = deferredTargets.causticIrradiance,
                .sourceSurfelIrradiance = deferredTargets.surfelIrradiance,
                .destinationShadowVisibility = captureHistory->shadowVisibility,
                .destinationCausticIrradiance = captureHistory->causticIrradiance,
                .destinationSurfelIrradiance = captureHistory->surfelIrradiance,
                .acceptedHistoryToken = &m_laggedLightingHistorySubmissionToken,
            }
        );
        if(!m_deferredLaggedLightingHistoryTask.valid()){
            NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not declare deferred lagged-lighting history-copy task"));
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
        m_deferredLightingTaskGraph,
        m_deferredLightingTaskGraphAnalysis,
        topology,
        m_deferredLightingTaskGraphQueueAssignments,
        m_deferredLightingCompiledGraph,
        scratchArena
    )){
        NWB_LOGGER_WARNING(NWB_TEXT("RendererSystem: could not compile deferred AVBOIT/lighting/composite/present task graph"));
        return;
    }
    m_deferredLightingRecordedGraph.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingSubmissionTransaction.reset(m_deferredLightingCompiledGraph);
    m_deferredLightingTaskGraphValid = true;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

