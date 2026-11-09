// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_system.h"
#include "material_pipeline_builder.h"
#include <impl/assets_graphics/mesh_subsystem.adhd.h>
#include <impl/ecs_render/material/csg_shader_variant_builder.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/material_shader_variants_private.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/material/renderer_render_state_private.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets_graphics/components.adhd.h>
#include <impl/assets_material/types.adhd.h>
#include <impl/ecs_csg/shape_registry.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>
#include <core/graphics/shader_stage_names.h>
#include <impl/assets_graphics/csg_subsystem.adhd.h>
#include <core/graphics/backend_selection.h>
#include <impl/ecs_render/avboit/avboit_raster_configuration.h>
#include "material_resource_resolver.h"
#include <impl/ecs_render/material/material_surface_builder.adhd.h>
#include <impl/ecs_render/material/material_instance_override_applier.adhd.h>
#include "sampled_texture_collection.h"
#include <impl/ecs_render/material/instance_typed_ranges.adhd.h>
#include <impl/assets_material/asset.h>
#include <impl/assets_shader/asset.h>
#include <core/assets/manager.h>
#include <core/ecs/world.h>
#include "material_instance_mutable_cache.h"
#include <core/graphics/backend_selection/backend.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_pipeline{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<Core::GraphicsString> ResolveCsgProjectEvaluatorModuleInclude(Core::GraphicsArena& arena, const CsgShapeRegistry& shapeRegistry, const Name& evaluatorVariant){
    if(!evaluatorVariant)
        return Core::GraphicsString(arena);
    auto moduleInclude = shapeRegistry.findShaderModuleInclude(evaluatorVariant, arena);
    if(!moduleInclude || moduleInclude->empty())
        return MakeUnexpected(Failure{});
    return moduleInclude;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<MaterialPipelineResources*>> RendererMaterialSystem::createRendererPipeline(const MaterialSurfaceInfo& materialInfo, const MaterialPipelineKey& pipelineKey, Core::Framebuffer& framebuffer){
    const Name& materialKey = materialInfo.materialName;
    const MaterialPipelinePass::Enum pass = pipelineKey.pass;
    NWB_ASSERT(materialKey);

    auto [it, inserted] = m_materialState.m_pipelines.try_emplace(pipelineKey);
    MaterialPipelineResources& resources = it.value();
    if(resources.ready())
        return MakeNotNull(&resources);

    auto removeFailedEntry = [&]()noexcept{
        if(inserted)
            m_materialState.m_pipelines.erase(it);
    };
    auto failMaterialPipeline = [&]()noexcept{
        removeFailedEntry();
        return MakeUnexpected(Failure{});
    };

    NWB_ASSERT(!materialInfo.shaderVariant.empty());
    const AStringView shaderVariant(materialInfo.shaderVariant.data(), materialInfo.shaderVariant.size());
    Core::GraphicsString csgShaderVariant(m_arena);
    Core::GraphicsString avboitCsgShaderVariant(m_arena);
    const MaterialPipelineCsgBindingUse csgBindingUse = MaterialPipelineResolveCsgBindingUse(pipelineKey.csgMode, pass);
    const b8 csgClipPipeline = csgBindingUse.clip;
    const b8 avboitCsgClipPipeline = csgBindingUse.avboitClip;
    Core::GraphicsString csgProjectEvaluatorModuleAssignment(m_arena);
    AStringView materialProjectEvaluatorModuleAssignmentToAdd;
    AStringView avboitProjectEvaluatorModuleAssignmentToAdd;
    if(csgClipPipeline){
        const auto csgProjectEvaluatorModuleInclude = __hidden_material_pipeline::ResolveCsgProjectEvaluatorModuleInclude(m_arena, m_csgShapeRegistry, pipelineKey.csgEvaluatorVariant);
        if(!csgProjectEvaluatorModuleInclude){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to resolve CSG evaluator module for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgProjectEvaluatorModuleAssignment = CsgShaderVariantBuilder().buildEvaluatorAssignment(AStringView(*csgProjectEvaluatorModuleInclude));
        if(!csgProjectEvaluatorModuleAssignment.empty()){
            const Expected<AStringView> existingEvaluatorModuleAssignment = CsgShaderVariantBuilder().findDefineAssignment(shaderVariant, s_CsgProjectEvaluatorModuleDefineName, Core::ShaderArchive::s_DefaultVariant);
            if(existingEvaluatorModuleAssignment && *existingEvaluatorModuleAssignment != AStringView(csgProjectEvaluatorModuleAssignment)){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' uses a different CSG evaluator module than its active cutters"), ConvertString(materialKey.resolvedText()));
                return failMaterialPipeline();
            }
            if(!existingEvaluatorModuleAssignment)
                materialProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
            avboitProjectEvaluatorModuleAssignmentToAdd = csgProjectEvaluatorModuleAssignment;
        }
    }
    if(csgClipPipeline && !avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(shaderVariant, Core::ShaderArchive::s_DefaultVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build CSG shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(shaderVariant, Core::ShaderArchive::s_DefaultVariant, materialProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG mesh shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        csgShaderVariant = Move(*variant);
    }

    if(avboitCsgClipPipeline){
        auto variant = CsgShaderVariantBuilder().buildVariant(Core::ShaderArchive::s_DefaultVariant, Core::ShaderArchive::s_DefaultVariant, avboitProjectEvaluatorModuleAssignmentToAdd);
        if(!variant){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to build AVBOIT CSG pixel shader variant for material '{}'"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }
        avboitCsgShaderVariant = Move(*variant);
    }
    const AStringView pixelShaderVariant = csgClipPipeline && !avboitCsgClipPipeline ? AStringView(csgShaderVariant) : shaderVariant;
    const AStringView meshShaderVariant = csgClipPipeline ? AStringView(csgShaderVariant) : shaderVariant;

    const Name sharedMeshShaderAssetName = m_shaderSystem.resolveSubsystemAsset<MeshSubsystem>(&MeshSubsystem::sharedMs);
    const Name objectVertexShaderAssetName = m_shaderSystem.resolveSubsystemAsset<MeshSubsystem>(&MeshSubsystem::objectVs);
    const b8 sharedObjectGeometry = UsesSharedMaterialMeshShader(sharedMeshShaderAssetName, materialInfo.meshShader, shaderVariant, Core::ShaderArchive::s_DefaultVariant);

    Name passPixelShaderName = s_NameNone;
    Name passPixelShaderDebugName = s_InvalidPassPixelShaderDebugName;
    MaterialPipelineAvboitPixelShaderSelection avboitPixelShaderSelection;
    if(MaterialPipelinePassUsesRendererAvboit(pass)){
        avboitPixelShaderSelection = SelectAvboitPixelShader(
            pass,
            materialInfo.avboitOccupancyPixelShader,
            materialInfo.avboitExtinctionPixelShader,
            materialInfo.avboitAccumulatePixelShader
        );
        NWB_ASSERT(!materialInfo.transparent || avboitPixelShaderSelection.materialDriven());
    }
    switch(pass){
    case MaterialPipelinePass::Opaque:
    case MaterialPipelinePass::CsgReceiverSurface:
        break;
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
    case MaterialPipelinePass::AvboitAccumulate:
    case MaterialPipelinePass::AvboitRefractionCapture:
        passPixelShaderName = avboitPixelShaderSelection.shaderName();
        passPixelShaderDebugName = avboitPixelShaderSelection.debugName;
        break;
    default:
        break;
    }

    auto& device = m_graphics.getDevice();
    const Core::RenderState renderState = ECSRenderDetail::BuildRenderStateForPass(pass, pipelineKey.twoSided);
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material geometry pipeline requires the global descriptor heap"));
        return failMaterialPipeline();
    }
    const auto materialPassBindingLayout = prepareMaterialPassBindingLayout();
    if(!materialPassBindingLayout)
        return failMaterialPipeline();

    const MaterialPipelineBuildInputs buildInputs{
        .system = *this,
        .shaderSystem = m_shaderSystem,
        .materialState = m_materialState,
        .resources = resources,
        .materialInfo = materialInfo,
        .pipelineKey = pipelineKey,
        .framebuffer = framebuffer,
        .device = device,
        .heap = heap,
        .renderState = renderState,
        .materialPassBindingLayout = *materialPassBindingLayout,
        .materialKey = materialKey,
        .sharedMeshShaderAssetName = sharedMeshShaderAssetName,
        .passPixelShaderName = passPixelShaderName,
        .passPixelShaderDebugName = passPixelShaderDebugName,
        .pixelShaderVariant = pixelShaderVariant,
        .meshShaderVariant = meshShaderVariant,
        .avboitCsgShaderVariant = AStringView(avboitCsgShaderVariant),
        .pass = pass,
        .avboitCsgClipPipeline = avboitCsgClipPipeline,
    };
    MaterialPipelineBuilder pipelineBuilder(buildInputs);

    const b8 meshSupported = m_graphics.queryFeatureSupport(Core::Feature::Meshlets);
    // Surface info admission already requires the mesh shader and the opaque pixel shader.
    NWB_ASSERT(pass != MaterialPipelinePass::Opaque || materialInfo.pixelShader.valid());
    NWB_ASSERT(materialInfo.meshShader.valid());

    if(meshSupported){
        if(!pipelineBuilder.buildMesh()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create the required mesh rendering path for material '{}' on a mesh-capable device"), ConvertString(materialKey.resolvedText()));
            return failMaterialPipeline();
        }

        logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
        return MakeNotNull(&resources);
    }

    const b8 indexedAvailable = sharedObjectGeometry
        && objectVertexShaderAssetName
        && m_shaderSystem.hasShaderArchiveStage(objectVertexShaderAssetName, Core::ShaderArchive::s_DefaultVariant, Core::ShaderStageNames::ArchiveStageNameFromShaderType(Core::ShaderType::VertexStage))
    ;
    if(indexedAvailable){
        if(!pipelineBuilder.buildIndexed())
            return failMaterialPipeline();
    }
    else if(!pipelineBuilder.buildCompute()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute-emulation rendering path for material '{}' from its mesh shader"), ConvertString(materialKey.resolvedText()));
        return failMaterialPipeline();
    }

    logMaterialRenderPathDecision(materialKey, resources.renderPath, meshSupported);
    return MakeNotNull(&resources);
}

Expected<NotNull<MaterialPipelineResources*>> RendererMaterialSystem::findRendererPipeline(const MaterialPipelineKey& pipelineKey)noexcept{
    const auto foundPipeline = m_materialState.m_pipelines.find(pipelineKey);
    if(foundPipeline == m_materialState.m_pipelines.end())
        return MakeUnexpected(Failure{});

    MaterialPipelineResources& resources = foundPipeline.value();
    if(!resources.ready())
        return MakeUnexpected(Failure{});

    return MakeNotNull(&resources);
}

void RendererMaterialSystem::invalidateRendererPipelines()noexcept{
    m_materialState.m_pipelines.clear();
    m_materialState.m_objectGeometryDecodeShader.reset();
    m_materialState.m_objectGeometryDecodePipeline.reset();
    m_materialState.m_objectGeometryInputLayout.reset();
}

void RendererMaterialSystem::logMaterialRenderPathDecision(const Name& materialKey, const RenderPath::Enum renderPath, const b8 meshSupported){
    auto [it, inserted] = m_materialState.m_loggedMaterialPaths.try_emplace(materialKey, renderPath);
    if(!inserted){
        if(it.value() == renderPath)
            return;
        it.value() = renderPath;
    }

    LogMaterialRenderPathDecision(materialKey, renderPath, meshSupported);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 MaterialPipelineBuilder::loadPassPixelShader(){
    if(m_inputs.pass == MaterialPipelinePass::Opaque)
        return m_inputs.shaderSystem.loadShader<PixelShader>(m_inputs.materialInfo.pixelShader.name(), m_inputs.pixelShaderVariant, s_RendererPsDebugName, m_inputs.resources.pixelShader);
    if(m_inputs.pass == MaterialPipelinePass::CsgReceiverSurface)
        return m_inputs.shaderSystem.loadShader<PixelShader>(&CsgSubsystem::receiverSurfacePs, Core::ShaderArchive::s_DefaultVariant, s_CsgReceiverSurfacePsDebugName, m_inputs.resources.pixelShader);
    if(m_inputs.avboitCsgClipPipeline)
        return m_inputs.shaderSystem.loadShader<PixelShader>(m_inputs.passPixelShaderName, AStringView(m_inputs.avboitCsgShaderVariant), m_inputs.passPixelShaderDebugName, m_inputs.resources.pixelShader);
    // AVBOIT pixel shaders are generated per material with its binding/surface contract; load the resolved pass shader at its default variant.
    return m_inputs.shaderSystem.loadShader<PixelShader>(m_inputs.passPixelShaderName, Core::ShaderArchive::s_DefaultVariant, m_inputs.passPixelShaderDebugName, m_inputs.resources.pixelShader);
}

b8 MaterialPipelineBuilder::buildMesh(){
    if(!m_inputs.shaderSystem.loadShader<MeshShader>(m_inputs.materialInfo.meshShader.name(), m_inputs.meshShaderVariant, s_RendererMeshDebugName, m_inputs.resources.meshShader))
        return false;
    if(!loadPassPixelShader())
        return false;

    Core::MeshletPipelineDesc pipelineDesc;
    pipelineDesc.setMeshShader(m_inputs.resources.meshShader);
    pipelineDesc.setPixelShader(m_inputs.resources.pixelShader);
    pipelineDesc.setRenderState(m_inputs.renderState);
    // Set 0 is the shared push-only range; all CSG and AVBOIT resources are selected through the global heap.
    pipelineDesc.addBindingLayout(m_inputs.materialPassBindingLayout);
    // Keep both fixed heap layouts in every mesh pipeline; samplers are frozen surface.
    pipelineDesc.addBindingLayout(m_inputs.heap.getResourceLayout()).addBindingLayout(m_inputs.heap.getSamplerLayout());

    auto meshletPipelineResult = m_inputs.device.createMeshletPipeline(pipelineDesc, m_inputs.framebuffer.getFramebufferInfo());
    if(meshletPipelineResult)
        m_inputs.resources.meshletPipeline = Move(*meshletPipelineResult);
    else
        m_inputs.resources.meshletPipeline.reset();
    if(!meshletPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create meshlet pipeline for material '{}'"), ConvertString(m_inputs.materialKey.resolvedText()));
        return false;
    }

    m_inputs.resources.renderPath = RenderPath::MeshShader;
    return true;
}

b8 MaterialPipelineBuilder::buildIndexed(){
    if(!m_inputs.system.createObjectGeometryPipelineResources(m_inputs.resources))
        return false;
    if(!loadPassPixelShader())
        return false;
    Core::GraphicsPipelineDesc desc;
    desc
        .setInputLayout(m_inputs.materialState.m_objectGeometryInputLayout)
        .setVertexShader(m_inputs.resources.objectGeometryVertexShader)
        .setPixelShader(m_inputs.resources.pixelShader)
        .setRenderState(m_inputs.renderState)
        .addBindingLayout(m_inputs.materialPassBindingLayout)
        .addBindingLayout(m_inputs.heap.getResourceLayout())
        .addBindingLayout(m_inputs.heap.getSamplerLayout())
    ;
    auto indexedPipelineResult = m_inputs.device.createGraphicsPipeline(desc, m_inputs.framebuffer.getFramebufferInfo());
    if(indexedPipelineResult)
        m_inputs.resources.indexedPipeline = Move(*indexedPipelineResult);
    else
        m_inputs.resources.indexedPipeline.reset();
    if(!indexedPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create indexed object geometry pipeline for material '{}'"), ConvertString(m_inputs.materialKey.resolvedText()));
        return false;
    }
    m_inputs.resources.renderPath = RenderPath::VertexIndexed;
    return true;
}

b8 MaterialPipelineBuilder::buildCompute(){
    if(!m_inputs.system.createComputeEmulationResources())
        return false;
    NWB_ASSERT(m_inputs.materialState.m_computeBindingLayout);
    NWB_ASSERT(m_inputs.materialState.m_emulationVertexShader);
    NWB_ASSERT(m_inputs.materialState.m_emulationInputLayout);
    const Name& meshComputeArchiveStageName = ShaderBinaryPayload::s_MeshComputeArchiveStageName;
    if(!m_inputs.shaderSystem.loadShader<ComputeShader>(m_inputs.materialInfo.meshShader.name(), m_inputs.meshShaderVariant, s_RendererCsDebugName, &meshComputeArchiveStageName, m_inputs.resources.computeShader))
        return false;
    if(!loadPassPixelShader())
        return false;
    // The compute-emulation mesh stage shares the mesh-shader source-stream runtime, so it needs the persistent tables too.
    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(m_inputs.device, m_inputs.resources.computeShader, m_inputs.materialState.m_computeBindingLayout, false, m_inputs.resources.computePipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create compute pipeline for material '{}'"), ConvertString(m_inputs.materialKey.resolvedText()));
        return false;
    }

    Core::GraphicsPipelineDesc emulationDesc;
    emulationDesc.setInputLayout(m_inputs.materialState.m_emulationInputLayout);
    emulationDesc.setVertexShader(m_inputs.materialState.m_emulationVertexShader);
    emulationDesc.setPixelShader(m_inputs.resources.pixelShader);
    emulationDesc.setRenderState(m_inputs.renderState);
    emulationDesc.addBindingLayout(m_inputs.materialPassBindingLayout);
    emulationDesc.addBindingLayout(m_inputs.heap.getResourceLayout()).addBindingLayout(m_inputs.heap.getSamplerLayout());
    auto emulationPipelineResult = m_inputs.device.createGraphicsPipeline(emulationDesc, m_inputs.framebuffer.getFramebufferInfo());
    if(emulationPipelineResult)
        m_inputs.resources.emulationPipeline = Move(*emulationPipelineResult);
    else
        m_inputs.resources.emulationPipeline.reset();
    if(!emulationPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create emulation graphics pipeline for material '{}'"), ConvertString(m_inputs.materialKey.resolvedText()));
        m_inputs.resources.computePipeline.reset();
        return false;
    }

    m_inputs.resources.renderPath = RenderPath::ComputeEmulation;
    // The shared mesh_compute program is independent of authored materials.
    m_inputs.resources.sharedGeometryComputeProgram = m_inputs.pipelineKey.csgMode == MaterialPipelineCsgMode::None
        && UsesSharedMaterialMeshShader(m_inputs.sharedMeshShaderAssetName, m_inputs.materialInfo.meshShader, m_inputs.meshShaderVariant, Core::ShaderArchive::s_DefaultVariant)
    ;
    m_inputs.resources.indexedGeometryOutput = m_inputs.resources.sharedGeometryComputeProgram;
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ECSRenderDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::RenderState BuildMeshRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip();
    return renderState;
}

Core::RenderState BuildRenderStateForPass(const MaterialPipelinePass::Enum pass, const b8 twoSided)noexcept{
    auto applyTwoSided = [&](Core::RenderState renderState)noexcept{
        if(twoSided)
            renderState.rasterState.setCullNone();
        return renderState;
    };

    switch(pass){
    case MaterialPipelinePass::Opaque:
        return applyTwoSided(BuildMeshRenderState());
    case MaterialPipelinePass::CsgReceiverSurface:{
        Core::RenderState renderState;
        renderState.depthStencilState.enableDepthTest().disableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
        Core::BlendState::RenderTarget target;
        target.setColorWriteMask(Core::ColorMask::None);
        for(u32 index = 0u; index < Core::s_MaxRenderTargets; ++index)
            renderState.blendState.setRenderTarget(index, target);
        renderState.rasterState.enableDepthClip().setCullNone();
        return renderState;
    }
    case MaterialPipelinePass::AvboitOccupancy:
    case MaterialPipelinePass::AvboitExtinction:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitVoxelRenderState());
    case MaterialPipelinePass::AvboitAccumulate:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitAccumulateRenderState());
    case MaterialPipelinePass::AvboitRefractionCapture:
        return applyTwoSided(AvboitRasterConfiguration::BuildRendererAvboitRefractionCaptureRenderState());
    default:
        return applyTwoSided(BuildMeshRenderState());
    }
}

Core::RenderState BuildCompositeRenderState()noexcept{
    Core::RenderState renderState;
    renderState.depthStencilState.enableDepthTest().enableDepthWrite().setDepthFunc(Core::ComparisonFunc::LessOrEqual);
    renderState.rasterState.enableDepthClip().setCullNone();
    return renderState;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMaterialSystem::ApplyMaterialInstanceOverrides(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent& materialInstance, Span<u8> inOutMutableTypedBytes){
    return MaterialInstanceOverrideApplier().apply(
        entity.id,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutBlocks,
        materialInfo.typedLayoutFields,
        materialInstance,
        inOutMutableTypedBytes
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::createMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset){
    const Name materialPath = materialAsset.name();
    if(!materialPath){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: renderer material is empty"));
        return MakeUnexpected(Failure{});
    }

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo != m_materialState.m_surfaceInfos.end()){
        MaterialSurfaceInfo& info = foundInfo.value();
        if(!RendererMaterialResourceResolver::Resolve(info, m_materialState.m_resourceState, m_graphics, m_assetManager))
            return MakeUnexpected(Failure{});
        return MakeNotNull(&info);
    }

    auto loadedAsset = m_assetManager.loadTypedSync<Material>(materialPath, s_RendererAssetOwnerName);
    if(!loadedAsset)
        return MakeUnexpected(Failure{});

    const Material& material = **loadedAsset;

    MaterialSurfaceInfo createdInfo(m_arena);
    // Material::loadBinary already rejected empty shader variants and missing material interfaces.
    NWB_ASSERT(!material.shaderVariant().empty());
    NWB_ASSERT(material.materialInterface());

    const auto pixelShader = material.findShader<PixelShader>();
    const auto meshShader = material.findShader<MeshShader>();
    const b8 hasPixelShader = pixelShader.has_value();
    const b8 hasMeshShader = meshShader.has_value();
    if(pixelShader)
        createdInfo.pixelShader = *pixelShader;
    if(meshShader)
        createdInfo.meshShader = *meshShader;
    createdInfo.avboitAccumulatePixelShader = material.avboitAccumulatePixelShader();
    createdInfo.avboitOccupancyPixelShader = material.avboitOccupancyPixelShader();
    createdInfo.avboitExtinctionPixelShader = material.avboitExtinctionPixelShader();

    // Material::loadBinary already validated the typed layout (hash, blocks, fields, bytes).
    NWB_ASSERT(material.typedLayoutHash() != 0u && !material.typedBlockBytes().empty());
    NWB_ASSERT(material.typedLayoutBlocks().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedLayoutFields().size() <= static_cast<usize>(Limit<u32>::s_Max));
    NWB_ASSERT(material.typedBlockBytes().size() <= static_cast<usize>(Limit<u32>::s_Max));
    if(!MaterialSurfaceBuilder().populate(
        materialPath,
        AStringView(material.shaderVariant()),
        hasMeshShader,
        hasPixelShader,
        material.transparent(),
        material.materialInterface(),
        material.typedLayoutHash(),
        material.typedLayoutBlocks(),
        material.typedLayoutFields(),
        material.typedBlockBytes(),
        material.resourceReferences(),
        material.shadingModelId(),
        material.surfaceDispatchId(),
        material.twoSided(),
        material.refractive(),
        createdInfo.materialName,
        createdInfo.shaderVariant,
        createdInfo.materialInterface,
        createdInfo.typedLayoutHash,
        createdInfo.typedLayoutBlocks,
        createdInfo.typedLayoutFields,
        createdInfo.resourceReferences,
        createdInfo.constantTypedBytes,
        createdInfo.mutableDefaultTypedBytes,
        createdInfo.unpatchedConstantTypedBytes,
        createdInfo.shadingModelId,
        createdInfo.surfaceDispatchId,
        createdInfo.csgCapSurfaceDispatchAvailable,
        createdInfo.transparent,
        createdInfo.twoSided,
        createdInfo.refractive
    ))
        return MakeUnexpected(Failure{});
    if(!RendererMaterialResourceResolver::Resolve(createdInfo, m_materialState.m_resourceState, m_graphics, m_assetManager))
        return MakeUnexpected(Failure{});

    auto result = m_materialState.m_surfaceInfos.try_emplace(materialPath, Move(createdInfo));
    auto it = result.first;
    return MakeNotNull(&it.value());
}

Expected<NotNull<MaterialSurfaceInfo*>> RendererMaterialSystem::findMaterialSurfaceInfo(const Core::Assets::AssetRef<Material>& materialAsset)noexcept{
    const Name materialPath = materialAsset.name();
    if(!materialPath)
        return MakeUnexpected(Failure{});

    const auto foundInfo = m_materialState.m_surfaceInfos.find(materialPath);
    if(foundInfo == m_materialState.m_surfaceInfos.end())
        return MakeUnexpected(Failure{});

    MaterialSurfaceInfo& materialInfo = foundInfo.value();
    if(!materialInfo.resourceReferencesResolved)
        return MakeUnexpected(Failure{});

    return MakeNotNull(&materialInfo);
}

b8 RendererMaterialSystem::prepareVisibleMaterialSurfaceInfos(){
    b8 hasTransparentRenderers = false;
    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const auto materialInfoResult = createMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        if(materialInfo->transparent)
            hasTransparentRenderers = true;
    }

    return hasTransparentRenderers;
}

void RendererMaterialSystem::prepareVisibleMaterialInstanceMutableCache(){
    m_materialState.m_instanceMutableCache.prune(m_world);

    auto rendererView = m_world.view<RendererComponent>();
    for(auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
        if(!materialInstance || materialInstance->overrides.empty())
            continue;

        const auto materialInfoResult = findMaterialSurfaceInfo(renderer.material);
        if(!materialInfoResult)
            continue;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;

        if(!prepareMaterialInstanceMutableTypedBytes(entity, *materialInfo, materialInstance))
            continue;
    }
}

b8 RendererMaterialSystem::hasTransparentRenderers(const RendererResourceLookupMode::Enum lookupMode){
    auto materialIsTransparent = [&](const Core::Assets::AssetRef<Material>& material) -> b8{
        const auto materialInfoResult = lookupMode == RendererResourceLookupMode::CreateMissing ? createMaterialSurfaceInfo(material) : findMaterialSurfaceInfo(material);
        if(!materialInfoResult)
            return false;
        const NotNull<MaterialSurfaceInfo*> materialInfo = *materialInfoResult;
        return materialInfo->transparent;
    };

    auto rendererView = m_world.view<RendererComponent>();
    for([[maybe_unused]] auto&& [entity, renderer] : rendererView){
        if(!renderer.visible)
            continue;

        if(materialIsTransparent(renderer.material))
            return true;
    }
    return false;
}

Expected<Span<const u8>> RendererMaterialSystem::prepareMaterialInstanceMutableTypedBytes(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance){
    return m_materialState.m_instanceMutableCache.prepare(m_world, entity, materialInfo, materialInstance);
}

Expected<Span<const u8>> RendererMaterialSystem::findPreparedMaterialInstanceMutableTypedBytes(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept{
    return m_materialState.m_instanceMutableCache.find(entity, materialInfo, materialInstance);
}

Expected<ShadowOccluderMaterialContext> RendererMaterialSystem::appendShadowOccluderMaterialContext(
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const NWB::Impl::Scene::TransformComponent* transform,
    MaterialTypedByteDataVector& inOutMaterialTypedBytes,
    ECSRenderDetail::MaterialTypedConstantRangeMap& inOutConstantRanges,
    ECSRenderDetail::MaterialTypedByteContentRangeMap& inOutMutableRanges
){
    const auto constantRange = ECSRenderDetail::FindOrAppendMaterialConstantTypedByteRange(materialInfo.materialName, materialInfo.typedLayoutHash, materialInfo.constantTypedBytes, inOutMaterialTypedBytes, inOutConstantRanges);
    if(!constantRange)
        return MakeUnexpected(Failure{});

    const MaterialInstanceComponent* materialInstance = m_world.tryGetComponent<MaterialInstanceComponent>(entity);
    const auto mutableTypedBytes = prepareMaterialInstanceMutableTypedBytes(entity, materialInfo, materialInstance);
    if(!mutableTypedBytes)
        return MakeUnexpected(Failure{});
    const auto mutableRange = ECSRenderDetail::FindOrAppendMaterialTypedByteRange(*mutableTypedBytes, inOutMaterialTypedBytes, inOutMutableRanges);
    if(!mutableRange)
        return MakeUnexpected(Failure{});

    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*constantRange));
    NWB_ASSERT(MaterialTypedByteRangeEmptyOffsetValid(*mutableRange));
    const NWB::Impl::Scene::TransformComponent defaultTransform;
    const NWB::Impl::Scene::TransformComponent& resolvedTransform = transform ? *transform : defaultTransform;
    ShadowOccluderMaterialContext context;
    context.instance = ::NWB::Impl::BuildInstanceGpuData(entity, transform != nullptr, LoadFloat(resolvedTransform.position), LoadFloat(resolvedTransform.rotation), LoadFloat(resolvedTransform.scale), mutableRange->byteOffset);
    context.constantByteOffset = constantRange->byteOffset;
    return context;
}

void RendererMaterialSystem::releaseMaterialResourceReferences(){
    RendererMaterialResourceResolver::Release(m_graphics, m_materialState.m_resourceState);
    for(auto it = m_materialState.m_surfaceInfos.begin(); it != m_materialState.m_surfaceInfos.end(); ++it){
        MaterialSurfaceInfo& materialInfo = it.value();
        materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
        materialInfo.resourceReferencesResolved = false;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_material_surface{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename CacheT, typename ReleaseFn>
static void ReleaseAssetCache(CacheT& cache, ReleaseFn&& releaseItem)noexcept(
    []()constexpr noexcept{
        using Returned = decltype(DeclVal<CacheT&>().begin());
        using Iterator = Decay_T<Returned>;
        return
            noexcept(DeclVal<CacheT&>().begin())
            && (!IsReference_V<Returned> || IsNothrowConvertible_V<Returned, Iterator>)
            && IsNothrowDestructible_V<Iterator>
            && noexcept(static_cast<b8>(DeclVal<Iterator&>() != DeclVal<CacheT&>().end()))
            && noexcept(++DeclVal<Iterator&>())
#if NWB_OCCUR_ASSERT
            && noexcept(static_cast<b8>(!DeclVal<Iterator&>().value()))
#endif
            && noexcept(DeclVal<ReleaseFn&>()(*DeclVal<Iterator&>().value()))
            && noexcept(DeclVal<CacheT&>().clear())
        ;
    }()
){
    for(auto it = cache.begin(); it != cache.end(); ++it){
        NWB_ASSERT(it.value());
        releaseItem(*it.value());
    }
    cache.clear();
}

static void ReleaseTextureAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.textureAssetCache, [&](TextureGpuResource& resource){ TextureAssetLoader::Release(graphics, resource); });
}

static void ReleaseSamplerAssetCache(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseAssetCache(resources.samplerAssetCache, [&](SamplerGpuResource& resource){ SamplerAssetLoader::Release(graphics, resource); });
}

static void ReleaseMaterialResourceState(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    ReleaseTextureAssetCache(graphics, resources);
    ReleaseSamplerAssetCache(graphics, resources);
}

template<typename AssetT, typename ResourceT, typename CacheT, typename LoadFn>
[[nodiscard]] static Expected<NotNull<ResourceT*>> FindOrCreateCachedAsset(CacheT& cache, const Core::Assets::AssetRef<AssetT>& assetRef, Core::GpuDescriptorHeap& heap, Core::GpuDescriptorHandle ResourceT::* descriptorMember, LoadFn&& loadResource){
    NWB_ASSERT(assetRef.valid());
    const Name& assetPath = assetRef.name();
    auto assetIt = cache.find(assetPath);
    if(assetIt == cache.end()){
        UniquePtr<ResourceT> resource = MakeUnique<ResourceT>();
        Core::GpuDescriptorRetirementGuard retireLoading(heap, resource.get()->*descriptorMember);
        if(!loadResource(*resource, assetRef, assetPath))
            return MakeUnexpected(Failure{});
        const Core::GpuDescriptorHandle pendingDescriptor = resource.get()->*descriptorMember;
        Core::GpuDescriptorRetirementGuard retireUnpublished(heap, pendingDescriptor);
        retireLoading.release();
        auto insertResult = cache.try_emplace(assetPath, Move(resource));
        assetIt = insertResult.first;
        if(insertResult.second)
            retireUnpublished.release();
    }
    NWB_ASSERT(assetIt.value());
    return MakeNotNull(assetIt.value().get());
}

[[nodiscard]] static Expected<u32> ResolveTextureAssetSlot(RendererMaterialResourceState& resources, const Core::Assets::AssetRef<Texture>& textureAsset, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager){
    const auto textureResourceResult = FindOrCreateCachedAsset<Texture, TextureGpuResource>(
        resources.textureAssetCache,
        textureAsset,
        graphics.getDevice().getDescriptorHeap(),
        &TextureGpuResource::sampledImageHeapHandle,
        [&](TextureGpuResource& outResource, const Core::Assets::AssetRef<Texture>& assetRef, const Name& assetPath){
            if(!TextureAssetLoader::Load(assetRef, assetPath, s_RendererAssetOwnerName, graphics, assetManager, outResource))
                return false;
            if(outResource.sampledImageHeapHandle.descriptorClass() != Core::GpuDescriptorClass::SampledImage){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: Texture2D asset '{}' has an incompatible texture dimension"), ConvertString(assetPath.resolvedText()));
                return false;
            }
            return true;
        });
    if(!textureResourceResult)
        return MakeUnexpected(textureResourceResult.error());
    const NotNull<TextureGpuResource*> textureResource = *textureResourceResult;
    NWB_ASSERT(textureResource->valid() && textureResource->sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage);
    return textureResource->sampledImageHeapHandle.slot();
}

[[nodiscard]] static Expected<u32> ResolveSamplerAssetSlot(RendererMaterialResourceState& resources, const Core::Assets::AssetRef<Sampler>& samplerAsset, Core::GraphicsRuntime& graphics, Core::Assets::AssetManager& assetManager){
    const auto samplerResourceResult = FindOrCreateCachedAsset<Sampler, SamplerGpuResource>(
        resources.samplerAssetCache,
        samplerAsset,
        graphics.getDevice().getDescriptorHeap(),
        &SamplerGpuResource::samplerHeapHandle,
        [&](SamplerGpuResource& outResource, const Core::Assets::AssetRef<Sampler>& assetRef, const Name& assetPath){
            return SamplerAssetLoader::Load(assetRef, assetPath, s_RendererAssetOwnerName, graphics, assetManager, outResource);
        });
    if(!samplerResourceResult)
        return MakeUnexpected(samplerResourceResult.error());
    const NotNull<SamplerGpuResource*> samplerResource = *samplerResourceResult;
    NWB_ASSERT(samplerResource->valid() && samplerResource->samplerHeapHandle.descriptorClass() == Core::GpuDescriptorClass::Sampler);
    return samplerResource->samplerHeapHandle.slot();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererMaterialResourceResolver::Resolve(MaterialSurfaceInfo& materialInfo, RendererMaterialResourceState& resources, Core::GraphicsRuntime& graphicsModule, Core::Assets::AssetManager& assetManager){
    if(materialInfo.resourceReferencesResolved)
        return true;

    materialInfo.constantTypedBytes = materialInfo.unpatchedConstantTypedBytes;
    if(materialInfo.resourceReferences.empty()){
        materialInfo.resourceReferencesResolved = true;
        return true;
    }

    Core::GpuDescriptorHeap& heap = graphicsModule.getDevice().getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot resolve material resources without an initialized descriptor heap"));
        return false;
    }

    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        Expected<u32> heapSlot = MakeUnexpected(Failure{});
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:
            heapSlot = __hidden_material_surface::ResolveTextureAssetSlot(resources, resourceReference.textureAsset, graphicsModule, assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load Texture2D asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.textureAsset.name().resolvedText()));
                return false;
            }
            break;
        case MaterialResourceKind::Sampler:
            heapSlot = __hidden_material_surface::ResolveSamplerAssetSlot(resources, resourceReference.samplerAsset, graphicsModule, assetManager);
            if(!heapSlot){
                NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: material '{}' failed to load sampler asset '{}'"), ConvertString(materialInfo.materialName.resolvedText()), ConvertString(resourceReference.samplerAsset.name().resolvedText()));
                return false;
            }
            break;
        default:
            NWB_ASSERT(false);
            return false;
        }

        MaterialInstanceOverrideApplier().patchConstantResourceSlot(resourceReference.constantByteOffset, *heapSlot, materialInfo.constantTypedBytes);
    }

    materialInfo.resourceReferencesResolved = true;
    return true;
}

