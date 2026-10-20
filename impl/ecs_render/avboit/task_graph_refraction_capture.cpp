// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_refraction_capture.h"
#include "generated_geometry_reuse.h"

#include <impl/ecs_render/material/generated_geometry_state.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/kernel/arena_names.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/shared/renderer_frame_types.h>

#include <core/graphics/backend_selection/backend.h>

#include <global/basic_string.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace RendererTaskGraphDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_refraction_capture{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportTexture(Core::GpuTaskGraph& graph, const Core::TextureHandle& texture){
    if(!texture)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedTexture(texture);
        if(existing.valid())
            return existing;
    }
    return graph.importTexture(texture, TextureResourceDesc(texture->getCreationDescription().name, "Refraction Capture Texture"));
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    if(!buffer)
        return MakeUnexpected(Failure{});
    {
        const Core::GpuTaskGraph::DeclarationReadView declarations(graph);
        if(!declarations.valid())
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId existing = declarations.findImportedBuffer(buffer);
        if(existing.valid())
            return existing;
    }
    return graph.importBuffer(buffer, BufferResourceDesc(buffer->getCreationDescription().debugName, "Refraction Capture Buffer"));
}

[[nodiscard]] Core::GpuTaskDesc TaskDesc(const Name identity, const AStringView label, const Core::GpuTaskId& dependency)noexcept{
    Core::GpuTaskSchedulingHint scheduling;
    scheduling.cost = Core::GpuTaskCostHint::Tiny;
    scheduling.forceSubmissionBoundary = false;
    scheduling.allowPacketMerge = true;
    scheduling.mergeWithPrevious = true;
    scheduling.allowMergeAcrossConsumerFrontier = true;
    Core::GpuTaskDesc desc;
    desc.setIdentity(identity).setMarkerLabel(label).setScheduling(scheduling);
    if(dependency.valid())
        desc.setDependencies(&dependency, 1u);
    return desc;
}

struct CaptureDrawTask{
    // The material system and the frame targets are required: capture always draws against this frame's targets.
    struct Payload{
        RendererMaterialSystem& materialSystem;
        const DeferredFrameTargets& deferredTargets;
        AvboitFrameTargets avboitTargets;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena> drawItems;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        bool csg = false;
        RenderPath::Enum renderPath = RenderPath::MeshShader;
        bool generate = false;
        bool conservativeGeometryScissor = false;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererMaterialSystem& materialSystemIn, const DeferredFrameTargets& deferredTargetsIn)noexcept
            : materialSystem(materialSystemIn)
            , deferredTargets(deferredTargetsIn)
            , drawItems(arena)
        {}
    };

    [[nodiscard]] static constexpr Core::GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{
        return { payload.generate ? Core::GpuQueueCapability::Compute : Core::GpuQueueCapability::Graphics };
    }

    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.avboitTargets.refractionFramebuffer || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount) || (payload.csg && !payload.csgResources.bindingValid()))
            return false;

        Core::Alloc::ScratchArena scratch(RendererArenaScope::s_RenderArena);
        MaterialPassDrawItemVector drawItems{scratch};
        drawItems.reserve(payload.drawItems.size());
        drawItems.assign(payload.drawItems.begin(), payload.drawItems.end());
        bool resourcesReady = false;
        switch(payload.renderPath){
        case RenderPath::MeshShader:
            resourcesReady = payload.materialSystem.meshMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::VertexIndexed:
            resourcesReady = payload.materialSystem.indexedMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::ComputeEmulation:
            resourcesReady = payload.materialSystem.computeMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        default:
            return false;
        }
        if(!resourcesReady || (payload.generate && payload.renderPath != RenderPath::ComputeEmulation))
            return false;

        commandList.endRenderPass();
        Core::ViewportState viewport;
        viewport.addViewportAndScissorRect(payload.avboitTargets.refractionFramebuffer->getFramebufferInfo().getViewport());
        const MaterialPassDrawContext drawContext{
            commandList,
            payload.generate ? nullptr : payload.avboitTargets.refractionFramebuffer.get(),
            &payload.avboitTargets,
            viewport,
            payload.csg ? &payload.csgResources : nullptr,
            payload.frameBindings,
            MaterialPipelinePass::AvboitRefractionCapture,
            true,
            true,
            true,
            payload.conservativeGeometryScissor
        };
        if(payload.generate)
            payload.materialSystem.generateComputeMaterialPassDrawItems(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::ComputeEmulation)
            payload.materialSystem.renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::VertexIndexed)
            payload.materialSystem.renderIndexedMaterialPassDrawItems(drawContext, drawItems);
        else
            payload.materialSystem.renderMeshMaterialPassDrawItems(drawContext, drawItems);
        commandList.endRenderPass();
        return true;
    }
};

