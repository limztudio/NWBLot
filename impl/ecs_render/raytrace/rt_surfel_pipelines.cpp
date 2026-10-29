// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_surfel_tasks.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_rt_surfel_pipelines{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static constexpr AStringView s_GiCsgDisabledVariant = "NWB_GI_CSG_ENABLED=0";
static constexpr AStringView s_GiCsgEnabledVariant = "NWB_GI_CSG_ENABLED=1";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::ensureSurfelSpawnPipeline(){
    if(m_rayTracingState.m_surfelSpawnPipeline)
        return true;
    if(m_rayTracingState.m_surfelSpawnPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel spawn requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelSpawnBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelSpawnBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelSpawnBindingLayoutResult)
            m_rayTracingState.m_surfelSpawnBindingLayout = Move(*surfelSpawnBindingLayoutResult);
        else
            m_rayTracingState.m_surfelSpawnBindingLayout.reset();
        if(!surfelSpawnBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn binding layout"));
            m_rayTracingState.m_surfelSpawnPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelSpawnShader, &GiSubsystem::surfelSpawnCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelSpawn")){
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelSpawnShader).addBindingLayout(m_rayTracingState.m_surfelSpawnBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelSpawnPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelSpawnPipelineResult)
        m_rayTracingState.m_surfelSpawnPipeline = Move(*surfelSpawnPipelineResult);
    else
        m_rayTracingState.m_surfelSpawnPipeline.reset();
    if(!surfelSpawnPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel spawn compute pipeline"));
        m_rayTracingState.m_surfelSpawnPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel spawn compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelAgeFreePipeline(){
    if(m_rayTracingState.m_surfelAgeFreePipeline)
        return true;
    if(m_rayTracingState.m_surfelAgeFreePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel age-free requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelAgeFreeBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelAgeFreeBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelAgeFreeBindingLayoutResult)
            m_rayTracingState.m_surfelAgeFreeBindingLayout = Move(*surfelAgeFreeBindingLayoutResult);
        else
            m_rayTracingState.m_surfelAgeFreeBindingLayout.reset();
        if(!surfelAgeFreeBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free binding layout"));
            m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelAgeFreeShader, &GiSubsystem::surfelAgeFreeCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelAgeFree")){
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelAgeFreeShader).addBindingLayout(m_rayTracingState.m_surfelAgeFreeBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelAgeFreePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelAgeFreePipelineResult)
        m_rayTracingState.m_surfelAgeFreePipeline = Move(*surfelAgeFreePipelineResult);
    else
        m_rayTracingState.m_surfelAgeFreePipeline.reset();
    if(!surfelAgeFreePipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel age-free compute pipeline"));
        m_rayTracingState.m_surfelAgeFreePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel age-free compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelHashBuildPipeline(){
    if(m_rayTracingState.m_surfelHashBuildPipeline)
        return true;
    if(m_rayTracingState.m_surfelHashBuildPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel hash-build requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelHashBuildBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelHashBuildBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelHashBuildBindingLayoutResult)
            m_rayTracingState.m_surfelHashBuildBindingLayout = Move(*surfelHashBuildBindingLayoutResult);
        else
            m_rayTracingState.m_surfelHashBuildBindingLayout.reset();
        if(!surfelHashBuildBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build binding layout"));
            m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelHashBuildShader, &GiSubsystem::surfelHashBuildCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelHashBuild")){
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelHashBuildShader).addBindingLayout(m_rayTracingState.m_surfelHashBuildBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelHashBuildPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelHashBuildPipelineResult)
        m_rayTracingState.m_surfelHashBuildPipeline = Move(*surfelHashBuildPipelineResult);
    else
        m_rayTracingState.m_surfelHashBuildPipeline.reset();
    if(!surfelHashBuildPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel hash-build compute pipeline"));
        m_rayTracingState.m_surfelHashBuildPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel hash-build compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelTracePipeline(){
    const b8 csg = m_rayTracingState.m_surfelUseCsgTrace;
    auto& pipeline = csg ? m_rayTracingState.m_surfelTraceCsgPipeline : m_rayTracingState.m_surfelTracePipeline;
    auto& shader = csg ? m_rayTracingState.m_surfelTraceCsgShader : m_rayTracingState.m_surfelTraceShader;
    auto& failed = csg ? m_rayTracingState.m_surfelTraceCsgPipelineFailed : m_rayTracingState.m_surfelTracePipelineFailed;

    if(pipeline)
        return true;
    if(failed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace requires the initialized global descriptor heap"));
        failed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelTraceBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceBindingLayoutResult)
            m_rayTracingState.m_surfelTraceBindingLayout = Move(*surfelTraceBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceBindingLayout.reset();
        if(!surfelTraceBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace binding layout"));
            failed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(shader, &GiSubsystem::surfelTraceCs, csg ? __hidden_rt_surfel_pipelines::s_GiCsgEnabledVariant : __hidden_rt_surfel_pipelines::s_GiCsgDisabledVariant, "ECSRender_SurfelTrace")){
        failed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_surfelTraceBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto pipelineResult = device.createComputePipeline(pipelineDesc);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    if(!pipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace compute pipeline"));
        failed = true;
        return false;
    }
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: created surfel trace compute pipeline"));
    return true;
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

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized() || !heap.hasAccelStructLayout()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel HW trace requires the descriptor-buffer TLAS heap layout"));
        failed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceHwBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelTraceHwBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceHwBindingLayoutResult)
            m_rayTracingState.m_surfelTraceHwBindingLayout = Move(*surfelTraceHwBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceHwBindingLayout.reset();
        if(!surfelTraceHwBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace binding layout"));
            failed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(shader, &GiSubsystem::surfelTraceHwCs, csg ? __hidden_rt_surfel_pipelines::s_GiCsgEnabledVariant : __hidden_rt_surfel_pipelines::s_GiCsgDisabledVariant, "ECSRender_SurfelTraceHw")){
        failed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(shader).addBindingLayout(m_rayTracingState.m_surfelTraceHwBindingLayout);
    // Preserve resource, sampler, and TLAS heap sets for hardware trace.
    pipelineDesc.addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout()).addBindingLayout(heap.getAccelStructLayout());
    auto pipelineResult = device.createComputePipeline(pipelineDesc);
    if(pipelineResult)
        pipeline = Move(*pipelineResult);
    else
        pipeline.reset();
    if(!pipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel HW trace compute pipeline"));
        failed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel HW trace compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelResolvePipeline(){
    if(m_rayTracingState.m_surfelResolvePipeline)
        return true;
    if(m_rayTracingState.m_surfelResolvePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel resolve requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelResolveBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelResolveBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelResolveBindingLayoutResult)
            m_rayTracingState.m_surfelResolveBindingLayout = Move(*surfelResolveBindingLayoutResult);
        else
            m_rayTracingState.m_surfelResolveBindingLayout.reset();
        if(!surfelResolveBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve binding layout"));
            m_rayTracingState.m_surfelResolvePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelResolveShader, &GiSubsystem::surfelResolveCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelResolve")){
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelResolveShader).addBindingLayout(m_rayTracingState.m_surfelResolveBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelResolvePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelResolvePipelineResult)
        m_rayTracingState.m_surfelResolvePipeline = Move(*surfelResolvePipelineResult);
    else
        m_rayTracingState.m_surfelResolvePipeline.reset();
    if(!surfelResolvePipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel resolve compute pipeline"));
        m_rayTracingState.m_surfelResolvePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel resolve compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelUpsamplePipeline(){
    if(m_rayTracingState.m_surfelUpsamplePipeline)
        return true;
    if(m_rayTracingState.m_surfelUpsamplePipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel upsample requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelUpsampleBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelUpsampleBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelUpsampleBindingLayoutResult)
            m_rayTracingState.m_surfelUpsampleBindingLayout = Move(*surfelUpsampleBindingLayoutResult);
        else
            m_rayTracingState.m_surfelUpsampleBindingLayout.reset();
        if(!surfelUpsampleBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample binding layout"));
            m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelUpsampleShader, &GiSubsystem::surfelUpsampleCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelUpsample")){
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelUpsampleShader).addBindingLayout(m_rayTracingState.m_surfelUpsampleBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelUpsamplePipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelUpsamplePipelineResult)
        m_rayTracingState.m_surfelUpsamplePipeline = Move(*surfelUpsamplePipelineResult);
    else
        m_rayTracingState.m_surfelUpsamplePipeline.reset();
    if(!surfelUpsamplePipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel upsample compute pipeline"));
        m_rayTracingState.m_surfelUpsamplePipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel upsample compute pipeline"));
    return true;
}

b8 RendererRayTracingSystem::ensureSurfelTraceBuildArgsPipeline(){
    if(m_rayTracingState.m_surfelTraceBuildArgsPipeline)
        return true;
    if(m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed)
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel trace build-args requires the initialized global descriptor heap"));
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    if(!m_rayTracingState.m_surfelTraceBuildArgsBindingLayout){
        Core::BindingLayoutDesc layoutDesc(m_arena);
        layoutDesc.setVisibility(Core::ShaderType::Compute);
        layoutDesc.addItem(Core::BindingLayoutItem::PushConstants(0, sizeof(SurfelHeapPushConstants)));
        auto surfelTraceBuildArgsBindingLayoutResult = device.createBindingLayout(layoutDesc);
        if(surfelTraceBuildArgsBindingLayoutResult)
            m_rayTracingState.m_surfelTraceBuildArgsBindingLayout = Move(*surfelTraceBuildArgsBindingLayoutResult);
        else
            m_rayTracingState.m_surfelTraceBuildArgsBindingLayout.reset();
        if(!surfelTraceBuildArgsBindingLayoutResult){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args binding layout"));
            m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
            return false;
        }
    }

    if(!m_shaderSystem.loadShader<ComputeShader>(m_rayTracingState.m_surfelTraceBuildArgsShader, &GiSubsystem::surfelTraceBuildArgsCs, Core::ShaderArchive::s_DefaultVariant, "ECSRender_SurfelTraceBuildArgs")){
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }

    Core::ComputePipelineDesc pipelineDesc;
    pipelineDesc.setComputeShader(m_rayTracingState.m_surfelTraceBuildArgsShader).addBindingLayout(m_rayTracingState.m_surfelTraceBuildArgsBindingLayout).addBindingLayout(heap.getResourceLayout()).addBindingLayout(heap.getSamplerLayout());
    auto surfelTraceBuildArgsPipelineResult = device.createComputePipeline(pipelineDesc);
    if(surfelTraceBuildArgsPipelineResult)
        m_rayTracingState.m_surfelTraceBuildArgsPipeline = Move(*surfelTraceBuildArgsPipelineResult);
    else
        m_rayTracingState.m_surfelTraceBuildArgsPipeline.reset();
    if(!surfelTraceBuildArgsPipelineResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel trace build-args compute pipeline"));
        m_rayTracingState.m_surfelTraceBuildArgsPipelineFailed = true;
        return false;
    }
    NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: created surfel trace build-args compute pipeline"));
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

