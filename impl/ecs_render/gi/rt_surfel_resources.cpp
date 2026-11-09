// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/gi/lifecycle_types.adhd.h>
#include <impl/ecs_render/gi/quality_settings.adhd.h>
#include <impl/ecs_render/raytrace/ray_trace_material_resources.h>
#include <impl/ecs_render/gi/surfel_gi_pipeline_set.h>
#include <impl/ecs_render/raytrace/rt_private.h>
#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>
#include <global/algorithm.h>
#include <impl/assets_graphics/gi_subsystem.adhd.h>
#include <impl/ecs_render/gi/rt_surfel_gi_state.h>
#include <impl/ecs_render/kernel/compute_pipeline_utils.h>
#include <impl/ecs_render/shader/shader_system.h>
#include <core/common/log.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/shader_archive.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 RendererRayTracingSystem::createSurfelTargets(DeferredFrameTargets& targets){
    const u32 factor = targets.surfelResolveFactor;
    const SurfelGiResolveSize resolveSize = MakeSurfelGiResolveSize(targets.width, targets.height, factor);
    if(!IsValidSurfelGiResolveSize(resolveSize))
        return false;

    // Deferred lighting samples resolved surfel GI, never the writable pool.
    targets.surfelIrradianceFormat = Core::Format::RGBA16_FLOAT;
    Core::TextureDesc surfelIrradianceDesc;
    surfelIrradianceDesc.setWidth(targets.width).setHeight(targets.height).setFormat(targets.surfelIrradianceFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer).setName("engine/gi/surfel_irradiance");
    auto surfelIrradianceResult = m_graphics.createTexture(surfelIrradianceDesc);
    if(surfelIrradianceResult)
        targets.surfelIrradiance = Move(*surfelIrradianceResult);
    else
        targets.surfelIrradiance.reset();
    if(!surfelIrradianceResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel irradiance target"));
        return false;
    }

    // Transient reduced-resolution surfel resolve input.
    Core::TextureDesc surfelIrradianceHalfDesc;
    surfelIrradianceHalfDesc.setWidth(resolveSize.width).setHeight(resolveSize.height).setFormat(targets.surfelIrradianceFormat).setIsUAV(true).setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute).setName("engine/gi/surfel_irradiance_half");
    auto surfelIrradianceHalfResult = m_graphics.createTexture(surfelIrradianceHalfDesc);
    if(surfelIrradianceHalfResult)
        targets.surfelIrradianceHalf = Move(*surfelIrradianceHalfResult);
    else
        targets.surfelIrradianceHalf.reset();
    if(!surfelIrradianceHalfResult){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to create surfel reduced-resolution irradiance target"));
        return false;
    }

    m_rayTracingState.m_surfelResolveDispatchLogged = false;
    return true;
}

Expected<Optional<Core::GpuUploadBlobId>> RendererRayTracingSystem::retainPreparedSurfelFrameConstantsUpload(Core::GpuTaskGraph& graph, const DeferredFrameTargets& targets)const{
    if(!hasSurfelWork())
        return Optional<Core::GpuUploadBlobId>{};
    if(!m_rayTracingState.m_surfelConstants){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI has no preflighted constant buffer"));
        return MakeUnexpected(Failure{});
    }

    const NwbSurfelConstantsGpu params = BuildSurfelFrameConstants(
        m_rayTracingState.m_surfelSeeded,
        m_rayTracingState.m_surfelHashCellCount,
        m_rayTracingState.m_surfelPoolCapacity,
        m_rayTracingState.m_surfelFrameIndex,
        targets.width,
        targets.height,
        targets.surfelResolveFactor
    );
    const auto blob = graph.copyUploadData(&params, sizeof(params), alignof(NwbSurfelConstantsGpu));
    if(!blob)
        return MakeUnexpected(Failure{});
    return Optional<Core::GpuUploadBlobId>{*blob};
}

void RendererRayTracingSystem::releaseSurfelGiHeapHandles(){
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelConstantsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelGuidePoolHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCounterHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelFreeListHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelPoolSnapshotHeapHandle);
    RayTracingDetail::RetireHeapHandle(heap, m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle);
}

