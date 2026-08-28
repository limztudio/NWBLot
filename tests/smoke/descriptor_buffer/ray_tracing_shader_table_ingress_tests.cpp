// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
#include <core/graphics/backend_contract.h>
#include <core/graphics/vulkan/backend.h>
#include <core/graphics/vulkan/raytracing_internal.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;

static_assert(GraphicsContract::RayTracingShaderTableApi<GraphicsBackend::ShaderTable>);

template<typename Operation>
void ExpectShaderTableBoolRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
    EXPECT_FALSE(operation());
#endif
}

template<typename Operation>
void ExpectShaderTableRecordRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_EQ(operation(), s_InvalidRayTracingShaderTableRecordIndex); }, "");
#else
    EXPECT_EQ(operation(), s_InvalidRayTracingShaderTableRecordIndex);
#endif
}

template<typename Operation>
void ExpectRayTracingPipelineRejection(Operation&& operation){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({ EXPECT_FALSE(operation()); }, "");
#else
    EXPECT_FALSE(operation());
#endif
}

template<typename Function>
class ScopedVolkFunctionNull final{
public:
    explicit ScopedVolkFunctionNull(Function& function)
        : m_function(function)
        , m_original(function)
    {
        m_function = nullptr;
    }
    ~ScopedVolkFunctionNull(){ m_function = m_original; }

    ScopedVolkFunctionNull(const ScopedVolkFunctionNull&) = delete;
    ScopedVolkFunctionNull& operator=(const ScopedVolkFunctionNull&) = delete;


private:
    Function& m_function;
    Function m_original;
};

[[nodiscard]] GraphicsBackend::VulkanDetail::RayTracingCapabilityInputs MakeSupportedRayTracingCapabilityInputs(){
    GraphicsBackend::VulkanDetail::RayTracingCapabilityInputs inputs;
    inputs.accelerationStructureExtensionEnabled = true;
    inputs.accelerationStructureFeatureEnabled = true;
    inputs.createAccelerationStructureEntryPointAvailable = true;
    inputs.destroyAccelerationStructureEntryPointAvailable = true;
    inputs.getAccelerationStructureBuildSizesEntryPointAvailable = true;
    inputs.getAccelerationStructureDeviceAddressEntryPointAvailable = true;
    inputs.cmdBuildAccelerationStructuresEntryPointAvailable = true;

    inputs.rayTracingPipelineExtensionEnabled = true;
    inputs.rayTracingPipelineFeatureEnabled = true;
    inputs.createRayTracingPipelinesEntryPointAvailable = true;
    inputs.getRayTracingShaderGroupHandlesEntryPointAvailable = true;
    inputs.cmdTraceRaysEntryPointAvailable = true;

    inputs.opacityMicromapExtensionEnabled = true;
    inputs.opacityMicromapFeatureEnabled = true;
    inputs.synchronization2ExtensionEnabled = true;
    inputs.createMicromapEntryPointAvailable = true;
    inputs.destroyMicromapEntryPointAvailable = true;
    inputs.getMicromapBuildSizesEntryPointAvailable = true;
    inputs.cmdBuildMicromapsEntryPointAvailable = true;
    return inputs;
}

// Minimal source for every module: `#version 460`, `#extension GL_EXT_ray_tracing : require`, then `void main(){}`.
// Exact Vulkan SDK 1.4.341.1 generation and validation commands:
// glslangValidator.exe --target-env vulkan1.2 -V -S rgen -e main shader_table_minimal.glsl -o shader_table_ray_generation.spv
// spirv-val.exe --target-env vulkan1.2 shader_table_ray_generation.spv
// glslangValidator.exe --target-env vulkan1.2 -V -S rmiss -e main shader_table_minimal.glsl -o shader_table_miss.spv
// spirv-val.exe --target-env vulkan1.2 shader_table_miss.spv
// glslangValidator.exe --target-env vulkan1.2 -V -S rchit -e main shader_table_minimal.glsl -o shader_table_closest_hit.spv
// spirv-val.exe --target-env vulkan1.2 shader_table_closest_hit.spv
// glslangValidator.exe --target-env vulkan1.2 -V -S rcall -e main shader_table_minimal.glsl -o shader_table_callable.spv
// spirv-val.exe --target-env vulkan1.2 shader_table_callable.spv
static constexpr u32 s_ShaderTableRayGenerationSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c1u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_ShaderTableMissSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c5u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_ShaderTableClosestHitSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c4u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

