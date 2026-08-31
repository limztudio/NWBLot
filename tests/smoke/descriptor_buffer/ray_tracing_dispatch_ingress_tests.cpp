// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/raytracing_internal.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/vulkan_test_sync.h>

#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_ray_tracing_dispatch_ingress_tests{


struct NativeTraceRaysCommand{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkStridedDeviceAddressRegionKHR rayGeneration = {};
    VkStridedDeviceAddressRegionKHR miss = {};
    VkStridedDeviceAddressRegionKHR hit = {};
    VkStridedDeviceAddressRegionKHR callable = {};
    u32 width = 0u;
    u32 height = 0u;
    u32 depth = 0u;
    bool regionsComplete = false;
};

struct NativeTraceRaysCapture{
    NativeTraceRaysCommand commands[2u] = {};
    u32 commandCount = 0u;
    bool overflowed = false;
};

class ScopedNativeTraceRaysObserver final : NoCopy{
private:
    static thread_local NativeTraceRaysCapture* s_activeCapture;
    static PFN_vkCmdTraceRaysKHR s_forwardCmdTraceRays;

    static VKAPI_ATTR void VKAPI_CALL InterceptCmdTraceRays(
        const VkCommandBuffer commandBuffer,
        const VkStridedDeviceAddressRegionKHR* const rayGeneration,
        const VkStridedDeviceAddressRegionKHR* const miss,
        const VkStridedDeviceAddressRegionKHR* const hit,
        const VkStridedDeviceAddressRegionKHR* const callable,
        const u32 width,
        const u32 height,
        const u32 depth
    ){
        NativeTraceRaysCapture* const capture = s_activeCapture;
        if(capture){
            const u32 commandIndex = capture->commandCount;
            if(commandIndex < LengthOf(capture->commands)){
                NativeTraceRaysCommand& command = capture->commands[commandIndex];
                command.commandBuffer = commandBuffer;
                command.width = width;
                command.height = height;
                command.depth = depth;
                command.regionsComplete = rayGeneration && miss && hit && callable;
                if(rayGeneration)
                    command.rayGeneration = *rayGeneration;
                if(miss)
                    command.miss = *miss;
                if(hit)
                    command.hit = *hit;
                if(callable)
                    command.callable = *callable;
            }else{
                capture->overflowed = true;
            }
            ++capture->commandCount;
        }

        if(s_forwardCmdTraceRays){
            s_forwardCmdTraceRays(
                commandBuffer,
                rayGeneration,
                miss,
                hit,
                callable,
                width,
                height,
                depth
            );
        }
    }


public:
    ScopedNativeTraceRaysObserver(GraphicsBackend::Device& device, NativeTraceRaysCapture& capture)
        : m_cmdTraceRaysOverride(device, &VolkDeviceTable::vkCmdTraceRaysKHR)
    {
        capture = {};
        if(s_activeCapture || !m_cmdTraceRaysOverride.valid())
            return;

        s_forwardCmdTraceRays = m_cmdTraceRaysOverride.original();
        s_activeCapture = &capture;
        if(!m_cmdTraceRaysOverride.replace(&ScopedNativeTraceRaysObserver::InterceptCmdTraceRays)){
            s_activeCapture = nullptr;
            return;
        }
        m_armed = true;
    }
    ~ScopedNativeTraceRaysObserver(){
        if(!m_armed)
            return;

        s_activeCapture = nullptr;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_armed; }


private:
    ScopedVulkanDeviceDispatchOverride<PFN_vkCmdTraceRaysKHR> m_cmdTraceRaysOverride;
    bool m_armed = false;
};

thread_local NativeTraceRaysCapture* ScopedNativeTraceRaysObserver::s_activeCapture = nullptr;
PFN_vkCmdTraceRaysKHR ScopedNativeTraceRaysObserver::s_forwardCmdTraceRays = nullptr;


};

// Minimal source for every module: `#version 460`, `#extension GL_EXT_ray_tracing : require`, then `void main(){}`.
// Generated and validated against Vulkan 1.2 by glslangValidator and spirv-val from Vulkan SDK 1.4.341.1.
static constexpr u32 s_DispatchRayGenerationSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c1u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_DispatchMissSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c5u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_DispatchClosestHitSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c4u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_DispatchCallableSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c6u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<usize WordCount>
[[nodiscard]] ShaderHandle CreateDispatchShader(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const ShaderType::Mask shaderType,
    const Name& debugName,
    const u32 (&spirv)[WordCount]
){
    ShaderDesc shaderDesc(arena);
    shaderDesc.setShaderType(shaderType).setDebugName(debugName);
    return device.createShader(shaderDesc, spirv, sizeof(spirv));
}

