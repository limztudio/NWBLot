// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "material_pipeline_builder.h"

#include <impl/assets_graphics/csg_subsystem.adhd.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/material/material_shader_variants_private.h>
#include <impl/ecs_render/material/renderer_material_state.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets_graphics/components.adhd.h>

#include <core/common/log.h>
#include <core/graphics/backend_selection.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


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


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