static constexpr u32 s_ShaderTableCallableSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c6u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ShaderTablePipelineShape{
enum Enum : u8{
    RayGenerationAndMiss,
    Hit,
    Callable,
    AmbiguousRayGenerationAndMiss,
    AmbiguousHit,
    AmbiguousCallable,
};
};


template<usize WordCount>
[[nodiscard]] ShaderHandle CreateShaderTableShader(
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

[[nodiscard]] RayTracingPipelineHandle CreateShaderTablePipeline(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    const ShaderTablePipelineShape::Enum shape,
    const bool allowOpacityMicromaps = false
){
    const ShaderHandle rayGenerationShader = CreateShaderTableShader(
        device,
        arena,
        ShaderType::RayGeneration,
        Name("tests/shader_table/ray_generation"),
        s_ShaderTableRayGenerationSpirv
    );
    const ShaderHandle missShader = CreateShaderTableShader(
        device,
        arena,
        ShaderType::Miss,
        Name("tests/shader_table/miss"),
        s_ShaderTableMissSpirv
    );
    const ShaderHandle closestHitShader = CreateShaderTableShader(
        device,
        arena,
        ShaderType::ClosestHit,
        Name("tests/shader_table/closest_hit"),
        s_ShaderTableClosestHitSpirv
    );
    const ShaderHandle callableShader = CreateShaderTableShader(
        device,
        arena,
        ShaderType::Callable,
        Name("tests/shader_table/callable"),
        s_ShaderTableCallableSpirv
    );
    if(!rayGenerationShader || !missShader || !closestHitShader || !callableShader)
        return nullptr;

    RayTracingPipelineDesc pipelineDesc(arena);
    pipelineDesc
        .addBindingLayout(device.getDescriptorHeap().getResourceLayout())
        .setAllowOpacityMicromaps(allowOpacityMicromaps)
    ;
    switch(shape){
    case ShaderTablePipelineShape::RayGenerationAndMiss:{
        RayTracingPipelineShaderDesc sharedRayGeneration(arena);
        sharedRayGeneration.setShader(rayGenerationShader).setExportName("shared_export");
        pipelineDesc.addShader(sharedRayGeneration);

        RayTracingPipelineShaderDesc fallbackRayGeneration(arena);
        fallbackRayGeneration.setShader(rayGenerationShader);
        pipelineDesc.addShader(fallbackRayGeneration);

        RayTracingPipelineShaderDesc sharedMiss(arena);
        sharedMiss.setShader(missShader).setExportName("shared_export");
        pipelineDesc.addShader(sharedMiss);

        RayTracingPipelineShaderDesc uniqueMiss(arena);
        uniqueMiss.setShader(missShader).setExportName("unique_miss");
        pipelineDesc.addShader(uniqueMiss);

        break;
    }
    case ShaderTablePipelineShape::Hit:{
        RayTracingPipelineShaderDesc rayGeneration(arena);
        rayGeneration.setShader(rayGenerationShader).setExportName("hit_ray_generation");
        pipelineDesc.addShader(rayGeneration);

        RayTracingPipelineHitGroupDesc hitGroup(arena);
        hitGroup.setClosestHitShader(closestHitShader).setExportName("unique_hit");
        pipelineDesc.addHitGroup(hitGroup);

        RayTracingPipelineHitGroupDesc secondHitGroup(arena);
        secondHitGroup.setClosestHitShader(closestHitShader).setExportName("second_hit");
        pipelineDesc.addHitGroup(secondHitGroup);

        break;
    }
    case ShaderTablePipelineShape::Callable:{
        RayTracingPipelineShaderDesc rayGeneration(arena);
        rayGeneration.setShader(rayGenerationShader).setExportName("callable_ray_generation");
        pipelineDesc.addShader(rayGeneration);

        RayTracingPipelineShaderDesc callableFirst(arena);
        callableFirst.setShader(callableShader).setExportName("callable_first");
        pipelineDesc.addShader(callableFirst);

        RayTracingPipelineShaderDesc callableSecond(arena);
        callableSecond.setShader(callableShader).setExportName("callable_second");
        pipelineDesc.addShader(callableSecond);

        break;
    }
    case ShaderTablePipelineShape::AmbiguousRayGenerationAndMiss:{
        RayTracingPipelineShaderDesc duplicateRayGenerationA(arena);
        duplicateRayGenerationA.setShader(rayGenerationShader).setExportName("duplicate_ray_generation");
        pipelineDesc.addShader(duplicateRayGenerationA);

        RayTracingPipelineShaderDesc duplicateRayGenerationB(arena);
        duplicateRayGenerationB.setShader(rayGenerationShader).setExportName("duplicate_ray_generation");
        pipelineDesc.addShader(duplicateRayGenerationB);

        RayTracingPipelineShaderDesc duplicateMissA(arena);
        duplicateMissA.setShader(missShader).setExportName("duplicate_miss");
        pipelineDesc.addShader(duplicateMissA);

        RayTracingPipelineShaderDesc duplicateMissB(arena);
        duplicateMissB.setShader(missShader).setExportName("duplicate_miss");
        pipelineDesc.addShader(duplicateMissB);

        break;
    }
    case ShaderTablePipelineShape::AmbiguousHit:{
        RayTracingPipelineShaderDesc rayGeneration(arena);
        rayGeneration.setShader(rayGenerationShader).setExportName("ambiguous_hit_ray_generation");
        pipelineDesc.addShader(rayGeneration);

        RayTracingPipelineHitGroupDesc duplicateHitGroupA(arena);
        duplicateHitGroupA.setClosestHitShader(closestHitShader).setExportName("duplicate_hit");
        pipelineDesc.addHitGroup(duplicateHitGroupA);

        RayTracingPipelineHitGroupDesc duplicateHitGroupB(arena);
        duplicateHitGroupB.setClosestHitShader(closestHitShader).setExportName("duplicate_hit");
        pipelineDesc.addHitGroup(duplicateHitGroupB);

        break;
    }
    case ShaderTablePipelineShape::AmbiguousCallable:{
        RayTracingPipelineShaderDesc rayGeneration(arena);
        rayGeneration.setShader(rayGenerationShader).setExportName("ambiguous_callable_ray_generation");
        pipelineDesc.addShader(rayGeneration);

        RayTracingPipelineShaderDesc duplicateCallableA(arena);
        duplicateCallableA.setShader(callableShader).setExportName("duplicate_callable");
        pipelineDesc.addShader(duplicateCallableA);

        RayTracingPipelineShaderDesc duplicateCallableB(arena);
        duplicateCallableB.setShader(callableShader).setExportName("duplicate_callable");
        pipelineDesc.addShader(duplicateCallableB);

        break;
    }
    default:
        return nullptr;
    }

    return device.createRayTracingPipeline(pipelineDesc);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RayTracingShaderTableIngressTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        GTEST_FLAG_SET(death_test_style, "threadsafe");
#endif
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
                << "validation-enabled shader-table tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeInitialized = false;
        s_ready = false;
    }

    virtual void SetUp()override{
        if(!s_ready)
            GTEST_SKIP() << "Shader-table ingress: no usable ray-tracing headless device.";
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

bool RayTracingShaderTableIngressTest::s_runtimeInitialized = false;
bool RayTracingShaderTableIngressTest::s_ready = false;
UniquePtr<HeadlessGraphicsScope> RayTracingShaderTableIngressTest::s_scope;
Optional<CapturingLogger> RayTracingShaderTableIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> RayTracingShaderTableIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RayTracingShaderTableContractTest, NamedFailureAndAlignmentHelpersAreDeterministic){
    EXPECT_EQ(s_InvalidRayTracingShaderTableRecordIndex, Limit<u32>::s_Max);

    u64 recordByteSize = 0u;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ComputeShaderTableByteSize(
        2u,
        64u,
        recordByteSize,
        NWB_TEXT("test shader table size")
    ));
    EXPECT_EQ(recordByteSize, 128u);

    u64 allocationByteSize = 0u;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ComputeShaderTableAllocationByteSize(64u, 256u, allocationByteSize));
    EXPECT_EQ(allocationByteSize, 319u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ComputeShaderTableAllocationByteSize(
        Limit<u64>::s_Max,
        256u,
        allocationByteSize
    ));
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ComputeShaderTableAllocationByteSize(64u, 3u, allocationByteSize));

    u64 offset = 0u;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::ComputeShaderTableAlignedOffset(0x1003u, 319u, 64u, 256u, offset));
    EXPECT_EQ(offset, 253u);
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ComputeShaderTableAlignedOffset(0x1003u, 316u, 64u, 256u, offset));
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::ComputeShaderTableAlignedOffset(
        Limit<u64>::s_Max - 3u,
        319u,
        64u,
        256u,
        offset
    ));
}