[[nodiscard]] RayTracingPipelineHandle CreateDispatchPipeline(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena
){
    const ShaderHandle rayGenerationShader = CreateDispatchShader(
        device,
        arena,
        ShaderType::RayGeneration,
        Name("tests/dispatch/ray_generation"),
        s_DispatchRayGenerationSpirv
    );
    const ShaderHandle missShader = CreateDispatchShader(
        device,
        arena,
        ShaderType::Miss,
        Name("tests/dispatch/miss"),
        s_DispatchMissSpirv
    );
    const ShaderHandle closestHitShader = CreateDispatchShader(
        device,
        arena,
        ShaderType::ClosestHit,
        Name("tests/dispatch/closest_hit"),
        s_DispatchClosestHitSpirv
    );
    const ShaderHandle callableShader = CreateDispatchShader(
        device,
        arena,
        ShaderType::Callable,
        Name("tests/dispatch/callable"),
        s_DispatchCallableSpirv
    );
    if(!rayGenerationShader || !missShader || !closestHitShader || !callableShader)
        return nullptr;

    RayTracingPipelineDesc pipelineDesc(arena);
    pipelineDesc.addBindingLayout(device.getDescriptorHeap().getResourceLayout());

    RayTracingPipelineShaderDesc rayGeneration(arena);
    rayGeneration.setShader(rayGenerationShader).setExportName("dispatch_ray_generation");
    pipelineDesc.addShader(rayGeneration);

    RayTracingPipelineShaderDesc miss(arena);
    miss.setShader(missShader).setExportName("dispatch_miss");
    pipelineDesc.addShader(miss);

    RayTracingPipelineHitGroupDesc hitGroup(arena);
    hitGroup.setClosestHitShader(closestHitShader).setExportName("dispatch_hit");
    pipelineDesc.addHitGroup(hitGroup);

    RayTracingPipelineShaderDesc callable(arena);
    callable.setShader(callableShader).setExportName("dispatch_callable");
    pipelineDesc.addShader(callable);

    return device.createRayTracingPipeline(pipelineDesc);
}

[[nodiscard]] RayTracingShaderTableHandle CreatePopulatedDispatchTable(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena
){
    const RayTracingPipelineHandle pipeline = CreateDispatchPipeline(device, arena);
    if(!pipeline)
        return nullptr;

    RayTracingShaderTableHandle table = pipeline->createShaderTable();
    if(!table)
        return nullptr;
    if(!table->setRayGenerationShader("dispatch_ray_generation"))
        return nullptr;
    if(table->addMissShader("dispatch_miss") != 0u)
        return nullptr;
    if(table->addHitGroup("dispatch_hit") != 0u)
        return nullptr;
    if(table->addCallableShader("dispatch_callable") != 0u)
        return nullptr;

    return table;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RayTracingDispatchIngressTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize())
            return;

        s_runtimeInitialized = true;
        s_ready = device().queryFeatureSupport(Feature::RayTracingPipeline);
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled ray dispatch tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
        s_ready = false;
    }

    virtual void SetUp()override{
        if(!s_ready)
            GTEST_SKIP() << "Ray dispatch ingress: no usable ray-tracing headless device.";
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_runtimeInitialized;
    static bool s_ready;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool RayTracingDispatchIngressTest::s_runtimeInitialized = false;
bool RayTracingDispatchIngressTest::s_ready = false;
UniquePtr<HeadlessGraphicsScope> RayTracingDispatchIngressTest::s_scope;
Optional<CapturingLogger> RayTracingDispatchIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> RayTracingDispatchIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RayTracingDispatchContractTest, DimensionValidationCoversTotalAxisAndOverflowLimits){
    GraphicsBackend::VulkanDetail::RayDispatchLimits totalLimits;
    totalLimits.maxInvocationCount = 64u;
    totalLimits.maxAxisCounts = { 64u, 64u, 64u };
    totalLimits.maxAxisSizes = { 1u, 1u, 1u };

    RayTracingDispatchRaysArguments arguments;
    arguments.setDimensions(4u, 4u, 4u);
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, totalLimits));
    arguments.setDimensions(5u, 4u, 4u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, totalLimits));

    GraphicsBackend::VulkanDetail::RayDispatchLimits axisLimits;
    axisLimits.maxInvocationCount = Limit<u64>::s_Max;
    axisLimits.maxAxisCounts = { 2u, 3u, 4u };
    axisLimits.maxAxisSizes = { 5u, 7u, 11u };
    arguments.setDimensions(10u, 21u, 44u);
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, axisLimits));
    arguments.setDimensions(11u, 1u, 1u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, axisLimits));
    arguments.setDimensions(1u, 22u, 1u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, axisLimits));
    arguments.setDimensions(1u, 1u, 45u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, axisLimits));

    arguments.setDimensions(Limit<u32>::s_Max, Limit<u32>::s_Max, 2u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, axisLimits));

    GraphicsBackend::VulkanDetail::RayDispatchLimits invalidLimits;
    invalidLimits.maxInvocationCount = 1u;
    invalidLimits.maxAxisCounts = { 1u, 1u, 1u };
    invalidLimits.maxAxisSizes = { 1u, 0u, 1u };
    arguments.setDimensions(1u, 1u, 1u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, invalidLimits));

    arguments.setDimensions(0u, 1u, 1u);
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ValidateRayDispatchDimensions(arguments, invalidLimits));
}

