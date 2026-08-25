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
    const ShaderTablePipelineShape::Enum shape
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
    pipelineDesc.addBindingLayout(device.getDescriptorHeap().getResourceLayout());
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
    ExpectShaderTableBoolRejection([&](){ return rayGenerationAndMissTable->setRayGenerationShader("unique_miss"); });
    ExpectShaderTableBoolRejection([&](){ return rayGenerationAndMissTable->setRayGenerationShader("unknown_export"); });
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("shared_export"), 0u);
    EXPECT_EQ(rayGenerationAndMissTable->addMissShader("unique_miss"), 1u);
    ExpectShaderTableRecordRejection([&](){ return rayGenerationAndMissTable->addMissShader("main"); });
    ExpectShaderTableRecordRejection([&](){ return rayGenerationAndMissTable->addMissShader("unknown_export"); });

    const RayTracingPipelineHandle hitPipeline = CreateShaderTablePipeline(device(), arena(), ShaderTablePipelineShape::Hit);
    ASSERT_TRUE(hitPipeline);
    const RayTracingShaderTableHandle hitTable = hitPipeline->createShaderTable();
    ASSERT_TRUE(hitTable);
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 0u);
    EXPECT_EQ(hitTable->addHitGroup("second_hit"), 1u);
    ExpectShaderTableRecordRejection([&](){ return hitTable->addHitGroup("hit_ray_generation"); });

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

#if !defined(NWB_FINAL)
TEST_F(RayTracingShaderTableIngressTest, AllocationAndNewMapFailuresPreserveEmptyAndNonemptyState){
    const RayTracingPipelineHandle pipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::RayGenerationAndMiss
    );
    ASSERT_TRUE(pipeline);
    const RayTracingShaderTableHandle table = pipeline->createShaderTable();
    ASSERT_TRUE(table);

    ASSERT_TRUE(table->setRayGenerationShader("shared_export"));
    const Object originalRayGenerationBuffer = table->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer);
    ASSERT_NE(originalRayGenerationBuffer.integer, 0u);

    table->rejectNextBufferAllocationForTesting();
    EXPECT_FALSE(table->setRayGenerationShader("main"));
    EXPECT_EQ(table->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer).integer, originalRayGenerationBuffer.integer);
    table->rejectNextNewBufferMapForTesting();
    EXPECT_FALSE(table->setRayGenerationShader("main"));
    EXPECT_EQ(table->getNativeHandle(GraphicsBackend::ObjectTypes::VK_Buffer).integer, originalRayGenerationBuffer.integer);

    table->rejectNextBufferAllocationForTesting();
    EXPECT_EQ(table->addMissShader("unique_miss"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(table->addMissShader("unique_miss"), 0u);
    table->rejectNextNewBufferMapForTesting();
    EXPECT_EQ(table->addMissShader("shared_export"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(table->addMissShader("shared_export"), 1u);

    const RayTracingPipelineHandle callablePipeline = CreateShaderTablePipeline(
        device(),
        arena(),
        ShaderTablePipelineShape::Callable
    );
    ASSERT_TRUE(callablePipeline);
    const RayTracingShaderTableHandle callableTable = callablePipeline->createShaderTable();
    ASSERT_TRUE(callableTable);
    callableTable->rejectNextBufferAllocationForTesting();
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(callableTable->addCallableShader("callable_first"), 0u);
    callableTable->rejectNextNewBufferMapForTesting();
    EXPECT_EQ(callableTable->addCallableShader("callable_second"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(callableTable->addCallableShader("callable_second"), 1u);

    const RayTracingPipelineHandle hitPipeline = CreateShaderTablePipeline(device(), arena(), ShaderTablePipelineShape::Hit);
    ASSERT_TRUE(hitPipeline);
    const RayTracingShaderTableHandle hitTable = hitPipeline->createShaderTable();
    ASSERT_TRUE(hitTable);
    hitTable->rejectNextBufferAllocationForTesting();
    ExpectShaderTableRecordRejection([&](){ return hitTable->addHitGroup("unknown_export"); });
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(hitTable->addHitGroup("unique_hit"), 0u);

    table->clearMissShaders();
    table->rejectNextNewBufferMapForTesting();
    EXPECT_EQ(table->addMissShader("unique_miss"), s_InvalidRayTracingShaderTableRecordIndex);
    EXPECT_EQ(table->addMissShader("unique_miss"), 0u);
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////



};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