TEST(RayTracingShaderTableContractTest, PipelineCreateFlagsPreserveDescriptorBufferAndRequestedRayTracingCapabilities){
    Alloc::GlobalArena arena(Name("tests/ray_tracing_pipeline_flags"));
    RayTracingPipelineDesc desc(arena);

    EXPECT_EQ(
        GraphicsBackend::VulkanDetail::ComputeRayTracingPipelineCreateFlags(desc),
        VK_PIPELINE_CREATE_2_DESCRIPTOR_BUFFER_BIT_EXT
    );

    desc.setAllowOpacityMicromaps(true);
    EXPECT_EQ(
        GraphicsBackend::VulkanDetail::ComputeRayTracingPipelineCreateFlags(desc),
        VK_PIPELINE_CREATE_2_DESCRIPTOR_BUFFER_BIT_EXT
            | VK_PIPELINE_CREATE_2_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT
    );

    desc.setAllowSpheres(true).setAllowLinearSweptSpheres(true);
    EXPECT_EQ(
        GraphicsBackend::VulkanDetail::ComputeRayTracingPipelineCreateFlags(desc),
        VK_PIPELINE_CREATE_2_DESCRIPTOR_BUFFER_BIT_EXT
            | VK_PIPELINE_CREATE_2_RAY_TRACING_OPACITY_MICROMAP_BIT_EXT
            | VK_PIPELINE_CREATE_2_RAY_TRACING_ALLOW_SPHERES_AND_LINEAR_SWEPT_SPHERES_BIT_NV
    );
}

