// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_pipelines.h"
#include "reflection_system.h"

#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <impl/assets/graphics/reflection/depth_constants.h>
#include <impl/assets/graphics/reflection/frame_constants.h>

#include <core/graphics/backend_selection/backend.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererReflectionPipelines::prepare(const b8 prepareHardware, const b8 prepareCsgHardware, ReflectionFrameSnapshot& resources){
    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(
        !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, sizeof(u32), Core::ShaderType::Compute, m_bindingLayout)
        || !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, NWB_REFLECTION_DEPTH_PUSH_CONSTANT_BYTES, Core::ShaderType::Compute, m_depthBindingLayout)
    )
        return false;
    const auto preparePipeline = [&](
        Core::ComputePipelineHandle& pipeline,
        Core::ShaderHandle& shader,
        const Core::Assets::AssetRef<ComputeShader> ReflectionSubsystem::* member,
        const Name debugName,
        const b8 hardware,
        const Core::BindingLayoutHandle& bindingLayout,
        const AStringView variant = Core::ShaderArchive::s_DefaultVariant
    ){
        if(pipeline)
            return true;
        if(!m_shaders.loadShader<ComputeShader>(member, variant, debugName, shader))
            return false;
        return RendererComputePipelineDetail::CreateHeapComputePipeline(device, shader, bindingLayout, hardware, pipeline);
    };
    if(
        !preparePipeline(resources.classifyPipeline, m_classifyShader, &ReflectionSubsystem::classifyCs, Name("ECSRender_ReflectionClassify"), false, m_bindingLayout)
        || !preparePipeline(resources.buildArgsPipeline, m_buildArgsShader, &ReflectionSubsystem::buildArgsCs, Name("ECSRender_ReflectionBuildArgs"), false, m_bindingLayout, s_ReflectionBuildArgsVariant)
        || !preparePipeline(resources.depthPyramid.pipeline, m_depthShader, &ReflectionSubsystem::depthReduceCs, Name("ECSRender_ReflectionDepthReduce"), false, m_depthBindingLayout)
    )
        return false;
    if(
        prepareHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout()
        && (!preparePipeline(resources.hardwarePipeline, m_hardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionOpticalHardware"), true, m_bindingLayout, s_ReflectionHwOpticalVariant) || !preparePipeline(
            m_plainHardwarePipeline, m_plainHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionHardware"), true, m_bindingLayout,
            s_ReflectionHwPlainVariant
        ) || !preparePipeline(m_unspecifiedHardwarePipeline, m_unspecifiedHardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionUnspecifiedOpticalHardware"), true, m_bindingLayout, s_ReflectionHwUnspecifiedVariant))
    )
        return false;
    const b8 csgHardware = prepareCsgHardware && m_graphics.queryFeatureSupport(Core::Feature::RayQuery) && heap.hasAccelStructLayout();
    if(csgHardware && !RendererComputePipelineDetail::EnsurePushConstantBindingLayout(m_arena, device, NWB_REFLECTION_CSG_PUSH_CONSTANT_BYTES, Core::ShaderType::Compute, m_csgBindingLayout))
        return false;
    if(
        csgHardware
        && (!preparePipeline(m_csgBuildArgsPipeline, m_csgBuildArgsShader, &ReflectionSubsystem::buildArgsCs, Name("ECSRender_ReflectionCsgBuildArgs"), false, m_csgBindingLayout, s_ReflectionCsgBuildArgsVariant) || !preparePipeline(
            m_csgHardwarePipeline, m_csgHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgOpticalHardware"), true, m_csgBindingLayout,
            s_ReflectionHwCsgOpticalVariant
        ) || !preparePipeline(m_csgPlainHardwarePipeline, m_csgPlainHardwareShader, &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgHardware"), true, m_csgBindingLayout, s_ReflectionHwCsgPlainVariant) || !preparePipeline(
            m_csgUnspecifiedHardwarePipeline, m_csgUnspecifiedHardwareShader,
            &ReflectionSubsystem::resolveHwCs, Name("ECSRender_ReflectionCsgUnspecifiedOpticalHardware"), true, m_csgBindingLayout,
            s_ReflectionHwCsgUnspecifiedVariant
        ))
    )
        return false;
    return true;
}


void RendererReflectionPipelines::invalidate(ReflectionFrameSnapshot& resources){
    resources.classifyPipeline = nullptr;
    resources.buildArgsPipeline = nullptr;
    resources.hardwarePipeline = nullptr;
    resources.depthPyramid.pipeline = nullptr;
    m_classifyShader = nullptr;
    m_buildArgsShader = nullptr;
    m_hardwareShader = nullptr;
    m_plainHardwareShader = nullptr;
    m_plainHardwarePipeline = nullptr;
    m_unspecifiedHardwareShader = nullptr;
    m_unspecifiedHardwarePipeline = nullptr;
    m_csgBuildArgsShader = nullptr;
    m_csgBuildArgsPipeline = nullptr;
    m_csgHardwareShader = nullptr;
    m_csgPlainHardwareShader = nullptr;
    m_csgUnspecifiedHardwareShader = nullptr;
    m_csgHardwarePipeline = nullptr;
    m_csgPlainHardwarePipeline = nullptr;
    m_csgUnspecifiedHardwarePipeline = nullptr;
    m_depthShader = nullptr;
    m_bindingLayout = nullptr;
    m_depthBindingLayout = nullptr;
    m_csgBindingLayout = nullptr;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