void RendererMaterialResourceResolver::Release(Core::GraphicsRuntime& graphics, RendererMaterialResourceState& resources){
    __hidden_material_surface::ReleaseMaterialResourceState(graphics, resources);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<Span<const u8>> MaterialInstanceMutableCache::prepare(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const MaterialSurfaceInfo& materialInfo,
    const MaterialInstanceComponent* materialInstance
){
    prune(world);

    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    auto it = m_entries.try_emplace(entity, m_arena).first;
    MaterialInstanceMutableCacheEntry& cacheEntry = it.value();
    if(::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return Span<const u8>(cacheEntry.mutableTypedBytes);

    MaterialSurfaceBuilder().copyTypedBytes(materialInfo.mutableDefaultTypedBytes, cacheEntry.mutableTypedBytes);
    Span<u8> mutableBytes(cacheEntry.mutableTypedBytes);
    if(!MaterialInstanceOverrideApplier().apply(entity.id, materialInfo.materialName, materialInfo.materialInterface, materialInfo.typedLayoutBlocks, materialInfo.typedLayoutFields, *materialInstance, mutableBytes)){
        m_entries.erase(it);
        return MakeUnexpected(Failure{});
    }

    cacheEntry.materialName = materialInfo.materialName;
    cacheEntry.materialInterface = materialInfo.materialInterface;
    cacheEntry.typedLayoutHash = materialInfo.typedLayoutHash;
    cacheEntry.revision = materialInstance->revision;

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

Expected<Span<const u8>> MaterialInstanceMutableCache::find(const Core::ECS::EntityID entity, const MaterialSurfaceInfo& materialInfo, const MaterialInstanceComponent* materialInstance)const noexcept{
    if(!materialInstance || materialInstance->overrides.empty())
        return Span<const u8>(materialInfo.mutableDefaultTypedBytes);

    const auto found = m_entries.find(entity);
    if(found == m_entries.end())
        return MakeUnexpected(Failure{});

    const MaterialInstanceMutableCacheEntry& cacheEntry = found.value();
    if(!::NWB::Impl::MaterialInstanceMutableCacheEntryMatches(
        cacheEntry.materialName,
        cacheEntry.materialInterface,
        cacheEntry.typedLayoutHash,
        cacheEntry.revision,
        materialInfo.materialName,
        materialInfo.materialInterface,
        materialInfo.typedLayoutHash,
        *materialInstance
    ))
        return MakeUnexpected(Failure{});

    return Span<const u8>(cacheEntry.mutableTypedBytes);
}

void MaterialInstanceMutableCache::prune(Core::ECS::World& world)noexcept{
    const u64 componentMutationVersion = world.componentMutationVersion<MaterialInstanceComponent>();
    if(componentMutationVersion == m_componentMutationVersion)
        return;

    m_entries.clear();
    m_componentMutationVersion = componentMutationVersion;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 AppendPreparedMaterialSurfaceSampledTextures(const MaterialSurfaceInfo& materialInfo, const RendererMaterialResourceState& resources, MaterialSampledTextureCollector<Core::Alloc::ScratchArena>& collector){
    if(!materialInfo.resourceReferencesResolved)
        return false;
    for(const MaterialResourceReference& resourceReference : materialInfo.resourceReferences){
        switch(resourceReference.resourceKind){
        case MaterialResourceKind::SampledImage2D:{
            // Resolved references own valid 2D cache entries until release clears the flag.
            NWB_ASSERT(resourceReference.textureAsset.valid());
            const auto foundTexture = resources.textureAssetCache.find(resourceReference.textureAsset.name());
            NWB_ASSERT(foundTexture != resources.textureAssetCache.end() && foundTexture.value());

            const TextureGpuResource& textureResource = *foundTexture.value();
            NWB_ASSERT(textureResource.valid() && textureResource.sampledImageHeapHandle.descriptorClass() == Core::GpuDescriptorClass::SampledImage);

            collector.append(textureResource.texture);
            break;
        }
        case MaterialResourceKind::Sampler:
            break;
        default:
            NWB_ASSERT(false);
            return false;
        }
    }
    return true;
}

Expected<Vector<Core::TextureHandle, Core::Alloc::ScratchArena>> GatherPreparedMaterialPassSampledTextures(
    const MaterialSurfaceInfoMap& materials,
    const RendererMaterialResourceState& resources,
    const Span<const MaterialPassDrawItemsView> drawItemSets,
    Core::Alloc::ScratchArena& scratchArena
){
    Vector<Core::TextureHandle, Core::Alloc::ScratchArena> textures(scratchArena);

    MaterialSampledTextureCollector<Core::Alloc::ScratchArena> collector(textures, scratchArena);
    const auto appendDrawItem = [&](const MaterialPassDrawItem& drawItem){
        const auto foundMaterial = materials.find(drawItem.pipelineKey.material);
        return foundMaterial != materials.end() && AppendPreparedMaterialSurfaceSampledTextures(foundMaterial.value(), resources, collector);
    };

    for(const MaterialPassDrawItemsView& drawItems : drawItemSets){
        for(const MaterialPassDrawItem& drawItem : drawItems.meshDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
        for(const MaterialPassDrawItem& drawItem : drawItems.indexedDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
        for(const MaterialPassDrawItem& drawItem : drawItems.computeDrawItems){
            if(!appendDrawItem(drawItem))
                return MakeUnexpected(Failure{});
        }
    }
    return textures;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