TEST(RayTracingShaderTableContractTest, AccelerationStructureCapabilityRequiresEnabledContractAndEntrypoints){
    using CapabilityInputs = GraphicsBackend::VulkanDetail::RayTracingCapabilityInputs;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::SupportsRayTracingAccelStruct(MakeSupportedRayTracingCapabilityInputs()));

    const auto expectUnsupported = [](bool CapabilityInputs::* missingInput){
        CapabilityInputs inputs = MakeSupportedRayTracingCapabilityInputs();
        inputs.*missingInput = false;
        EXPECT_FALSE(GraphicsBackend::VulkanDetail::SupportsRayTracingAccelStruct(inputs));
    };
    expectUnsupported(&CapabilityInputs::accelerationStructureExtensionEnabled);
    expectUnsupported(&CapabilityInputs::accelerationStructureFeatureEnabled);
    expectUnsupported(&CapabilityInputs::createAccelerationStructureEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::destroyAccelerationStructureEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::getAccelerationStructureBuildSizesEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::getAccelerationStructureDeviceAddressEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::cmdBuildAccelerationStructuresEntryPointAvailable);
}

TEST(RayTracingShaderTableContractTest, RayTracingPipelineCapabilityRequiresEnabledContractDependenciesAndEntrypoints){
    using CapabilityInputs = GraphicsBackend::VulkanDetail::RayTracingCapabilityInputs;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::SupportsRayTracingPipeline(MakeSupportedRayTracingCapabilityInputs()));

    const auto expectUnsupported = [](bool CapabilityInputs::* missingInput){
        CapabilityInputs inputs = MakeSupportedRayTracingCapabilityInputs();
        inputs.*missingInput = false;
        EXPECT_FALSE(GraphicsBackend::VulkanDetail::SupportsRayTracingPipeline(inputs));
    };
    expectUnsupported(&CapabilityInputs::rayTracingPipelineExtensionEnabled);
    expectUnsupported(&CapabilityInputs::rayTracingPipelineFeatureEnabled);
    expectUnsupported(&CapabilityInputs::createRayTracingPipelinesEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::getRayTracingShaderGroupHandlesEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::cmdTraceRaysEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::accelerationStructureFeatureEnabled);
}