RayTracingSurfelPersistentResourceSnapshot RendererRayTracingSystem::snapshotSurfelPersistentResources()const noexcept{
    return RayTracingSurfelPersistentResourceSnapshot{
        .constantsBuffer = m_rayTracingState.m_surfelConstants,
        .poolBuffer = m_rayTracingState.m_surfelPoolBuffer,
        .guidePoolBuffer = m_rayTracingState.m_surfelGuidePoolBuffer,
        .cellHeadBuffer = m_rayTracingState.m_surfelCellHeadBuffer,
        .counterBuffer = m_rayTracingState.m_surfelCounterBuffer,
        .traceIndirectArgsBuffer = m_rayTracingState.m_surfelTraceIndirectArgsBuffer,
        .freeListBuffer = m_rayTracingState.m_surfelFreeListBuffer,
        .poolSnapshotBuffer = m_rayTracingState.m_surfelPoolSnapshotBuffer,
        .cellHeadSnapshotBuffer = m_rayTracingState.m_surfelCellHeadSnapshotBuffer,
        .counterReadbackBuffer = m_rayTracingState.m_surfelCounterReadback,
        .countReadbackSubmissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken,
    };
}

b8 RendererRayTracingSystem::surfelCountReadbackSubmissionMatches(const Core::QueueSubmissionToken& submissionToken)const noexcept{
    return SurfelReadbackSubmissionMatches(m_rayTracingState.m_surfelCountReadbackSubmissionToken, submissionToken);
}

void RendererRayTracingSystem::confirmSurfelCountReadbackSubmission(const Core::QueueSubmissionToken& submissionToken)noexcept{
    NWB_ASSERT(submissionToken.valid());
    m_rayTracingState.m_surfelCountReadbackSubmissionToken = submissionToken;
}

b8 RendererRayTracingSystem::recordSurfelResourceInitializationLifecycle()noexcept{
    return BeginSurfelResourceInitializationLifecycle(m_rayTracingState.m_surfelResourcesNeedClear, m_rayTracingState.m_surfelResourcesClearPending);
}

void RendererRayTracingSystem::finalizeSurfelResourceInitialization()noexcept{
    FinalizeSurfelResourceInitializationLifecycle(m_rayTracingState.m_surfelResourcesClearPending, m_rayTracingState.m_surfelResourcesNeedClear);
}