TEST_F(RayTracingDispatchIngressTest, ZeroExtentWithoutStateIsAnUnconditionalNoOp){
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();

    RayTracingDispatchRaysArguments arguments;
    commandList->dispatchRays(arguments.setDimensions(0u, 1u, 1u));
    commandList->dispatchRays(arguments.setDimensions(1u, 0u, 1u));
    commandList->dispatchRays(arguments.setDimensions(1u, 1u, 0u));
    EXPECT_FALSE(commandList->commandRecordingFailed());

    commandList->close();
    EXPECT_FALSE(commandList->commandRecordingFailed());
}

TEST_F(RayTracingDispatchIngressTest, SemanticFailuresRejectRecordingSticky){
    {
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->dispatchRays(RayTracingDispatchRaysArguments());
        EXPECT_TRUE(commandList->commandRecordingFailed());

        RayTracingDispatchRaysArguments zeroArguments;
        commandList->dispatchRays(zeroArguments.setDimensions(0u, 1u, 1u));
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }

    const RayTracingPipelineHandle pipeline = CreateDispatchPipeline(device(), arena());
    ASSERT_TRUE(pipeline);
    const RayTracingShaderTableHandle missingRayGeneration = pipeline->createShaderTable();
    ASSERT_TRUE(missingRayGeneration);
    ASSERT_EQ(missingRayGeneration->addMissShader("dispatch_miss"), 0u);
    {
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setRayTracingState(RayTracingState().setShaderTable(missingRayGeneration.get()));
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->dispatchRays(RayTracingDispatchRaysArguments());
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }

    const RayTracingShaderTableHandle table = CreatePopulatedDispatchTable(device(), arena());
    ASSERT_TRUE(table);
    {
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
        ASSERT_FALSE(commandList->commandRecordingFailed());

        RayTracingDispatchRaysArguments maximumArguments;
        maximumArguments.setDimensions(Limit<u32>::s_Max, Limit<u32>::s_Max, Limit<u32>::s_Max);
        commandList->dispatchRays(maximumArguments);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        commandList->close();
    }
}

TEST_F(RayTracingDispatchIngressTest, ValidAllRegionTableRecordsTraceCommand){
    const RayTracingShaderTableHandle table = CreatePopulatedDispatchTable(device(), arena());
    ASSERT_TRUE(table);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->dispatchRays(RayTracingDispatchRaysArguments().setDimensions(1u, 1u, 1u));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
    EXPECT_FALSE(commandList->commandRecordingFailed());
}

