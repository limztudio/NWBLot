// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_refraction_capture.h"
#include "refraction_cap_builder.h"
#include "refraction_capture_resources.h"
#include "generated_geometry_reuse.h"

#include <impl/ecs_render/kernel/task_graph_scheduling.h>
#include <impl/ecs_render/material/task_graph_object_geometry_cache.h>
#include <impl/ecs_render/csg/csg_system.h>
#include <impl/ecs_render/material/material_system.h>
#include <impl/ecs_render/material/task_graph_resource_sets.h>
#include <impl/ecs_render/mesh/mesh_view_private.h>
#include <impl/ecs_render/avboit/avboit_frame_targets.h>

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
    return FindOrImportTexture(graph, texture, "Refraction Capture Texture");
}

[[nodiscard]] Expected<Core::GpuGraphResourceId> ImportBuffer(Core::GpuTaskGraph& graph, const Core::BufferHandle& buffer){
    return FindOrImportBuffer(graph, buffer, "Refraction Capture Buffer");
}

struct CaptureDrawTask{
    struct Payload{
        RendererMaterialSystem& materialSystem;
        AvboitFrameTargets avboitTargets;
        ECSRenderDetail::MeshFrameBindingSnapshot frameBindings;
        ECSRenderDetail::CsgGraphResourceSnapshot csgResources;
        Vector<MaterialPassDrawItem, Core::Alloc::GlobalArena> drawItems;
        usize instanceCount = 0u;
        usize materialTypedByteCount = 0u;
        b8 csg = false;
        RenderPath::Enum renderPath = RenderPath::MeshShader;
        b8 generate = false;
        b8 conservativeGeometryScissor = false;


    public:
        explicit Payload(Core::Alloc::GlobalArena& arena, RendererMaterialSystem& materialSystemIn)noexcept
            : materialSystem(materialSystemIn)
            , drawItems(arena)
        {}
    };

    [[nodiscard]] static constexpr Core::GpuTaskCommandRequirements CommandRequirements(const Payload& payload)noexcept{
        return RendererTaskGraphDetail::SharedComputeEmulationCommandRequirements(payload.generate);
    }