b8 RendererRayTracingSystem::ensureSurfelResources(){
    // Persistent field storage survives window resizes.
    if(!hasSurfelWork())
        return true;

    const u32 poolCapacity = m_rayTracingState.m_surfelPoolCapacity;
    const u32 cellCount = m_rayTracingState.m_surfelHashCellCount;
    if(!IsValidSurfelGiStorageCounts(poolCapacity, cellCount))
        return false;

    auto& device = m_graphics.getDevice();
    Core::GpuDescriptorHeap& heap = device.getDescriptorHeap();
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI requires the initialized global descriptor heap"));
        return false;
    }

    const auto ensureBuffer = [&](
        const Core::BufferDesc& desc,
        const tchar* const errorMessage,
        const b8 markNeedsClear,
        const b8 resetSeeded,
        Core::BufferHandle& inOutBuffer
    ){
        if(inOutBuffer)
            return true;
        auto bufferResult = m_graphics.createBuffer(desc);
        if(bufferResult)
            inOutBuffer = Move(*bufferResult);
        else{
            inOutBuffer.reset();
            NWB_LOGGER_ERROR(NWB_TEXT("{}"), errorMessage);
            return false;
        }
        if(resetSeeded)
            m_rayTracingState.m_surfelSeeded = false;
        if(markNeedsClear)
            m_rayTracingState.m_surfelResourcesNeedClear = true;
        return true;
    };
    const auto makeStructuredDesc = [](
        const u64 byteSize,
        const u32 stride,
        const b8 canHaveUavs,
        const b8 indirectArgs,
        const Core::ResourceQueueSharing::Mask queueSharing,
        const Name debugName
    ){
        Core::BufferDesc desc;
        desc
            .setByteSize(byteSize)
            .setStructStride(stride)
            .setCanHaveUAVs(canHaveUavs)
            .setIsDrawIndirectArgs(indirectArgs)
            .setQueueSharing(queueSharing)
            .setDebugName(debugName)
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        return desc;
    };

    Core::BufferDesc readbackDesc;
    readbackDesc
        .setByteSize(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE)
        .setCpuAccess(Core::CpuAccessMode::Read)
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer)
        .setDebugName(Name(s_SurfelResourceCounterReadback))
        .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
    ;
    Core::BufferDesc constantsDesc;
    constantsDesc
        .setByteSize(sizeof(NwbSurfelConstantsGpu))
        .setIsConstantBuffer(true)
        // The upload and asynchronous consumers share this selector.
        .setQueueSharing(Core::ResourceQueueSharing::GraphicsAndAsyncCompute)
        .setDebugName(Name(s_SurfelResourceConstants))
        .enableAutomaticStateTracking(Core::ResourceStates::Common)
    ;

    if(
        // Fresh pool storage needs one clear before tracing.
        !ensureBuffer(
            makeStructuredDesc(ComputeSurfelBufferByteSize(NWB_SURFEL_RECORD_SIZE, poolCapacity), NWB_SURFEL_RECORD_SIZE, true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourcePool)),
            NWB_TEXT("RendererSystem: failed to create surfel pool buffer"),
            true,
            true,
            m_rayTracingState.m_surfelPoolBuffer
        )
        // Four float4 guide lanes per surfel; this live-only buffer is never needed by bounce gathers.
        || !ensureBuffer(
            makeStructuredDesc(
                ComputeSurfelBufferByteSize(static_cast<u64>(NWB_SURFEL_GUIDE_FLOAT4_STRIDE) * NWB_SURFEL_GUIDE_FLOAT4S_PER_SURFEL, poolCapacity),
                NWB_SURFEL_GUIDE_FLOAT4_STRIDE, true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourceGuidePool)
            ),
            NWB_TEXT("RendererSystem: failed to create surfel guide pool buffer"),
            true,
            false,
            m_rayTracingState.m_surfelGuidePoolBuffer
        )
        // Hash heads use 0xFFFFFFFF as the empty sentinel.
        || !ensureBuffer(
            makeStructuredDesc(ComputeSurfelBufferByteSize(sizeof(u32), cellCount), sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourceCellHead)),
            NWB_TEXT("RendererSystem: failed to create surfel cell-head buffer"),
            true,
            false,
            m_rayTracingState.m_surfelCellHeadBuffer
        )
        // The persistent counter writes on Compute; the diagnostic tail may copy it on Transfer before the next import.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_COUNTER_SIZE, sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourceCounter)),
            NWB_TEXT("RendererSystem: failed to create surfel counter buffer"),
            true,
            false,
            m_rayTracingState.m_surfelCounterBuffer
        )
        // Build-args rewrites the indirect dispatch buffer each frame.
        || !ensureBuffer(
            makeStructuredDesc(static_cast<u64>(sizeof(u32)) * NWB_SURFEL_TRACE_INDIRECT_ARGS_WORD_COUNT, sizeof(u32), true, true, Core::ResourceQueueSharing::GraphicsAndAsyncCompute, Name(s_SurfelResourceTraceIndirectArgs)),
            NWB_TEXT("RendererSystem: failed to create surfel trace indirect-args buffer"),
            false,
            false,
            m_rayTracingState.m_surfelTraceIndirectArgsBuffer
        )
        // Age-free pushes ids and spawn pops them from this persistent free list.
        || !ensureBuffer(
            makeStructuredDesc(ComputeSurfelBufferByteSize(sizeof(u32), poolCapacity), sizeof(u32), true, false, Core::ResourceQueueSharing::GraphicsAndAsyncCompute, Name(s_SurfelResourceFreeList)),
            NWB_TEXT("RendererSystem: failed to create surfel free-list buffer"),
            true,
            false,
            m_rayTracingState.m_surfelFreeListBuffer
        )
        // Bounce gathers read a stable previous-frame pool snapshot.
        || !ensureBuffer(
            makeStructuredDesc(ComputeSurfelBufferByteSize(NWB_SURFEL_RECORD_SIZE, poolCapacity), NWB_SURFEL_RECORD_SIZE, false, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourcePoolSnapshot)),
            NWB_TEXT("RendererSystem: failed to create surfel pool snapshot buffer"),
            false,
            false,
            m_rayTracingState.m_surfelPoolSnapshotBuffer
        )
        // Snapshot hash heads alongside the pool for a consistent bounce gather.
        || !ensureBuffer(
            makeStructuredDesc(ComputeSurfelBufferByteSize(sizeof(u32), cellCount), sizeof(u32), false, false, Core::ResourceQueueSharing::GraphicsAsyncComputeAndTransfer, Name(s_SurfelResourceCellHeadSnapshot)),
            NWB_TEXT("RendererSystem: failed to create surfel cell-head snapshot buffer"),
            false,
            false,
            m_rayTracingState.m_surfelCellHeadSnapshotBuffer
        )
        || !ensureBuffer(
            readbackDesc,
            NWB_TEXT("RendererSystem: failed to create surfel counter readback buffer"),
            false,
            false,
            m_rayTracingState.m_surfelCounterReadback
        )
        // Every surfel pass binds this per-frame parameter buffer as a ConstantBuffer.
        || !ensureBuffer(
            constantsDesc,
            NWB_TEXT("RendererSystem: failed to create surfel constant buffer"),
            false,
            false,
            m_rayTracingState.m_surfelConstants
        )
    )
        return false;

    // Persistent descriptors own backing resources until deferred retirement.
    if(
        !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::UniformBuffer, false, heap, *m_rayTracingState.m_surfelConstants.get(), m_rayTracingState.m_surfelConstantsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelPoolBuffer.get(), m_rayTracingState.m_surfelPoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelGuidePoolBuffer.get(), m_rayTracingState.m_surfelGuidePoolHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelCellHeadBuffer.get(), m_rayTracingState.m_surfelCellHeadHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelCounterBuffer.get(), m_rayTracingState.m_surfelCounterHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, true, heap, *m_rayTracingState.m_surfelFreeListBuffer.get(), m_rayTracingState.m_surfelFreeListHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_rayTracingState.m_surfelPoolSnapshotBuffer.get(), m_rayTracingState.m_surfelPoolSnapshotHeapHandle)
        || !RayTracingDetail::EnsureHeapBuffer(Core::GpuDescriptorClass::StorageBuffer, false, heap, *m_rayTracingState.m_surfelCellHeadSnapshotBuffer.get(), m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle)
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: failed to register persistent surfel resources in the descriptor heap"));
        return false;
    }

    // Only tracing differs between hardware and software surfel paths.
    SurfelGiPipelineSet pipelines(m_arena, m_graphics, m_shaderSystem, m_rayTracingState);
    const b8 traceReady = m_rayTracingState.m_surfelUseHwTrace ? pipelines.ensureSurfelTraceHwPipeline() : pipelines.ensureSurfelTracePipeline();
    if(!pipelines.ensureSurfelSpawnPipeline() || !pipelines.ensureSurfelAgeFreePipeline() || !pipelines.ensureSurfelHashBuildPipeline() || !traceReady || !pipelines.ensureSurfelResolvePipeline() || !pipelines.ensureSurfelUpsamplePipeline() || !pipelines.ensureSurfelTraceBuildArgsPipeline())
        return false;
    return true;
}

