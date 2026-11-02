// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_surfel_pipelines{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] static b8 EnsureSurfelComputePipeline(
    Core::Alloc::GlobalArena& arena,
    Core::GraphicsBackend::Device& device,
    RendererShaderSystem& shaderSystem,
    const Core::Assets::AssetRef<ComputeShader> GiSubsystem::* member,
    const AStringView variantName,
    const Name& debugName,
    const b8 includeAccelStruct,
    const TStringView passLabel,
    Core::BindingLayoutHandle& inOutLayout,
    Core::ShaderHandle& inOutShader,
    Core::ComputePipelineHandle& inOutPipeline,
    b8& inOutFailed
){
    if(inOutPipeline)
        return true;
    if(inOutFailed)
        return false;

    const Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || (includeAccelStruct && !heap.hasAccelStructLayout())){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel {} requires the initialized global descriptor heap"), passLabel);
        inOutFailed = true;
        return false;
    }
    if(!RendererComputePipelineDetail::EnsurePushConstantBindingLayout(arena, device, sizeof(SurfelHeapPushConstants), Core::ShaderType::Compute, inOutLayout)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel {} binding layout"), passLabel);
        inOutFailed = true;
        return false;
    }
    if(!shaderSystem.loadShader<ComputeShader>(member, variantName, debugName, inOutShader)){
        inOutFailed = true;
        return false;
    }
    if(!RendererComputePipelineDetail::CreateHeapComputePipeline(device, inOutShader, inOutLayout, includeAccelStruct, inOutPipeline)){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel {} compute pipeline"), passLabel);
        inOutFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel {} compute pipeline"), passLabel);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelSpawnCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelSpawn",
        false,
        NWB_TEXT("spawn"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelSpawnShader,
        m_rayTracingState.m_surfelSpawnPipeline,
        m_rayTracingState.m_surfelSpawnPipelineFailed
    );
}

b8 RendererRayTracingSystem::ensureSurfelAgeFreePipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelAgeFreeCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelAgeFree",
        false,
        NWB_TEXT("age-free"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelAgeFreeShader,
        m_rayTracingState.m_surfelAgeFreePipeline,
        m_rayTracingState.m_surfelAgeFreePipelineFailed
    );
}

b8 RendererRayTracingSystem::ensureSurfelHashBuildPipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelHashBuildCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelHashBuild",
        false,
        NWB_TEXT("hash-build"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelHashBuildShader,
        m_rayTracingState.m_surfelHashBuildPipeline,
        m_rayTracingState.m_surfelHashBuildPipelineFailed
    );
}

b8 RendererRayTracingSystem::ensureSurfelTracePipeline(){
    const b8 csg = m_rayTracingState.m_surfelUseCsgTrace;
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelTraceCs,
        csg ? s_GiCsgEnabledVariant : s_GiCsgDisabledVariant,
        "ECSRender_SurfelTrace",
        false,
        NWB_TEXT("trace"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        csg ? m_rayTracingState.m_surfelTraceCsgShader : m_rayTracingState.m_surfelTraceShader,
        csg ? m_rayTracingState.m_surfelTraceCsgPipeline : m_rayTracingState.m_surfelTracePipeline,
        csg ? m_rayTracingState.m_surfelTraceCsgPipelineFailed : m_rayTracingState.m_surfelTracePipelineFailed
    );
}

// Hardware trace twin; other surfel passes are shared.
b8 RendererRayTracingSystem::ensureSurfelTraceHwPipeline(){
    const b8 csg = m_rayTracingState.m_surfelUseCsgTrace;
    auto& pipeline = csg ? m_rayTracingState.m_surfelTraceCsgHwPipeline : m_rayTracingState.m_surfelTraceHwPipeline;
    auto& shader = csg ? m_rayTracingState.m_surfelTraceCsgHwShader : m_rayTracingState.m_surfelTraceHwShader;
    auto& failed = csg ? m_rayTracingState.m_surfelTraceCsgHwPipelineFailed : m_rayTracingState.m_surfelTraceHwPipelineFailed;

    if(pipeline)
        return true;
    if(failed)
        return false;
    if(!m_graphics.queryFeatureSupport(Core::Feature::RayTracingAccelStruct) || !m_graphics.queryFeatureSupport(Core::Feature::RayQuery)){
        failed = true;
        return false;
    }

    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelTraceHwCs,
        csg ? s_GiCsgEnabledVariant : s_GiCsgDisabledVariant,
        "ECSRender_SurfelTraceHw",
        true,
        NWB_TEXT("HW trace"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        shader,
        pipeline,
        failed
    );
}

b8 RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelResolveCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelResolve",
        false,
        NWB_TEXT("resolve"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelResolveShader,
        m_rayTracingState.m_surfelResolvePipeline,
        m_rayTracingState.m_surfelResolvePipelineFailed
    );
}

b8 RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelUpsampleCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelUpsample",
        false,
        NWB_TEXT("upsample"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelUpsampleShader,
        m_rayTracingState.m_surfelUpsamplePipeline,
        m_rayTracingState.m_surfelUpsamplePipelineFailed
    );
}

b8 RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelTraceBuildArgsCs,
        Core::ShaderArchive::s_DefaultVariant,
        "ECSRender_SurfelTraceBuildArgs",
        false,
        NWB_TEXT("trace build-args"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        m_rayTracingState.m_surfelTraceBuildArgsShader,
        m_rayTracingState.m_surfelTraceBuildArgsPipeline,
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