    [[nodiscard]] static b8 Record(const Payload& payload, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
        if(!payload.avboitTargets.refractionFramebuffer || !payload.frameBindings.frameReady(payload.instanceCount, payload.materialTypedByteCount) || (payload.csg && !payload.csgResources.bindingValid()))
            return false;

        const Span<const MaterialPassDrawItem> drawItems(payload.drawItems);
        b8 resourcesReady = false;
        switch(payload.renderPath){
        case RenderPath::MeshShader:
            resourcesReady = payload.materialSystem.drawRecorder().meshMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::VertexIndexed:
            resourcesReady = payload.materialSystem.drawRecorder().indexedMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
            break;
        case RenderPath::ComputeEmulation:
            resourcesReady = payload.materialSystem.drawRecorder().computeMaterialPassDrawResourcesReady(drawItems, payload.frameBindings);
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
            payload.materialSystem.drawRecorder().generateComputeMaterialPassDrawItems(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::ComputeEmulation)
            payload.materialSystem.drawRecorder().renderComputeMaterialPassDrawItemsRasterOnly(drawContext, drawItems);
        else if(payload.renderPath == RenderPath::VertexIndexed)
            payload.materialSystem.drawRecorder().renderIndexedMaterialPassDrawItems(drawContext, drawItems);
        else
            payload.materialSystem.drawRecorder().renderMeshMaterialPassDrawItems(drawContext, drawItems);
        commandList.endRenderPass();
        return true;
    }
};

struct FinalizeTask{
    static constexpr Core::GpuTaskCommandRequirements s_CommandRequirements = {};


public:
    struct Payload{};


public:
    [[nodiscard]] static b8 Record(const Payload&, Core::CommandList& commandList, const Core::GpuTaskRecordContext&){
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
    const CsgFrameState& csgFrameState,
    const ECSRenderDetail::CsgGraphResourceSnapshot& csgResources,
    const ECSRenderDetail::MeshFrameBindingSnapshot& frameBindings,
    const MeshViewGpuData& meshViewState,
    Core::GpuTaskId dependency,
    const b8 enabled,
    DeferredFrameTargets& targets,
    ObjectGeometryCacheGraph& objectGeometry,
    AvboitGeneratedGeometryReuse& generatedGeometry
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

    const auto appendClear = [&](const Name identity, const Core::GpuGraphResourceId destination, const b8 isDepth, const Core::Color color = Core::Color(0.f, 0.f, 0.f, 0.f)){
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
    if(
        !appendClear(Name("render.refraction.clear.depth"), depth, true)
        || !appendClear(Name("render.refraction.clear.normal_ior"), normalIor, false)
        || !appendClear(Name("render.refraction.clear.tint_coverage"), tintCoverage, false)
        || !appendClear(Name("render.refraction.clear.instance"), instance, false)
        || !appendClear(Name("render.refraction.clear.specular_roughness"), specularRoughness, false, Core::Color(0.f, 0.f, 0.f, 1.f))
    )
        return MakeUnexpected(Failure{});

    const auto finalize = [&](){
        const Array<Core::GpuTaskResourceUse, 5u> uses = { ReadUse(depth), ReadUse(normalIor), ReadUse(tintCoverage), ReadUse(instance), ReadUse(specularRoughness) };
        Core::GpuTaskDesc desc = TaskDesc(Name("render.refraction.capture_finalize"), "Refraction Capture Finalize", dependency);
        desc.setResourceUses(uses);
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
        csgFrameState, RendererResourceLookupMode::PreparedOnly, &meshViewState,
        drawItems, instances, csgFrameData,
#if defined(NWB_DEBUG)
        typedRanges,
#endif
        typedBytes
    );
    if(drawItems.empty())
        return finalize();
    const b8 hasCsg = !drawItems.csg.empty();
    if(
        !frameBindings.frameReady(instances.size(), typedBytes.size())
        || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.regular, frameBindings)
        || (hasCsg && (!csgFrameData.hasWork() || !csgResources.frameReady(csgFrameData) || !materialSystem.drawRecorder().materialPassDrawResourcesReady(drawItems.csg, frameBindings)))
    )
        return MakeUnexpected(Failure{});

    const Array<MaterialPassDrawItemsView, 2u> drawSets = {drawItems.regular, drawItems.csg};
    const auto geometrySet = GatherPreparedMaterialGeometryResourceSet(drawSets, Name("render.refraction.capture.geometry"), "Refraction Capture Geometry", graph, scratchArena);
    if(!geometrySet)
        return MakeUnexpected(Failure{});
    const auto sampledTextureSet = GatherPreparedMaterialSampledTextureResourceSet(materialSystem, drawSets, Name("render.refraction.capture.material_textures"), "Refraction Capture Material Textures", graph, scratchArena);
    if(!sampledTextureSet)
        return MakeUnexpected(Failure{});
    const Array<Core::GpuTaskResourceSetUse, 2u> setUses = {{
        {.resourceSet = *geometrySet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
        {.resourceSet = *sampledTextureSet, .range = {}, .requiredState = Core::ResourceStates::ShaderResource, .access = Core::GpuTaskResourceAccess::Read},
    }};
    const usize setUseCount = sampledTextureSet->valid() ? s_RefractionCaptureGeometryAndTextureSetUseCount : 1u;

    materialSystem.prepareMaterialPassInstanceUploadData(csgResources, instances);
    const b8 producesReusableGeometry = generatedGeometry.capture(drawItems, instances, frameBindings, meshViewState, MaterialPipelinePass::AvboitRefractionCapture);
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

    AvboitRefractionCaptureResources captureResources(graph, dependency);
    if(
        !captureResources.appendUpload(Name("render.refraction.capture.instances_upload"), materialInstances, instances.data(), instances.size() * sizeof(InstanceGpuData), alignof(InstanceGpuData))
        || !captureResources.appendUpload(Name("render.refraction.capture.material_upload"), materialTyped, typedBytes.data(), typedBytes.size(), alignof(u32))
    )
        return MakeUnexpected(Failure{});

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> commonUses{scratchArena};
    commonUses.reserve(s_RefractionCaptureCommonUseCapacity);
    commonUses.push_back(ReadBufferUse(materialInstances, Core::BufferRange(0u, instances.size() * sizeof(InstanceGpuData))));
    commonUses.push_back(ReadBufferUse(materialTyped, Core::BufferRange(0u, typedBytes.size())));
    commonUses.push_back(ReadUse(meshView, Core::ResourceStates::ConstantBuffer));
    commonUses.push_back(ReadUse(slots, Core::ResourceStates::ConstantBuffer));

    Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> csgUses{scratchArena};
    if(hasCsg && !captureResources.appendCsg(csgSystem, targets, csgFrameData, csgResources, frameBindings, csgUses))
        return MakeUnexpected(Failure{});

    if(!objectGeometry.prepare(drawItems.regular.indexedDrawItems, frameBindings, scratchArena, dependency, commonUses))
        return MakeUnexpected(Failure{});
    if(!objectGeometry.prepare(drawItems.csg.indexedDrawItems, frameBindings, scratchArena, dependency, commonUses))
        return MakeUnexpected(Failure{});

    usize drawTaskIndex = 0u;
    const auto appendDraw = [&](const Span<const MaterialPassDrawItem> items, const b8 csg, const RenderPath::Enum renderPath, const b8 generate, const Core::GpuGraphResourceId output){
        if(items.empty())
            return true;
        Vector<Core::GpuTaskResourceUse, Core::Alloc::ScratchArena> uses{scratchArena};
        uses.reserve(commonUses.size() + (csg ? csgUses.size() : 0u) + s_RefractionCaptureDrawExtraUseCapacity);
        uses.assign(commonUses.begin(), commonUses.end());
        if(csg)
            uses.insert(uses.end(), csgUses.begin(), csgUses.end());
        if(renderPath == RenderPath::ComputeEmulation)
            uses.push_back(generate ? WriteUse(output, Core::ResourceStates::UnorderedAccess) : ReadUse(output, ECSRenderDetail::s_ObjectGeometryRasterState));
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
        desc.setResourceUses(uses).setResourceSetUses({ setUses.data(), setUseCount });
        CaptureDrawTask::Payload payload{arena, materialSystem};
        payload.avboitTargets = targets.avboit;
        payload.frameBindings = frameBindings;
        payload.csgResources = csgResources;
        payload.drawItems.reserve(items.size());
        payload.drawItems.assign(items.begin(), items.end());
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
    const Array<MaterialPassDrawItemsView, 2u> orderedSets = {drawItems.regular, drawItems.csg};
    for(usize setIndex = 0u; setIndex < LengthOf(orderedSets); ++setIndex){
        const MaterialPassDrawItemsView& items = orderedSets[setIndex];
        const b8 csg = setIndex != 0u;
        if(!appendDraw(items.meshDrawItems, csg, RenderPath::MeshShader, false, {}))
            return MakeUnexpected(Failure{});
        if(!appendDraw(items.indexedDrawItems, csg, RenderPath::VertexIndexed, false, {}))
            return MakeUnexpected(Failure{});
        for(const MaterialPassDrawItem& item : items.computeDrawItems){
            const auto outputResult = ImportBuffer(graph, item.meshResources.emulationVertexBuffer);
            const Span<const MaterialPassDrawItem> singleItem(&item, 1u);
            if(!outputResult || !appendDraw(singleItem, csg, RenderPath::ComputeEmulation, true, *outputResult) || !appendDraw(singleItem, csg, RenderPath::ComputeEmulation, false, *outputResult))
                return MakeUnexpected(Failure{});
        }
    }
    if(hasCsg){
        const AvboitRefractionCapInputs capInputs{
            scratchArena, graph, arena, csgSystem, targets, csgResources, csgFrameData,
            commonUses, csgUses, opaqueDepth, depth, normalIor, tintCoverage, instance, specularRoughness,
            sampledTextureSet->valid() ? &setUses[1] : nullptr, dependency
        };
        const auto capResult = AvboitRefractionCapBuilder::Declare(capInputs);
        if(!capResult)
            return MakeUnexpected(capResult.error());
        dependency = *capResult;
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