b8 RendererRayTracingSystem::shouldCaptureSurfelCountReadback()const noexcept{
    return hasSurfelWork() && m_rayTracingState.m_surfelCounterBuffer && m_rayTracingState.m_surfelCounterReadback && !m_rayTracingState.m_surfelCountReadbackSubmissionToken.valid() && IsSurfelCountReadbackDue(m_rayTracingState.m_surfelFrameIndex);
}

b8 RendererRayTracingSystem::prepareSurfelResources(DeferredFrameTargets& targets){
    if(!hasSurfelWork())
        return true;

    m_rayTracingState.m_surfelUseCsgTrace = m_lightSpaceShadow.m_csg.snapshot.hasCsg;
    if(!ensureSurfelResources())
        return false;

    // GI consumes the same selector descriptor as the other current ray effects.
    if(!targets.bindless.valid() || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer) || !RayTraceMaterialResources(m_graphics, m_rayTracingState, m_rayTracingState).ensureRayTraceMaterialContextSlotsHeapHandle()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI trace heap context is incomplete"));
        return false;
    }

    return true;
}


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
    NWB_LOGGER_ESSENTIAL_INFO(NWB_TEXT("RendererSystem: created surfel {} compute pipeline"), passLabel);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


b8 SurfelGiPipelineSet::ensureSurfelSpawnPipeline(){
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

b8 SurfelGiPipelineSet::ensureSurfelAgeFreePipeline(){
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

b8 SurfelGiPipelineSet::ensureSurfelHashBuildPipeline(){
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

b8 SurfelGiPipelineSet::ensureSurfelTracePipeline(){
    const b8 csg = m_rayTracingState.m_surfelUseCsgTrace;
    return __hidden_surfel_pipelines::EnsureSurfelComputePipeline(
        m_arena,
        m_graphics.getDevice(),
        m_shaderSystem,
        &GiSubsystem::surfelTraceCs,
        ResolveGiCsgVariant(csg),
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
b8 SurfelGiPipelineSet::ensureSurfelTraceHwPipeline(){
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
        ResolveGiCsgVariant(csg),
        "ECSRender_SurfelTraceHw",
        true,
        NWB_TEXT("HW trace"),
        m_rayTracingState.m_surfelHeapBindingLayout,
        shader,
        pipeline,
        failed
    );
}

b8 SurfelGiPipelineSet::ensureSurfelResolvePipeline(){
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

b8 SurfelGiPipelineSet::ensureSurfelUpsamplePipeline(){
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

b8 SurfelGiPipelineSet::ensureSurfelTraceBuildArgsPipeline(){
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


b8 RendererRayTracingSystem::renderSurfelGiPhases(
    Core::CommandList& commandList,
    DeferredFrameTargets& targets,
    const DeferredLightingGraphResources& deferredLightingResources,
    const b8 dispatchAgeFree,
    const b8 dispatchHashBuild,
    const b8 dispatchSpawn,
    const b8 dispatchTraceBuildArgs,
    const b8 dispatchTrace,
    const b8 dispatchResolve,
    const b8 dispatchRemaining
){
    if(!hasSurfelWork())
        return true;

    NWB_ASSERT(targets.bindless.valid());
    Core::GpuDescriptorHeap& heap = m_graphics.getDevice().getDescriptorHeap();
    if(!heap.isInitialized())
        return false;

    // Only the trace pass is backend-specific.
    const b8 useHwTrace = m_rayTracingState.m_surfelUseHwTrace;
    const b8 useCsgTrace = m_rayTracingState.m_surfelUseCsgTrace;
    Core::ComputePipeline* const tracePipeline = useHwTrace
        ? (useCsgTrace ? m_rayTracingState.m_surfelTraceCsgHwPipeline.get() : m_rayTracingState.m_surfelTraceHwPipeline.get())
        : (useCsgTrace ? m_rayTracingState.m_surfelTraceCsgPipeline.get() : m_rayTracingState.m_surfelTracePipeline.get())
    ;

    if(
        !m_rayTracingState.m_surfelSpawnPipeline
        || !m_rayTracingState.m_surfelAgeFreePipeline
        || !m_rayTracingState.m_surfelHashBuildPipeline
        || !tracePipeline
        || !m_rayTracingState.m_surfelResolvePipeline
        || !m_rayTracingState.m_surfelUpsamplePipeline
        || !m_rayTracingState.m_surfelTraceBuildArgsPipeline
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: active surfel GI phase pipeline is missing"));
        return false;
    }

    if(
        !targets.bindless.valid()
        || !IsValidSurfelGiResolveFactor(targets.surfelResolveFactor)
        || !deferredLightingResources.valid()
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelConstantsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelGuidePoolHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCounterHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelFreeListHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelPoolSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle, Core::GpuDescriptorClass::StorageBuffer)
        || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.slotsBufferDescriptor, Core::GpuDescriptorClass::UniformBuffer)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferWorldPosition, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.gbufferNormal, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalf, Core::GpuDescriptorClass::SampledImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceHalfStorage, Core::GpuDescriptorClass::StorageImage)
        || !RayTracingDetail::IsHeapHandle(targets.bindless.surfelIrradianceStorage, Core::GpuDescriptorClass::StorageImage)
        || (useHwTrace && (!m_rayTracingState.m_tlas || !RayTracingDetail::IsHeapHandle(m_rayTracingState.m_tlasHeapHandle, Core::GpuDescriptorClass::AccelStruct)))
    ){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: surfel GI heap registration is incomplete"));
        return false;
    }

    const u32 poolCapacity = m_rayTracingState.m_surfelPoolCapacity;

    SurfelHeapPushConstants surfelPush = MakeSurfelHeapPushConstants(
        m_rayTracingState.m_surfelConstantsHeapHandle.slot(),
        m_rayTracingState.m_surfelPoolHeapHandle.slot(),
        m_rayTracingState.m_surfelGuidePoolHeapHandle.slot(),
        m_rayTracingState.m_surfelCellHeadHeapHandle.slot(),
        m_rayTracingState.m_surfelCounterHeapHandle.slot(),
        m_rayTracingState.m_surfelFreeListHeapHandle.slot(),
        m_rayTracingState.m_surfelPoolSnapshotHeapHandle.slot(),
        m_rayTracingState.m_surfelCellHeadSnapshotHeapHandle.slot(),
        m_rayTracingState.m_surfelTraceIndirectArgsHeapHandle.slot(),
        targets.bindless.slotsBufferDescriptor.slot(),
        m_rayTracingState.m_rayTraceMaterialContextSlotsHeapHandle.slot()
    );

    // Order every in-place field update, including prior-frame spawn writes.
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelPoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelGuidePoolBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCellHeadBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelCounterBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelFreeListBuffer.get(), true);
    commandList.setEnableUavBarriersForBuffer(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get(), true);

    // Age-free recycles unseen surfels before the graph-owned cell-head reset and hash rebuild.
    if(dispatchAgeFree){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelAgeFree, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelAgeFreePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchHashBuild && !dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Rebuild bucket lists before spawning uncovered surface representatives.
    if(dispatchHashBuild){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelHashBuild, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelHashBuildPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(DivideUp(poolCapacity, static_cast<u32>(NWB_SURFEL_LINEAR_GROUP_SIZE)), 1u, 1u);
    }

    if(!dispatchSpawn && !dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Spawn retains compatible surfaces and inserts uncovered candidates into bounded bucket lists.
    if(dispatchSpawn){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelSpawn, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();

        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();

        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelSpawnPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelSpawnPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(ResolveSurfelSpawnDispatchGroup(targets.width, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), ResolveSurfelSpawnDispatchGroup(targets.height, static_cast<u32>(NWB_SURFEL_GROUP_SIZE)), 1u);
    }

    if(!dispatchTraceBuildArgs && !dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    // Build an indirect dispatch sized for live surfels.
    if(dispatchTraceBuildArgs){
        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelTraceBuildArgsPipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatch(NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_X, NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_Y, NWB_SURFEL_TRACE_BUILD_ARGS_DISPATCH_GROUP_COUNT_Z);
    }

    if(!dispatchTrace && !dispatchResolve && !dispatchRemaining)
        return true;

    if(dispatchTrace){
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelTrace, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();
        Core::ComputeState state;
        state.setPipeline(tracePipeline);
        state.setIndirectParams(m_rayTracingState.m_surfelTraceIndirectArgsBuffer.get());
        commandList.setComputeState(state);
        // Hardware trace additionally selects the TLAS generation at set 2.
        if(m_rayTracingState.m_surfelUseHwTrace && !m_rayTracingState.m_tlasHeapHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: cannot dispatch surfel HW GI without the descriptor-heap TLAS handle"));
            return false;
        }
        const Core::GpuDescriptorHandle tlasHeapHandle = m_rayTracingState.m_surfelUseHwTrace ? m_rayTracingState.m_tlasHeapHandle : Core::GpuDescriptorHandle::Invalid();
        heap.bindCompute(commandList, *tracePipeline, tlasHeapHandle);
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        commandList.dispatchIndirect(0u);
    }

    if(!dispatchResolve && !dispatchRemaining)
        return true;

    // Resolve at the target generation's selected scale so deferred lighting never touches the writable pool.
    if(dispatchResolve){
        const SurfelGiResolveSize resolveSize = MakeSurfelGiResolveSize(targets.width, targets.height, targets.surfelResolveFactor);
        {
            Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelResolve, m_graphics.getDevice(), commandList);

            commandList.commitBarriers();

            surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
            surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
            surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceHalfStorage.slot();

            Core::ComputeState state;
            state.setPipeline(m_rayTracingState.m_surfelResolvePipeline.get());
            commandList.setComputeState(state);
            heap.bindCompute(commandList, *m_rayTracingState.m_surfelResolvePipeline.get());
            commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
            const u32 groupSize = static_cast<u32>(NWB_SURFEL_RESOLVE_GROUP_SIZE);
            commandList.dispatch(DivideUp(resolveSize.width, groupSize), DivideUp(resolveSize.height, groupSize), 1u);
            if(commandList.commandRecordingFailed())
                return false;
            LogSurfelGiResolveDispatchIfNeeded(targets.surfelResolveFactor, targets.width, targets.height, resolveSize, m_rayTracingState.m_surfelResolveDispatchLogged);
        }
    }

    if(!dispatchRemaining)
        return true;

    // Surface-aware upsample preserves coverage across edges.
    {
        Core::GpuTimingMeasure timing(m_graphics.gpuTiming(), RendererGpuTimingScope::s_SurfelUpsample, m_graphics.getDevice(), commandList);

        commandList.commitBarriers();

        surfelPush.halfIrradianceSlot = targets.bindless.surfelIrradianceHalf.slot();
        surfelPush.normalSlot = targets.bindless.gbufferNormal.slot();
        surfelPush.worldPositionSlot = targets.bindless.gbufferWorldPosition.slot();
        surfelPush.outputStorageHeapSlot = targets.bindless.surfelIrradianceStorage.slot();

        Core::ComputeState state;
        state.setPipeline(m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setComputeState(state);
        heap.bindCompute(commandList, *m_rayTracingState.m_surfelUpsamplePipeline.get());
        commandList.setPushConstants(&surfelPush, sizeof(surfelPush));
        const u32 groupSize = static_cast<u32>(NWB_SURFEL_UPSAMPLE_GROUP_SIZE);
        commandList.dispatch(DivideUp(targets.width, groupSize), DivideUp(targets.height, groupSize), 1u);
    }

    // The graph-owned late copy publishes its token only after Transfer/Compute/Graphics accepts. This pass only consumes completed diagnostics.
    {
        const u32 frameIndex = m_rayTracingState.m_surfelFrameIndex;
        const Core::QueueSubmissionToken submissionToken = m_rayTracingState.m_surfelCountReadbackSubmissionToken;
        b8 submissionComplete = false;
        if(submissionToken.valid() && submissionToken.hasPhysicalQueueIdentity()){
            const Expected<u64> completedValue = m_graphics.getDevice().queueGetCompletedInstance(Core::GpuPhysicalQueueId{ .index = submissionToken.physicalQueueIndex, .deviceGeneration = submissionToken.deviceGeneration, });
            submissionComplete = completedValue && *completedValue >= submissionToken.value;
        }
        if(submissionComplete && IsSurfelCountReadbackDelayElapsed(frameIndex, m_rayTracingState.m_surfelCountReadbackFrame)){
            Core::Buffer& readback = *m_rayTracingState.m_surfelCounterReadback;
            const Expected<void*> mappedCounts = m_graphics.getDevice().mapBuffer(readback, Core::CpuAccessMode::Read);
            if(mappedCounts){
                const NotNull<const u32*> counts = MakeNotNull(static_cast<const u32*>(*mappedCounts));
                const u32 bumpTop = counts.get()[NWB_SURFEL_COUNTER_BUMP_TOP];
                const u32 freeTop = counts.get()[NWB_SURFEL_COUNTER_FREE_TOP];
                const u32 geometryFailures = counts.get()[NWB_SURFEL_COUNTER_TRACE_GEOMETRY_FAILURE];
                m_graphics.getDevice().unmapBuffer(readback);
                LogSurfelCountReadbackCounts(
                    bumpTop,
                    freeTop,
                    geometryFailures,
                    m_rayTracingState.m_surfelPoolCapacity,
                    m_rayTracingState.m_surfelTraceGeometryFailureFlags
                );
            }
            m_rayTracingState.m_surfelCountReadbackSubmissionToken = {};
        }
    }

    // Subsequent frames use steady-state round-robin updates.
    AdvanceSurfelGiFrameState(m_rayTracingState.m_surfelSeeded, m_rayTracingState.m_surfelFrameIndex);
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