TEST(RayTracingShaderTableContractTest, OpacityMicromapCapabilityRequiresEnabledContractDependenciesAndEntrypoints){
    using CapabilityInputs = GraphicsBackend::VulkanDetail::RayTracingCapabilityInputs;
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::SupportsRayTracingOpacityMicromap(MakeSupportedRayTracingCapabilityInputs()));

    const auto expectUnsupported = [](bool CapabilityInputs::* missingInput){
        CapabilityInputs inputs = MakeSupportedRayTracingCapabilityInputs();
        inputs.*missingInput = false;
        EXPECT_FALSE(GraphicsBackend::VulkanDetail::SupportsRayTracingOpacityMicromap(inputs));
    };
    expectUnsupported(&CapabilityInputs::opacityMicromapExtensionEnabled);
    expectUnsupported(&CapabilityInputs::opacityMicromapFeatureEnabled);
    expectUnsupported(&CapabilityInputs::synchronization2ExtensionEnabled);
    expectUnsupported(&CapabilityInputs::createMicromapEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::destroyMicromapEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::getMicromapBuildSizesEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::cmdBuildMicromapsEntryPointAvailable);
    expectUnsupported(&CapabilityInputs::accelerationStructureFeatureEnabled);
}

TEST(RayTracingShaderTableContractTest, RayTracingShaderTypeContractRejectsMissingCombinedAndUnexpectedStages){
    constexpr ShaderType::Mask s_GeneralShaderTypes = static_cast<ShaderType::Mask>(
        ShaderType::RayGeneration | ShaderType::Miss | ShaderType::Callable
    );
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(ShaderType::RayGeneration, s_GeneralShaderTypes));
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(ShaderType::Miss, s_GeneralShaderTypes));
    EXPECT_TRUE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(ShaderType::Callable, s_GeneralShaderTypes));
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(ShaderType::None, s_GeneralShaderTypes));
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(ShaderType::Compute, s_GeneralShaderTypes));
    EXPECT_FALSE(GraphicsBackend::VulkanDetail::IsRayTracingShaderTypeAllowed(
        static_cast<ShaderType::Mask>(ShaderType::RayGeneration | ShaderType::Miss),
        s_GeneralShaderTypes
    ));
}

TEST_F(RayTracingShaderTableIngressTest, OpacityMicromapPipelineRequestMatchesEnabledFeatureSupport){
    const bool opacityMicromapSupported = device().queryFeatureSupport(Feature::RayTracingOpacityMicromap);
    if(opacityMicromapSupported){
        EXPECT_TRUE(CreateShaderTablePipeline(
            device(),
            arena(),
            ShaderTablePipelineShape::RayGenerationAndMiss,
            true
        ));
        return;
    }

#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(CreateShaderTablePipeline(
            device(),
            arena(),
            ShaderTablePipelineShape::RayGenerationAndMiss,
            true
        ));
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    EXPECT_FALSE(CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::RayGenerationAndMiss,
        true
    ));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("opacity micromap support is unavailable")));
#endif
}

TEST_F(RayTracingShaderTableIngressTest, PipelineFeatureAndCreationFailClosedWhenRequiredEntrypointIsMissing){
    ASSERT_NE(vkCreateRayTracingPipelinesKHR, nullptr);
    ScopedVolkFunctionNull<PFN_vkCreateRayTracingPipelinesKHR> missingCreatePipeline(vkCreateRayTracingPipelinesKHR);

    EXPECT_FALSE(device().queryFeatureSupport(Feature::RayTracingPipeline));
    ExpectRayTracingPipelineRejection([&](){
        return CreateShaderTablePipeline(device(), arena(), ShaderTablePipelineShape::RayGenerationAndMiss);
    });
}