struct CaptureCapTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {Core::GpuQueueCapability::Graphics};

    struct Payload{
        RendererCsgSystem& csgSystem;
        const DeferredFrameTargets& targets;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Core::Rect workRect;
        Vector<u32, Core::Alloc::GlobalArena> receiverIndices;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererCsgSystem& csgSystemIn, const DeferredFrameTargets& targetsIn)noexcept
            : csgSystem(csgSystemIn)
            , targets(targetsIn)
            , receiverIndices(arena)
        {}
    };


public:
    [[nodiscard]] static bool Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.csgResources.refractionCapFillPipeline || !payload.csgResources.bindingValid() || !payload.targets.avboit.refractionFramebuffer || !payload.targets.bindless.slotsBufferDescriptor.valid() || payload.receiverIndices.empty())
            return false;
        commandList.endRenderPass();
        payload.csgSystem.renderRefractionIntervalCaps(commandList, payload.targets, payload.csgResources, payload.workRect, payload.receiverIndices.data(), payload.receiverIndices.size());
        commandList.endRenderPass();
        return true;
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};

    struct Payload{};


public:
    [[nodiscard]] static bool Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        commandList.endRenderPass();
        return true;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Core::GpuTaskId> DeclareAvboitRefractionCapture(
    Core::Alloc::ScratchArena& scratchArena,
    Core::GpuTaskGraph& graph,
    Core::Alloc::GlobalArena& arena,
    RendererMaterialSystem& materialSystem,
    RendererCsgSystem& csgSystem,
    DeferredFrameTargets& targets,
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const ECSRenderDetail::MeshViewGpuData& meshViewState,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry,
    Core::GpuTaskId dependency,
    const bool enabled
){
    using namespace __hidden_refraction_capture;
    generatedGeometry.reset();
    const auto depthResult = ImportTexture(graph, targets.avboit.refractionDepth);
    const auto normalIorResult = ImportTexture(graph, targets.avboit.refractionNormalIor);
    const auto tintCoverageResult = ImportTexture(graph, targets.avboit.refractionTintCoverage);
    const auto instanceResult = ImportTexture(graph, targets.avboit.refractionInstance);
    const auto specularRoughnessResult = ImportTexture(graph, targets.avboit.refractionSpecularRoughness);
    if(!depthResult || !normalIorResult || !tintCoverageResult || !instanceResult || !specularRoughnessResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId depth = *depthResult;
    const Core::GpuGraphResourceId normalIor = *normalIorResult;
    const Core::GpuGraphResourceId tintCoverage = *tintCoverageResult;
    const Core::GpuGraphResourceId instance = *instanceResult;
    const Core::GpuGraphResourceId specularRoughness = *specularRoughnessResult;

    const auto appendClear = [&](const Name identity, const Core::GpuGraphResourceId destination, const bool isDepth, const Core::Color color = Core::Color(0.f, 0.f, 0.f, 0.f)){
        Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Clear", dependency);
        Core::GpuClearTextureTaskDesc clear;
        clear.destination = destination;
        clear.subresources = Core::TextureSubresourceSet(0u, 1u, 0u, 1u);
        clear.valueType = isDepth ? Core::GpuClearTextureTaskValueType::DepthStencil : Core::GpuClearTextureTaskValueType::Float;
        clear.clearDepth = isDepth;
        clear.depthValue = 1.f;
        clear.floatValue = color;
        const auto producedTask = graph.addClearTextureTask(desc, clear);
        if(!producedTask){
            dependency = {};
            return false;
        }
        dependency = *producedTask;
        return true;
    };
    if(!appendClear(Name("render.refraction.clear.depth"), depth, true)
        || !appendClear(Name("render.refraction.clear.normal_ior"), normalIor, false)
        || !appendClear(Name("render.refraction.clear.tint_coverage"), tintCoverage, false)
        || !appendClear(Name("render.refraction.clear.instance"), instance, false)
        || !appendClear(Name("render.refraction.clear.specular_roughness"), specularRoughness, false, Core::Color(0.f, 0.f, 0.f, 1.f)))
        return MakeUnexpected(Failure{});

    const auto finalize = [&](){
        const Core::GpuTaskResourceUse uses[] = { ReadUse(depth), ReadUse(normalIor), ReadUse(tintCoverage), ReadUse(instance), ReadUse(specularRoughness) };
        Core::GpuTaskDesc desc = TaskDesc(Name("render.refraction.capture_finalize"), "Refraction Capture Finalize", dependency);
        desc.setResourceUses(uses, LengthOf(uses));
        return graph.addTask<FinalizeTask>(desc, FinalizeTask::Payload{});
    };
    if(!enabled)
        return finalize();
    if(!targets.avboit.refractionFramebuffer)
        return MakeUnexpected(Failure{});
    MaterialPassDrawItemPartitions drawItems{scratchArena};
    InstanceGpuDataVector instances{scratchArena};
    MaterialTypedByteDataVector typedBytes{scratchArena};
    CsgFrameGpuData csgFrameData{scratchArena};
#if defined(NWB_DEBUG)
    ECSRenderDetail::MaterialTypedInstanceRangeVector typedRanges{scratchArena};
#endif
    materialSystem.gatherMaterialPassDrawItems(
        targets.avboit.refractionFramebuffer.get(), MaterialPipelinePass::AvboitRefractionCapture, true,
        csgFrameState, drawItems, instances, csgFrameData,
#if defined(NWB_DEBUG)
        typedRanges,
#endif
        typedBytes, RendererResourceLookupMode::PreparedOnly, &meshViewState
    );
    if(drawItems.empty())
        return finalize();
    const bool hasCsg = !drawItems.csg.empty();
    if(!frameBindings.frameReady(instances.size(), typedBytes.size())
        || !materialSystem.materialPassDrawResourcesReady(drawItems.regular, frameBindings)
        || (hasCsg && (!csgFrameData.hasWork() || !csgResources.frameReady(csgFrameData) || !materialSystem.materialPassDrawResourcesReady(drawItems.csg, frameBindings))))
        return MakeUnexpected(Failure{});

    const MaterialPassDrawItems* const drawSets[] = {&drawItems.regular, &drawItems.csg};
    const auto geometrySet = GatherPreparedMaterialGeometryResourceSet(graph, drawSets, LengthOf(drawSets), scratchArena, Name("render.refraction.capture.geometry"), "Refraction Capture Geometry");
    if(!geometrySet)
        return MakeUnexpected(Failure{});
    const auto sampledTextureSet = GatherPreparedMaterialSampledTextureResourceSet(materialSystem, graph, drawSets, LengthOf(drawSets), scratchArena, Name("render.refraction.capture.material_textures"), "Refraction Capture Material Textures");
    if(!sampledTextureSet)
        return MakeUnexpected(Failure{});
    const Core::GpuTaskResourceSetUse setUses[] = {
        {.resourceSet = *geometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
        {.resourceSet = *sampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
    };
    constexpr usize s_GeometryAndTextureSetUseCount = 2u;
    const usize setUseCount = sampledTextureSet->valid() ? s_GeometryAndTextureSetUseCount : 1u;

    materialSystem.prepareMaterialPassInstanceUploadData(instances, csgResources);
    const bool producesReusableGeometry = generatedGeometry.capture(drawItems, instances, frameBindings, meshViewState, MaterialPipelinePass::AvboitRefractionCapture);
    const auto materialInstancesResult = ImportBuffer(graph, frameBindings.instanceBuffer);
    const auto materialTypedResult = ImportBuffer(graph, frameBindings.materialTypedBuffer);
    const auto meshViewResult = ImportBuffer(graph, frameBindings.meshView.buffer);
    const auto slotsResult = ImportBuffer(graph, targets.bindless.slotsBuffer);
    const auto opaqueDepthResult = ImportTexture(graph, targets.depth);
    if(!materialInstancesResult || !materialTypedResult || !meshViewResult || !slotsResult || !opaqueDepthResult)
        return MakeUnexpected(Failure{});
    const Core::GpuGraphResourceId materialInstances = *materialInstancesResult;
    const Core::GpuGraphResourceId materialTyped = *materialTypedResult;
    const Core::GpuGraphResourceId meshView = *meshViewResult;
    const Core::GpuGraphResourceId slots = *slotsResult;
    const Core::GpuGraphResourceId opaqueDepth = *opaqueDepthResult;

    const auto appendUpload = [&](const Name identity, const Core::GpuGraphResourceId destination, const void* bytes, const usize byteSize, const usize alignment){
        if(!destination.valid() || !bytes || byteSize == 0u)
            return false;
        const auto blobResult = graph.copyUploadData(bytes, byteSize, alignment);
        if(!blobResult)
            return false;
        const Core::GpuUploadBlobId blob = *blobResult;
        Core::GpuTaskDesc desc = TaskDesc(identity, "Refraction Capture Upload", dependency);
        const auto producedTask = graph.addUploadBufferTask(desc, Core::GpuUploadBufferTaskDesc{ .source = blob, .destination = destination, .finalState = Core::ResourceStates::Common, });
        if(!producedTask){
            dependency = {};
            return false;
        }
        dependency = *producedTask;
        return true;
    };
    if(!appendUpload(Name("render.refraction.capture.instances_upload"), materialInstances, instances.data(), instances.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData))
        || !appendUpload(Name("render.refraction.capture.material_upload"), materialTyped, typedBytes.data(), typedBytes.size(), alignof(u32)))
        return MakeUnexpected(Failure{});

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> commonUses{scratchArena};
    constexpr usize s_CommonUseCapacity = 4u;
    commonUses.reserve(s_CommonUseCapacity);
    commonUses.push_back(ReadBufferUse(materialInstances, Core::BufferRange(0u, instances.size() * sizeof(InstanceGpuData))));
    commonUses.push_back(ReadBufferUse(materialTyped, Core::BufferRange(0u, typedBytes.size())));
    commonUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    commonUses.push_back(ReadUse(slots, Core::ResourceStates::ConstantBuffer));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses{scratchArena};
    if(hasCsg){
        constexpr usize s_CsgUseCapacity = 8u;
        csgUses.reserve(s_CsgUseCapacity);
        const auto clipContext = csgSystem.prepareCsgClipContextSlotData(targets, csgFrameData, csgResources, frameBindings);
        if(!clipContext)
            return MakeUnexpected(Failure{});
        const auto rangesResult = ImportBuffer(graph, csgResources.receiverRanges);
        const auto cuttersResult = ImportBuffer(graph, csgResources.cutters);
        const auto clipResult = ImportBuffer(graph, csgResources.clipContextSlots);
        const auto intervalResult = ImportBuffer(graph, csgResources.intervalSampleState);
        if(!intervalResult
            || !rangesResult
            || !appendUpload(Name("render.refraction.capture.csg_ranges_upload"), *rangesResult, csgFrameData.receiverRanges.data(), csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData), alignof(CsgReceiverRangeGpuData))
            || !cuttersResult
            || !appendUpload(Name("render.refraction.capture.csg_cutters_upload"), *cuttersResult, csgFrameData.cutters.data(), csgFrameData.cutters.size() * sizeof(CsgCutterGpuData), alignof(CsgCutterGpuData))
            || !clipResult
            || !appendUpload(Name("render.refraction.capture.csg_context_upload"), *clipResult, &*clipContext, sizeof(*clipContext), alignof(CsgClipContextSlots)))
            return MakeUnexpected(Failure{});
        const Core::GpuGraphResourceId interval = *intervalResult;
        const Core::GpuGraphResourceId ranges = *rangesResult;
        const Core::GpuGraphResourceId cutters = *cuttersResult;
        const Core::GpuGraphResourceId clip = *clipResult;
        csgUses.push_back(ReadBufferUse(ranges, Core::BufferRange(0u, csgFrameData.receiverRanges.size() * sizeof(CsgReceiverRangeGpuData))));
        csgUses.push_back(ReadBufferUse(cutters, Core::BufferRange(0u, csgFrameData.cutters.size() * sizeof(CsgCutterGpuData))));
        csgUses.push_back(ReadUse(clip, Core::ResourceStates::ConstantBuffer));
        csgUses.push_back(ReadUse(interval, Core::ResourceStates::ConstantBuffer));
        const ReferenceWrapper<const Core::TextureHandle> intervalTextures[] = { targets.csgRemovedIntervalDepth, targets.csgRemovedIntervalCapNormal, targets.csgRemovedIntervalData, targets.csgRemovedIntervalCount, };
        for(const Core::TextureHandle& texture : intervalTextures){
            const auto resourceResult = ImportTexture(graph, texture);
            if(!resourceResult)
                return MakeUnexpected(Failure{});
            const Core::GpuGraphResourceId resource = *resourceResult;
            csgUses.push_back(ReadTextureUse(resource, Core::s_AllSubresources, Core::ResourceStates::UnorderedAccess));
        }
    }

    if(!objectGeometry.prepare(drawItems.regular.indexedDrawItems.data(), drawItems.regular.indexedDrawItems.size(), frameBindings, targets, dependency, commonUses, scratchArena))
        return MakeUnexpected(Failure{});
    if(!objectGeometry.prepare(drawItems.csg.indexedDrawItems.data(), drawItems.csg.indexedDrawItems.size(), frameBindings, targets, dependency, commonUses, scratchArena))
        return MakeUnexpected(Failure{});

    usize drawTaskIndex = 0u;
    const auto appendDraw = [&](const MaterialPassDrawItem* items, const usize count, const bool csg, const RenderPath::Enum renderPath, const bool generate, const Core::GpuGraphResourceId output){
        if(count == 0u)
            return true;
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
        constexpr usize s_DrawExtraUseCapacity = 7u;
        uses.reserve(commonUses.size() + (csg ? csgUses.size() : 0u) + s_DrawExtraUseCapacity);
        uses.assign(commonUses.begin(), commonUses.end());
        if(csg)
            uses.insert(uses.end(), csgUses.begin(), csgUses.end());
        if(renderPath == RenderPath::ComputeEmulation)
            uses.push_back(generate ? WriteUse(output, Core::ResourceStates::UnorderedAccess) : ReadUse(output, ECSRenderDetail::s_GeneratedGeometryRasterState));
        if(!generate){
            uses.push_back(ReadUse(opaqueDepth));
            uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
            uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
            uses.push_back(ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget));
        }
        const auto identityText = StringFormat(scratchArena, "render.refraction.capture.draw_{}", drawTaskIndex++);
        const auto identity = ToName(identityText);
        if(!identity || !*identity){
            dependency = {};
            return false;
        }
        Core::GpuTaskDesc desc = TaskDesc(**identity, generate ? "Refraction Capture Generate" : "Refraction Capture Raster", dependency);
        desc.setResourceUses(uses.data(), uses.size()).setResourceSetUses(setUses, setUseCount);
        CaptureDrawTask::Payload payload{arena, materialSystem, targets};
        payload.avboitTargets = targets.avboit;
        payload.frameBindings = frameBindings;
        payload.csgResources = csgResources;
        payload.drawItems.reserve(count);
        payload.drawItems.assign(items, items + count);
        payload.instanceCount = instances.size();
        payload.materialTypedByteCount = typedBytes.size();
        payload.csg = csg;
        payload.renderPath = renderPath;
        payload.generate = generate;
        payload.conservativeGeometryScissor = generate && producesReusableGeometry;
        const auto drawTask = graph.addTask<CaptureDrawTask>(desc, Move(payload));
        if(!drawTask){
            dependency = {};
            return false;
        }
        dependency = *drawTask;
        return true;
    };

    // Emulated draws keep a generator/raster pair so outputs outlive their draw.
    const MaterialPassDrawItems* const orderedSets[] = {&drawItems.regular, &drawItems.csg};
    for(usize setIndex = 0u; setIndex < LengthOf(orderedSets); ++setIndex){
        const MaterialPassDrawItems& items = *orderedSets[setIndex];
        const bool csg = setIndex != 0u;
        if(!appendDraw(items.meshDrawItems.data(), items.meshDrawItems.size(), csg, RenderPath::MeshShader, false, {}))
            return MakeUnexpected(Failure{});
        if(!appendDraw(items.indexedDrawItems.data(), items.indexedDrawItems.size(), csg, RenderPath::VertexIndexed, false, {}))
            return MakeUnexpected(Failure{});
        for(const MaterialPassDrawItem& item : items.computeDrawItems){
            const auto outputResult = ImportBuffer(graph, item.meshResources.emulationVertexBuffer);
            if(!outputResult || !appendDraw(&item, 1u, csg, RenderPath::ComputeEmulation, true, *outputResult) || !appendDraw(&item, 1u, csg, RenderPath::ComputeEmulation, false, *outputResult))
                return MakeUnexpected(Failure{});
        }
    }
    if(hasCsg){
        if(!csgResources.refractionCapFillPipeline)
            return MakeUnexpected(Failure{});
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
        constexpr usize s_CapExtraUseCapacity = 6u;
        uses.reserve(commonUses.size() + csgUses.size() + s_CapExtraUseCapacity);
        uses.assign(commonUses.begin(), commonUses.end());
        uses.insert(uses.end(), csgUses.begin(), csgUses.end());
        uses.push_back(ReadUse(opaqueDepth));
        uses.push_back(ReadWriteUse(depth, Core::ResourceStates::DepthWrite));
        uses.push_back(ReadWriteUse(normalIor, Core::ResourceStates::RenderTarget));
        uses.push_back(ReadWriteUse(tintCoverage, Core::ResourceStates::RenderTarget));
        uses.push_back(ReadWriteUse(instance, Core::ResourceStates::RenderTarget));
        uses.push_back(ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget));
        Core::GpuTaskDesc desc = TaskDesc(Name("render.refraction.capture.caps"), "Refraction Capture Caps", dependency);
        desc.setResourceUses(uses.data(), uses.size());
        if(sampledTextureSet->valid())
            desc.setResourceSetUses(&setUses[1], 1u);
        CaptureCapTask::Payload payload{arena, csgSystem, targets};
        payload.csgResources = csgResources;
        payload.workRect = csgFrameData.workRegion.resolveRect(targets.width, targets.height);
        payload.receiverIndices.reserve(csgFrameData.receiverRanges.size());
        for(usize receiverIndex = 0u; receiverIndex < csgFrameData.receiverRanges.size(); ++receiverIndex){
            if(csgFrameData.receiverRanges[receiverIndex].cutterCount != 0u)
                payload.receiverIndices.push_back(static_cast<u32>(receiverIndex));
        }
        if(payload.receiverIndices.empty())
            return MakeUnexpected(Failure{});
        const auto dependencyResult = graph.addTask<CaptureCapTask>(desc, Move(payload));
        if(!dependencyResult){
            dependency = {};
            return MakeUnexpected(Failure{});
        }
        dependency = *dependencyResult;
    }
    const auto completionResult = finalize();
    if(!completionResult){
        if(producesReusableGeometry)
            generatedGeometry.reset();
        return MakeUnexpected(completionResult.error());
    }
    const Core::GpuTaskId completion = *completionResult;
    if(producesReusableGeometry && !generatedGeometry.publishProducer(completion))
        return MakeUnexpected(Failure{});
    return completion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

