// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_render/raytrace/rt_private.h>

#include <impl/assets/graphics/bindless/binding_slots.h>
#include <impl/assets/graphics/bindless/names.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#if defined(NWB_DEBUG)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace{
    // One known value per resource class the kernel reads; their sum is what the round-trip must reproduce.
    inline constexpr u32 s_HeapSelfTestStorageValue = 0x11110000u;   // StorageBuffer  (RWByteAddressBuffer)
    inline constexpr u32 s_HeapSelfTestUniformValue = 0x00002200u;   // UniformBuffer  (ConstantBuffer)
    inline constexpr u32 s_HeapSelfTestTypedValue   = 0x00000033u;   // SampledBuffer  (typed R32_UINT)
    inline constexpr u32 s_HeapSelfTestExpectedSum  = s_HeapSelfTestStorageValue + s_HeapSelfTestUniformValue + s_HeapSelfTestTypedValue;

    inline constexpr u32 s_HeapSelfTestConstantBufferBytes = 256u;   // pad the CBV to the constant-buffer size alignment
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Phase-1 exit gate for the global descriptor heap (docs/design/bindless-phase1-rhi-heap.md, section 10). Runs once,
// debug only, on the live device (Backend A / descriptor indexing, the guaranteed floor). Registers real resources
// through GpuDescriptorHandles, dispatches a kernel that reads each one back ONLY through its handle slot, and asserts
// the readback. Then frees half the handles, recycles the deferred-free quarantine, reallocates, and re-runs - proving
// a recycled slot never aliases a live resource. Phase 2: the heap is now brought live by the device for every run,
// so this test no longer owns its lifetime - it runs against the production heap and frees every handle it mints,
// proving the test coexists with (does not disturb) the live heap.
void RendererRayTracingSystem::runGpuDescriptorHeapSelfTest(){
    if(rayTracingState().m_gpuDescriptorHeapSelfTestDone)
        return;
    rayTracingState().m_gpuDescriptorHeapSelfTestDone = true;

    auto* device = graphics().getDevice();
    Core::GpuDescriptorHeap& heap = device->getDescriptorHeap();

    // Phase 2: the device brings the heap live at creation, so this test verifies it exists rather than initializing
    // it. No other consumer allocates from the heap at capability-probe time yet, so it is still fresh here and the
    // first StorageBuffer allocation lands at the bootstrap slot the round-trip kernel hardcodes
    // (NWB_BINDLESS_TEST_PARAMS_SLOT == 0). Once a real consumer (Phase 2 M1 mesh registration) begins allocating
    // before this runs, this test must point the kernel at the params handle's actual slot instead of asserting 0.
    if(!heap.isInitialized()){
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test skipped: device did not bring the heap live"));
        return;
    }

    // The heap-touching body runs as a lambda; on the success path it frees every handle it minted (see the end),
    // leaving the production heap exactly as found. A failure path returns early without freeing - acceptable because
    // an [ERROR] in this debug build already triggers a crash diagnostic, so the process does not carry leaked slots.
    const auto runBody = [this, device, &heap]() -> bool{
        // ---- create backing resources, one per class the kernel reads ----
        const auto createStorageBuffer = [this](const u64 byteSize, const tchar* name) -> Core::BufferHandle{
            Core::BufferDesc desc;
            desc
                .setByteSize(byteSize)
                .setCanHaveUAVs(true)
                .setCanHaveRawViews(true)
                .setDebugName(Name(name))
                .enableAutomaticStateTracking(Core::ResourceStates::Common)
            ;
            return graphics().createBuffer(desc);
        };

        Core::BufferHandle paramsBuffer  = createStorageBuffer(NWB_BINDLESS_TEST_PARAMS_BYTES, NWB_TEXT("bindless_selftest_params"));
        Core::BufferHandle storageBuffer = createStorageBuffer(sizeof(u32), NWB_TEXT("bindless_selftest_storage"));
        Core::BufferHandle outputBuffer  = createStorageBuffer(sizeof(u32), NWB_TEXT("bindless_selftest_output"));

        Core::BufferDesc uniformDesc;
        uniformDesc
            .setByteSize(s_HeapSelfTestConstantBufferBytes)
            .setIsConstantBuffer(true)
            .setDebugName(Name(NWB_TEXT("bindless_selftest_uniform")))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle uniformBuffer = graphics().createBuffer(uniformDesc);

        Core::BufferDesc typedDesc;
        typedDesc
            .setByteSize(sizeof(u32))
            .setFormat(Core::Format::R32_UINT)
            .setCanHaveTypedViews(true)
            .setDebugName(Name(NWB_TEXT("bindless_selftest_typed")))
            .enableAutomaticStateTracking(Core::ResourceStates::Common)
        ;
        Core::BufferHandle typedBuffer = graphics().createBuffer(typedDesc);

        Core::BufferDesc readbackDesc;
        readbackDesc
            .setByteSize(sizeof(u32))
            .setCpuAccess(Core::CpuAccessMode::Read)
            .setDebugName(Name(NWB_TEXT("bindless_selftest_readback")))
            .enableAutomaticStateTracking(Core::ResourceStates::CopyDest)
        ;
        Core::BufferHandle readbackBuffer = graphics().createBuffer(readbackDesc);

        if(!paramsBuffer || !storageBuffer || !outputBuffer || !uniformBuffer || !typedBuffer || !readbackBuffer){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test failed to create backing buffers"));
            return false;
        }

        // ---- allocate handles; params must land at the fixed bootstrap slot (fresh heap => first alloc is slot 0) ----
        const Core::GpuDescriptorHandle paramsHandle  = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        if(paramsHandle.slot() != NWB_BINDLESS_TEST_PARAMS_SLOT){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test: params slot {} != expected bootstrap slot {}")
                , paramsHandle.slot()
                , static_cast<u32>(NWB_BINDLESS_TEST_PARAMS_SLOT)
            );
            return false;
        }
        const Core::GpuDescriptorHandle storageHandle = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        const Core::GpuDescriptorHandle outputHandle  = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        const Core::GpuDescriptorHandle uniformHandle = heap.allocate(Core::GpuDescriptorClass::UniformBuffer);
        const Core::GpuDescriptorHandle typedHandle   = heap.allocate(Core::GpuDescriptorClass::SampledBuffer);

        // Prove the remaining non-AccelStruct classes allocate too. (AccelStruct is deliberately NOT probed here: its
        // allocate() rejection path logs an [ERROR], and this debug build captures a crash diagnostic on every [ERROR]
        // - so exercising the expected rejection from a passing self-test would derail the run. That invariant lives in
        // allocate() itself, unconditional and independent of any table state.)
        const Core::GpuDescriptorHandle sampledImageHandle = heap.allocate(Core::GpuDescriptorClass::SampledImage);
        const Core::GpuDescriptorHandle storageImageHandle = heap.allocate(Core::GpuDescriptorClass::StorageImage);
        const Core::GpuDescriptorHandle samplerHandle      = heap.allocate(Core::GpuDescriptorClass::Sampler);

        if(!paramsHandle.valid() || !storageHandle.valid() || !outputHandle.valid() || !uniformHandle.valid()
            || !typedHandle.valid() || !sampledImageHandle.valid() || !storageImageHandle.valid() || !samplerHandle.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test: a class failed to allocate a handle"));
            return false;
        }
        if(samplerHandle.slot() != 0u){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test: sampler namespace must count from 0 (got {})"), samplerHandle.slot());
            return false;
        }

        // ---- register the buffer resources into the heap (class forces slot/arrayElement/type internally) ----
        heap.write(paramsHandle,  Core::BindingSetItem::StructuredBuffer_UAV(0u, paramsBuffer.get()));
        heap.write(storageHandle, Core::BindingSetItem::StructuredBuffer_UAV(0u, storageBuffer.get()));
        heap.write(outputHandle,  Core::BindingSetItem::StructuredBuffer_UAV(0u, outputBuffer.get()));
        heap.write(uniformHandle, Core::BindingSetItem::ConstantBuffer(0u, uniformBuffer.get()));
        heap.write(typedHandle,   Core::BindingSetItem::TypedBuffer_SRV(0u, typedBuffer.get(), Core::Format::R32_UINT));

        // ---- load the cooked kernel and build a pipeline from the heap's two bindless layouts (sets 0 and 1) ----
        Core::ShaderHandle shader;
        if(!m_renderer.shaderSystem().loadShader(
            shader,
            AssetsGraphicsBindless::s_RoundtripShaderName,
            Core::ShaderArchive::s_DefaultVariant,
            Core::ShaderType::Compute,
            "ECSRender_BindlessHeapRoundtrip"
        )){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test failed to load the round-trip kernel"));
            return false;
        }

        Core::ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        Core::ComputePipelineHandle pipeline = device->createComputePipeline(pipelineDesc);
        if(!pipeline){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test failed to create the round-trip pipeline"));
            return false;
        }

        // ---- one dispatch: upload known values + the current slot table, dispatch, read the sum back ----
        const auto dispatchAndRead = [&](
            const Core::GpuDescriptorHandle storageSlotHandle,
            const Core::GpuDescriptorHandle uniformSlotHandle,
            const Core::GpuDescriptorHandle typedSlotHandle,
            const Core::GpuDescriptorHandle outputSlotHandle,
            u32& outValue
        ) -> bool{
            const u32 paramWords[] = {
                storageSlotHandle.slot(),
                uniformSlotHandle.slot(),
                typedSlotHandle.slot(),
                outputSlotHandle.slot(),
            };

            Core::CommandListHandle commandList = device->createCommandList();
            if(!commandList)
                return false;

            commandList->open();

            const auto uploadBuffer = [&](Core::Buffer* buffer, const void* data, const usize byteSize, const Core::ResourceStates::Mask readState){
                commandList->setBufferState(buffer, Core::ResourceStates::CopyDest);
                commandList->commitBarriers();
                commandList->writeBuffer(buffer, data, byteSize);
                commandList->setBufferState(buffer, readState);
                commandList->commitBarriers();
            };

            uploadBuffer(storageBuffer.get(), &s_HeapSelfTestStorageValue, sizeof(u32), Core::ResourceStates::UnorderedAccess);
            uploadBuffer(uniformBuffer.get(), &s_HeapSelfTestUniformValue, sizeof(u32), Core::ResourceStates::ConstantBuffer);
            uploadBuffer(typedBuffer.get(),   &s_HeapSelfTestTypedValue,   sizeof(u32), Core::ResourceStates::ShaderResource);
            uploadBuffer(paramsBuffer.get(),  paramWords,                  sizeof(paramWords), Core::ResourceStates::UnorderedAccess);

            commandList->setBufferState(outputBuffer.get(), Core::ResourceStates::UnorderedAccess);
            commandList->commitBarriers();

            Core::ComputeState computeState;
            computeState.setPipeline(pipeline.get());
            commandList->setComputeState(computeState);
            heap.bindCompute(*commandList, *pipeline.get());
            commandList->dispatch(1u, 1u, 1u);

            commandList->setBufferState(outputBuffer.get(), Core::ResourceStates::CopySource);
            commandList->commitBarriers();
            commandList->copyBuffer(readbackBuffer.get(), 0u, outputBuffer.get(), 0u, sizeof(u32));
            commandList->close();

            Core::CommandList* commandLists[] = { commandList.get() };
            device->executeCommandLists(commandLists, 1u);
            if(!device->waitForIdle())
                return false;

            const u32* mapped = static_cast<const u32*>(device->mapBuffer(readbackBuffer.get(), Core::CpuAccessMode::Read));
            if(!mapped)
                return false;
            outValue = mapped[0];
            device->unmapBuffer(readbackBuffer.get());
            return true;
        };

        // Pass 1 - fresh handles.
        u32 firstResult = 0u;
        if(!dispatchAndRead(storageHandle, uniformHandle, typedHandle, outputHandle, firstResult))
            return false;
        if(firstResult != s_HeapSelfTestExpectedSum){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test pass 1 read {} but expected {}"), firstResult, s_HeapSelfTestExpectedSum);
            return false;
        }

        // Pass 2 - free half, mature the deferred-free quarantine, reallocate, rewrite, re-run. A recycled slot that
        // still aliased a live resource would corrupt the sum here.
        heap.free(storageHandle);
        heap.free(typedHandle);
        for(u32 frame = 0u; frame < Core::s_MaxFramesInFlight; ++frame)
            heap.advanceFrame();

        const Core::GpuDescriptorHandle storageHandle2 = heap.allocate(Core::GpuDescriptorClass::StorageBuffer);
        const Core::GpuDescriptorHandle typedHandle2   = heap.allocate(Core::GpuDescriptorClass::SampledBuffer);
        if(!storageHandle2.valid() || !typedHandle2.valid()){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test failed to reallocate recycled handles"));
            return false;
        }
        heap.write(storageHandle2, Core::BindingSetItem::StructuredBuffer_UAV(0u, storageBuffer.get()));
        heap.write(typedHandle2,   Core::BindingSetItem::TypedBuffer_SRV(0u, typedBuffer.get(), Core::Format::R32_UINT));

        u32 secondResult = 0u;
        if(!dispatchAndRead(storageHandle2, uniformHandle, typedHandle2, outputHandle, secondResult))
            return false;
        if(secondResult != s_HeapSelfTestExpectedSum){
            NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap self-test pass 2 (post free/realloc) read {} but expected {}"), secondResult, s_HeapSelfTestExpectedSum);
            return false;
        }

        // Coexistence: return the production heap to its pre-test state. storageHandle/typedHandle were already freed
        // and recycled in pass 2; free the rest here. The per-frame advanceFrame() pump matures the deferred-free
        // quarantine and returns these slots to the free list within s_MaxFramesInFlight frames.
        heap.free(paramsHandle);
        heap.free(outputHandle);
        heap.free(uniformHandle);
        heap.free(sampledImageHandle);
        heap.free(storageImageHandle);
        heap.free(samplerHandle);
        heap.free(storageHandle2);
        heap.free(typedHandle2);

        return true;
    };

    const bool passed = runBody();

    if(passed)
        NWB_LOGGER_INFO(NWB_TEXT("RendererSystem: GpuDescriptorHeap round-trip self-test PASSED (Backend A, expected sum {})"), s_HeapSelfTestExpectedSum);
    else
        NWB_LOGGER_ERROR(NWB_TEXT("RendererSystem: GpuDescriptorHeap round-trip self-test FAILED"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