TEST_F(RayTracingShaderTableIngressTest, PipelineCreationRejectsInvalidShaderAndHitGroupIngress){
    const ShaderHandle rayGenerationShader = CreateShaderTableShader(
        device(),
        arena(),
        ShaderType::RayGeneration,
        Name("tests/shader_table/ingress_ray_generation"),
        s_ShaderTableRayGenerationSpirv
    );
    const ShaderHandle closestHitShader = CreateShaderTableShader(
        device(),
        arena(),
        ShaderType::ClosestHit,
        Name("tests/shader_table/ingress_closest_hit"),
        s_ShaderTableClosestHitSpirv
    );
    ASSERT_TRUE(rayGenerationShader);
    ASSERT_TRUE(closestHitShader);

    HeadlessGraphicsScope foreignScope;
    ASSERT_TRUE(foreignScope.initialize());
    GraphicsBackend::Device& foreignDevice = foreignScope.graphics().getDevice();
    const ShaderHandle foreignRayGenerationShader = CreateShaderTableShader(
        foreignDevice,
        foreignScope.arena(),
        ShaderType::RayGeneration,
        Name("tests/shader_table/foreign_ray_generation"),
        s_ShaderTableRayGenerationSpirv
    );
    ASSERT_TRUE(foreignRayGenerationShader);

    RayTracingPipelineDesc nullShaderPipeline(arena());
    RayTracingPipelineShaderDesc nullShader(arena());
    nullShaderPipeline.addShader(nullShader);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(nullShaderPipeline); });

    RayTracingPipelineDesc wrongGeneralStagePipeline(arena());
    RayTracingPipelineShaderDesc wrongGeneralStage(arena());
    wrongGeneralStage.setShader(closestHitShader);
    wrongGeneralStagePipeline.addShader(wrongGeneralStage);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(wrongGeneralStagePipeline); });

    RayTracingPipelineDesc foreignShaderPipeline(arena());
    RayTracingPipelineShaderDesc foreignShader(arena());
    foreignShader.setShader(foreignRayGenerationShader);
    foreignShaderPipeline.addShader(foreignShader);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(foreignShaderPipeline); });

    RayTracingPipelineShaderDesc validGeneralShader(arena());
    validGeneralShader.setShader(rayGenerationShader);

    RayTracingPipelineDesc wrongHitStagePipeline(arena());
    wrongHitStagePipeline.addShader(validGeneralShader);
    RayTracingPipelineHitGroupDesc wrongHitStage(arena());
    wrongHitStage.setClosestHitShader(rayGenerationShader);
    wrongHitStagePipeline.addHitGroup(wrongHitStage);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(wrongHitStagePipeline); });

    RayTracingPipelineDesc proceduralWithoutIntersectionPipeline(arena());
    proceduralWithoutIntersectionPipeline.addShader(validGeneralShader);
    RayTracingPipelineHitGroupDesc proceduralWithoutIntersection(arena());
    proceduralWithoutIntersection.setClosestHitShader(closestHitShader).setIsProceduralPrimitive(true);
    proceduralWithoutIntersectionPipeline.addHitGroup(proceduralWithoutIntersection);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(proceduralWithoutIntersectionPipeline); });

    RayTracingPipelineDesc triangleWithIntersectionPipeline(arena());
    triangleWithIntersectionPipeline.addShader(validGeneralShader);
    RayTracingPipelineHitGroupDesc triangleWithIntersection(arena());
    triangleWithIntersection.setIntersectionShader(closestHitShader);
    triangleWithIntersectionPipeline.addHitGroup(triangleWithIntersection);
    ExpectRayTracingPipelineRejection([&](){ return device().createRayTracingPipeline(triangleWithIntersectionPipeline); });
}