TEST_F(RayTracingDispatchIngressTest, NativeFourRegionAddressesChangeAfterMutationAndOriginalDispatchSubmits){
    const RayTracingShaderTableHandle table = CreatePopulatedDispatchTable(device(), arena());
    ASSERT_TRUE(table);

    __hidden_ray_tracing_dispatch_ingress_tests::NativeTraceRaysCapture nativeCapture;
    CommandListHandle originalCommandList;
    CommandListHandle replacementCommandList;
    const VulkanTestDeviceContext nativeContext = VulkanTestDeviceProbe::capture(device());
    ASSERT_TRUE(nativeContext.valid());
    const PFN_vkCmdTraceRaysKHR originalCmdTraceRays = nativeContext.deviceDispatch->vkCmdTraceRaysKHR;
    ASSERT_TRUE(originalCmdTraceRays);

    {
        __hidden_ray_tracing_dispatch_ingress_tests::ScopedNativeTraceRaysObserver observer(device(), nativeCapture);
        ASSERT_TRUE(observer.valid());

        originalCommandList = device().createCommandList();
        ASSERT_TRUE(originalCommandList);
        originalCommandList->open();
        originalCommandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
        ASSERT_FALSE(originalCommandList->commandRecordingFailed());
        originalCommandList->dispatchRays(RayTracingDispatchRaysArguments().setDimensions(1u, 1u, 1u));
        ASSERT_FALSE(originalCommandList->commandRecordingFailed());
        originalCommandList->close();
        ASSERT_FALSE(originalCommandList->commandRecordingFailed());

        ASSERT_TRUE(table->setRayGenerationShader("dispatch_ray_generation"));
        table->clearMissShaders();
        table->clearHitShaders();
        table->clearCallableShaders();
        ASSERT_EQ(table->addMissShader("dispatch_miss"), 0u);
        ASSERT_EQ(table->addHitGroup("dispatch_hit"), 0u);
        ASSERT_EQ(table->addCallableShader("dispatch_callable"), 0u);

        replacementCommandList = device().createCommandList();
        ASSERT_TRUE(replacementCommandList);
        replacementCommandList->open();
        replacementCommandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
        ASSERT_FALSE(replacementCommandList->commandRecordingFailed());
        replacementCommandList->dispatchRays(RayTracingDispatchRaysArguments().setDimensions(1u, 1u, 1u));
        ASSERT_FALSE(replacementCommandList->commandRecordingFailed());
        replacementCommandList->close();
        ASSERT_FALSE(replacementCommandList->commandRecordingFailed());
    }
    ASSERT_EQ(nativeContext.deviceDispatch->vkCmdTraceRaysKHR, originalCmdTraceRays);

    ASSERT_EQ(nativeCapture.commandCount, 2u);
    ASSERT_FALSE(nativeCapture.overflowed);
    const auto& original = nativeCapture.commands[0u];
    const auto& replacement = nativeCapture.commands[1u];
    ASSERT_TRUE(original.regionsComplete);
    ASSERT_TRUE(replacement.regionsComplete);
    ASSERT_NE(original.commandBuffer, VK_NULL_HANDLE);
    ASSERT_NE(replacement.commandBuffer, VK_NULL_HANDLE);
    ASSERT_NE(original.commandBuffer, replacement.commandBuffer);
    EXPECT_EQ(original.width, 1u);
    EXPECT_EQ(original.height, 1u);
    EXPECT_EQ(original.depth, 1u);
    EXPECT_EQ(replacement.width, 1u);
    EXPECT_EQ(replacement.height, 1u);
    EXPECT_EQ(replacement.depth, 1u);

    const VkStridedDeviceAddressRegionKHR* const originalRegions[]{
        &original.rayGeneration,
        &original.miss,
        &original.hit,
        &original.callable,
    };
    const VkStridedDeviceAddressRegionKHR* const replacementRegions[]{
        &replacement.rayGeneration,
        &replacement.miss,
        &replacement.hit,
        &replacement.callable,
    };
    for(usize regionIndex = 0u; regionIndex < LengthOf(originalRegions); ++regionIndex){
        const VkStridedDeviceAddressRegionKHR& originalRegion = *originalRegions[regionIndex];
        const VkStridedDeviceAddressRegionKHR& replacementRegion = *replacementRegions[regionIndex];
        ASSERT_NE(originalRegion.deviceAddress, 0u);
        ASSERT_NE(originalRegion.stride, 0u);
        ASSERT_NE(originalRegion.size, 0u);
        ASSERT_EQ(originalRegion.size, originalRegion.stride);
        ASSERT_NE(replacementRegion.deviceAddress, 0u);
        ASSERT_NE(replacementRegion.stride, 0u);
        ASSERT_NE(replacementRegion.size, 0u);
        ASSERT_EQ(replacementRegion.size, replacementRegion.stride);
        EXPECT_EQ(replacementRegion.stride, originalRegion.stride);
        EXPECT_EQ(replacementRegion.size, originalRegion.size);
        ASSERT_NE(replacementRegion.deviceAddress, originalRegion.deviceAddress);
    }

    for(usize originalIndex = 0u; originalIndex < LengthOf(originalRegions); ++originalIndex){
        const VkStridedDeviceAddressRegionKHR& originalRegion = *originalRegions[originalIndex];
        for(usize replacementIndex = 0u; replacementIndex < LengthOf(replacementRegions); ++replacementIndex){
            const VkStridedDeviceAddressRegionKHR& replacementRegion = *replacementRegions[replacementIndex];
            const bool originalBeforeReplacement = originalRegion.deviceAddress < replacementRegion.deviceAddress
                && originalRegion.size <= replacementRegion.deviceAddress - originalRegion.deviceAddress;
            const bool replacementBeforeOriginal = replacementRegion.deviceAddress < originalRegion.deviceAddress
                && replacementRegion.size <= originalRegion.deviceAddress - replacementRegion.deviceAddress;
            EXPECT_TRUE(originalBeforeReplacement || replacementBeforeOriginal)
                << "Original region " << originalIndex << " overlaps replacement region " << replacementIndex;
        }
    }

    CommandList* const commandLists[]{ originalCommandList.get() };
    const QueueSubmissionToken token = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    ASSERT_TRUE(device().waitForIdle());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