TEST_F(RayTracingShaderTableIngressTest, TypedLookupRejectsEmptyWrongKindAndSameKindAmbiguity){
    const RayTracingPipelineHandle rayGenerationAndMissPipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::RayGenerationAndMiss
    );
    ASSERT_TRUE(rayGenerationAndMissPipeline);
    const RayTracingShaderTableHandle rayGenerationAndMissTable = rayGenerationAndMissPipeline->createShaderTable();
    ASSERT_TRUE(rayGenerationAndMissTable);
    EXPECT_TRUE(rayGenerationAndMissTable->setRayGenerationShader("shared_export"));
    EXPECT_TRUE(rayGenerationAndMissTable->setRayGenerationShader("main"));
    const Object rayGenerationBuffer = rayGenerationAndMissTable->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(rayGenerationBuffer.integer, 0u);
    ExpectShaderTableBoolRejection([&](){ return rayGenerationAndMissTable->setRayGenerationShader("unique_miss"); });
    ExpectShaderTableBoolRejection([&](){ return rayGenerationAndMissTable->setRayGenerationShader("unknown_export"); });
    EXPECT_EQ(
        rayGenerationAndMissTable->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer).integer,
        rayGenerationBuffer.integer
    );
    EXPECT_TRUE(rayGenerationAndMissTable->setRayGenerationShader("shared_export"));
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("shared_export"), 0u);
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("unique_miss"), 1u);
    ExpectShaderTableRecordRejection([&](){ return rayGenerationAndMissTable->addMissShader("main"); });
    ExpectShaderTableRecordRejection([&](){ return rayGenerationAndMissTable->addMissShader("unknown_export"); });
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("shared_export"), 2u);

    const RayTracingPipelineHandle hitPipeline = CreateShaderTablePipeline(device(), arena(), ShaderTablePipelineShape::Hit);
    ASSERT_TRUE(hitPipeline);
    const RayTracingShaderTableHandle hitTable = hitPipeline->createShaderTable();
    ASSERT_TRUE(hitTable);
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 0u);
    EXPECT_EQ(hitTable->addHitGroup("second_hit"), 1u);
    ExpectShaderTableRecordRejection([&](){ return hitTable->addHitGroup("hit_ray_generation"); });
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 2u);

    const RayTracingPipelineHandle callablePipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::Callable
    );
    ASSERT_TRUE(callablePipeline);
    const RayTracingShaderTableHandle callableTable = callablePipeline->createShaderTable();
    ASSERT_TRUE(callableTable);
    ExpectShaderTableRecordRejection([&](){ return callableTable->addMissShader("callable_first"); });
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), 0u);
    EXPECT_EQ(callableTable->addCallableShader("callable_second"), 1u);
    ExpectShaderTableRecordRejection([&](){ return callableTable->addCallableShader("unknown_export"); });
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), 2u);

    const RayTracingPipelineHandle ambiguousRayGenerationAndMissPipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::AmbiguousRayGenerationAndMiss
    );
    ASSERT_TRUE(ambiguousRayGenerationAndMissPipeline);
    const RayTracingShaderTableHandle ambiguousRayGenerationAndMissTable =
        ambiguousRayGenerationAndMissPipeline->createShaderTable();
    ASSERT_TRUE(ambiguousRayGenerationAndMissTable);
    ExpectShaderTableBoolRejection([&](){
        return ambiguousRayGenerationAndMissTable->setRayGenerationShader("duplicate_ray_generation");
    });
    ExpectShaderTableRecordRejection([&](){
        return ambiguousRayGenerationAndMissTable->addMissShader("duplicate_miss");
    });

    const RayTracingPipelineHandle ambiguousHitPipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::AmbiguousHit
    );
    ASSERT_TRUE(ambiguousHitPipeline);
    const RayTracingShaderTableHandle ambiguousHitTable = ambiguousHitPipeline->createShaderTable();
    ASSERT_TRUE(ambiguousHitTable);
    ExpectShaderTableRecordRejection([&](){ return ambiguousHitTable->addHitGroup("duplicate_hit"); });

    const RayTracingPipelineHandle ambiguousCallablePipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::AmbiguousCallable
    );
    ASSERT_TRUE(ambiguousCallablePipeline);
    const RayTracingShaderTableHandle ambiguousCallableTable = ambiguousCallablePipeline->createShaderTable();
    ASSERT_TRUE(ambiguousCallableTable);
    ExpectShaderTableRecordRejection([&](){
        return ambiguousCallableTable->addCallableShader("duplicate_callable");
    });

    ExpectShaderTableBoolRejection([&](){ return rayGenerationAndMissTable->setRayGenerationShader(""); });
    ExpectShaderTableRecordRejection([&](){ return rayGenerationAndMissTable->addMissShader(""); });
    ExpectShaderTableRecordRejection([&](){ return hitTable->addHitGroup(""); });
    ExpectShaderTableRecordRejection([&](){ return callableTable->addCallableShader(""); });

    rayGenerationAndMissTable->clearMissShaders();
    hitTable->clearHitShaders();
    callableTable->clearCallableShaders();
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("unique_miss"), 0u);
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 0u);
    EXPECT_EQ(hitTable->addHitGroup("second_hit"), 1u);
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), 0u);
}

TEST_F(RayTracingShaderTableIngressTest, ImmutableGroupMetadataSurvivesDescriptionAndEntryMutation){
    const RayTracingPipelineHandle pipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::RayGenerationAndMiss
    );
    ASSERT_TRUE(pipeline);

    RayTracingPipelineDesc& mutableDesc = const_cast<RayTracingPipelineDesc&>(pipeline->getDescription());
    ASSERT_GE(mutableDesc.shaders.size(), 3u);
    Shader& fallbackShader = *mutableDesc.shaders[1u].shader.get();
    ShaderDesc& mutableShaderDesc = const_cast<ShaderDesc&>(fallbackShader.getDescription());
    mutableShaderDesc.entryName.assign("mutated_entry");
    mutableShaderDesc.shaderType = ShaderType::Miss;
    mutableDesc.shaders[0u].exportName.assign("mutated_ray_generation");
    mutableDesc.shaders[1u].exportName.assign("mutated_fallback");
    mutableDesc.shaders[2u].exportName.assign("mutated_miss");

    const RayTracingShaderTableHandle table = pipeline->createShaderTable();
    ASSERT_TRUE(table);
    EXPECT_TRUE(table->setRayGenerationShader("shared_export"));
    EXPECT_TRUE(table->setRayGenerationShader("main"));
    ExpectShaderTableBoolRejection([&](){ return table->setRayGenerationShader("mutated_ray_generation"); });
    ExpectShaderTableBoolRejection([&](){ return table->setRayGenerationShader("mutated_entry"); });
    ExpectShaderTableRecordRejection([&](){ return table->addMissShader("main"); });
    EXPECT_EQ(table->addMissShader("shared_export"), 0u);
    ExpectShaderTableRecordRejection([&](){ return table->addMissShader("mutated_miss"); });

    const RayTracingPipelineHandle hitPipeline = CreateShaderTablePipeline(device(), arena(), ShaderTablePipelineShape::Hit);
    ASSERT_TRUE(hitPipeline);
    RayTracingPipelineDesc& mutableHitDesc = const_cast<RayTracingPipelineDesc&>(hitPipeline->getDescription());
    ASSERT_FALSE(mutableHitDesc.hitGroups.empty());
    mutableHitDesc.hitGroups[0u].exportName.assign("mutated_hit");
    const RayTracingShaderTableHandle hitTable = hitPipeline->createShaderTable();
    ASSERT_TRUE(hitTable);
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 0u);
    ExpectShaderTableRecordRejection([&](){ return hitTable->addHitGroup("mutated_hit"); });

    const RayTracingPipelineHandle callablePipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::Callable
    );
    ASSERT_TRUE(callablePipeline);
    RayTracingPipelineDesc& mutableCallableDesc = const_cast<RayTracingPipelineDesc&>(callablePipeline->getDescription());
    ASSERT_GE(mutableCallableDesc.shaders.size(), 2u);
    mutableCallableDesc.shaders[1u].exportName.assign("mutated_callable");
    const RayTracingShaderTableHandle callableTable = callablePipeline->createShaderTable();
    ASSERT_TRUE(callableTable);
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), 0u);
    ExpectShaderTableRecordRejection([&](){ return callableTable->addCallableShader("mutated_callable"); });
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

