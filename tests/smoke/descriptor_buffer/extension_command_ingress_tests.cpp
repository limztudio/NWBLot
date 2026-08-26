// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/common/module.h>
#include <core/graphics/api.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_extension_command_ingress_tests{


static constexpr u32 s_ClusterRayGenerationSpirv[] = {
    0x07230203u, 0x00010500u, 0x0008000bu, 0x00000006u, 0x00000000u, 0x00020011u, 0x0000117fu, 0x0006000au,
    0x5f565053u, 0x5f52484bu, 0x5f796172u, 0x63617274u, 0x00676e69u, 0x0006000bu, 0x00000001u, 0x4c534c47u,
    0x6474732eu, 0x3035342eu, 0x00000000u, 0x0003000eu, 0x00000000u, 0x00000001u, 0x0005000fu, 0x000014c1u,
    0x00000004u, 0x6e69616du, 0x00000000u, 0x00030003u, 0x00000002u, 0x000001ccu, 0x00060004u, 0x455f4c47u,
    0x725f5458u, 0x745f7961u, 0x69636172u, 0x0000676eu, 0x00040005u, 0x00000004u, 0x6e69616du, 0x00000000u,
    0x00020013u, 0x00000002u, 0x00030021u, 0x00000003u, 0x00000002u, 0x00050036u, 0x00000002u, 0x00000004u,
    0x00000000u, 0x00000003u, 0x000200f8u, 0x00000005u, 0x000100fdu, 0x00010038u,
};

[[nodiscard]] BufferHandle CreateClusterBuildInputBuffer(
    GraphicsBackend::Device& device,
    const u64 byteSize,
    const u32 structStride
){
    return device.createBuffer(
        BufferDesc()
            .setByteSize(byteSize)
            .setStructStride(structStride)
            .setIsAccelStructBuildInput(true)
            .setInitialState(ResourceStates::Common)
    );
}

[[nodiscard]] BufferHandle CreateClusterStorageArrayBuffer(
    GraphicsBackend::Device& device,
    const u64 byteSize,
    const u32 structStride
){
    return device.createBuffer(
        BufferDesc()
            .setByteSize(byteSize)
            .setStructStride(structStride)
            .setCanHaveUAVs(true)
            .setIsAccelStructStorage(true)
            .setInitialState(ResourceStates::Common)
    );
}

[[nodiscard]] BufferHandle CreateCooperativeVectorBuffer(GraphicsBackend::Device& device){
    return device.createBuffer(
        BufferDesc()
            .setByteSize(512u)
            .setStructStride(sizeof(f32))
            .setCanHaveUAVs(true)
            .setInitialState(ResourceStates::Common)
    );
}

[[nodiscard]] RayTracingShaderTableHandle CreateClusterShaderTable(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena
){
    ShaderDesc shaderDesc(arena);
    shaderDesc.setShaderType(ShaderType::RayGeneration).setDebugName(Name("tests/cluster/ray_generation"));
    const ShaderHandle shader = device.createShader(shaderDesc, s_ClusterRayGenerationSpirv, sizeof(s_ClusterRayGenerationSpirv));
    if(!shader)
        return nullptr;

    RayTracingPipelineDesc pipelineDesc(arena);
    pipelineDesc.addBindingLayout(device.getDescriptorHeap().getResourceLayout());
    pipelineDesc.setAllowClusterAccelerationStructures(true);

    RayTracingPipelineShaderDesc rayGeneration(arena);
    rayGeneration.setShader(shader).setExportName("cluster_ray_generation");
    pipelineDesc.addShader(rayGeneration);

    const RayTracingPipelineHandle pipeline = device.createRayTracingPipeline(pipelineDesc);
    if(!pipeline)
        return nullptr;

    RayTracingShaderTableHandle table = pipeline->createShaderTable();
    if(!table || !table->setRayGenerationShader("cluster_ray_generation"))
        return nullptr;
    return table;
}

[[nodiscard]] RayTracingClusterOperationParams MakeClusterMoveParams(){
    RayTracingClusterOperationParams params;
    params.maxArgCount = 1u;
    params.type = RayTracingClusterOperationType::Move;
    params.mode = RayTracingClusterOperationMode::ExplicitDestinations;
    params.move.type = RayTracingClusterOperationMoveType::ClusterLevel;
    params.move.maxBytes = 64u;
    return params;
}

[[nodiscard]] RayTracingClusterOperationDesc MakeExplicitClusterOperation(
    const RayTracingClusterOperationParams& params,
    Buffer& source,
    Buffer& count,
    Buffer& addresses,
    Buffer& sizes,
    const u64 scratchByteSize,
    const u64 countOffset
){
    RayTracingClusterOperationDesc desc;
    desc.params = params;
    desc.scratchSizeInBytes = scratchByteSize;
    desc.inIndirectArgCountBuffer = &count;
    desc.inIndirectArgCountOffsetInBytes = countOffset;
    desc.inIndirectArgsBuffer = &source;
    desc.inOutAddressesBuffer = &addresses;
    desc.outSizesBuffer = &sizes;
    return desc;
}

[[nodiscard]] bool GetAlignedMatrixOffset(Buffer& buffer, u64& outOffset){
    VkDeviceAddress alignedAddress = 0u;
    if(!AlignUpU64Checked(buffer.getGpuVirtualAddress(), 64u, alignedAddress))
        return false;
    outOffset = alignedAddress - buffer.getGpuVirtualAddress();
    return outOffset <= buffer.getCreationDescription().byteSize - 64u;
}

[[nodiscard]] CooperativeVectorConvertMatrixLayoutDesc MakeFloat32MatrixConversion(
    Buffer& source,
    const u64 sourceOffset,
    Buffer& destination,
    const u64 destinationOffset
){
    CooperativeVectorConvertMatrixLayoutDesc desc;
    desc.numRows = 2u;
    desc.numColumns = 2u;
    desc.src.buffer = &source;
    desc.src.offset = sourceOffset;
    desc.src.type = CooperativeVectorDataType::Float32;
    desc.src.layout = CooperativeVectorMatrixLayout::RowMajor;
    desc.src.size = 20u;
    desc.src.stride = 12u;
    desc.dst.buffer = &destination;
    desc.dst.offset = destinationOffset;
    desc.dst.type = CooperativeVectorDataType::Float32;
    desc.dst.layout = CooperativeVectorMatrixLayout::RowMajor;
    desc.dst.size = 20u;
    desc.dst.stride = 12u;
    return desc;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(ExtensionCommandIngressHostContractTest, ClusterBlasIndirectArgsMatchVulkanAbiAndDefaultValidStride){
    static_assert(sizeof(IndirectArgs) == sizeof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV));
    static_assert(alignof(IndirectArgs) == alignof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV));
    static_assert(
        offsetof(IndirectArgs, clusterCount)
        == offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferencesCount)
    );
    static_assert(
        offsetof(IndirectArgs, clusterReferencesStride)
        == offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferencesStride)
    );
    static_assert(
        offsetof(IndirectArgs, clusterAddresses)
        == offsetof(VkClusterAccelerationStructureBuildClustersBottomLevelInfoNV, clusterReferences)
    );

    const IndirectArgs args{};
    EXPECT_EQ(args.clusterReferencesStride, static_cast<u32>(sizeof(GpuVirtualAddress)));
    EXPECT_GE(args.clusterReferencesStride, 8u);
}

TEST(ExtensionCommandIngressHostContractTest, ClusterDestinationTopologyMatchesOperationModes){
    struct DestinationTopologyCase{
        RayTracingClusterOperationMode::Enum mode;
        bool hasAddresses;
        bool hasSizes;
        bool hasImplicitDestination;
        bool expected;
    };
    constexpr Array<DestinationTopologyCase, 15u> cases = {
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, false, false, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, true, true, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, false, false, true, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, true, false, true, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, false, true, true, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ImplicitDestinations, true, true, true, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ExplicitDestinations, true, true, false, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ExplicitDestinations, false, true, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ExplicitDestinations, true, false, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::ExplicitDestinations, true, true, true, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::GetSizes, false, true, false, true },
        DestinationTopologyCase{ RayTracingClusterOperationMode::GetSizes, false, false, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::GetSizes, true, true, false, false },
        DestinationTopologyCase{ RayTracingClusterOperationMode::GetSizes, false, true, true, false },
        DestinationTopologyCase{ static_cast<RayTracingClusterOperationMode::Enum>(Limit<u8>::s_Max), true, true, true, false },
    };

    for(const DestinationTopologyCase& testCase : cases){
        EXPECT_EQ(RayTracingClusterOperationMode::IsDestinationTopologyValid(
            testCase.mode,
            testCase.hasAddresses,
            testCase.hasSizes,
            testCase.hasImplicitDestination
        ), testCase.expected);
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class ExtensionCommandIngressTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);
        s_scope = MakeUnique<HeadlessGraphicsScope>();
        s_runtimeReady = s_scope->initialize();
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_runtimeReady && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled extension command ingress tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeReady = false;
    }

    virtual void SetUp()override{
        if(!s_runtimeReady)
            GTEST_SKIP() << "Extension command ingress: no usable headless graphics device.";
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_runtimeReady;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool ExtensionCommandIngressTest::s_runtimeReady = false;
UniquePtr<HeadlessGraphicsScope> ExtensionCommandIngressTest::s_scope;
Optional<CapturingLogger> ExtensionCommandIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> ExtensionCommandIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ExtensionCommandIngressTest, ClusterFeatureAdvertisementRequiresPipelineAndEntrypoints){
    if(!device().queryFeatureSupport(Feature::RayTracingClusters))
        GTEST_SKIP() << "Cluster feature query: VK_NV_cluster_acceleration_structure is unavailable.";

    EXPECT_TRUE(device().queryFeatureSupport(Feature::RayTracingPipeline));
    const PFN_vkGetClusterAccelerationStructureBuildSizesNV originalSizeQuery = vkGetClusterAccelerationStructureBuildSizesNV;
    const PFN_vkCmdBuildClusterAccelerationStructureIndirectNV originalBuildCommand = vkCmdBuildClusterAccelerationStructureIndirectNV;
    ASSERT_NE(originalSizeQuery, nullptr);
    ASSERT_NE(originalBuildCommand, nullptr);

    vkGetClusterAccelerationStructureBuildSizesNV = nullptr;
    const bool advertisedWithoutSizeQuery = device().queryFeatureSupport(Feature::RayTracingClusters);
    vkGetClusterAccelerationStructureBuildSizesNV = originalSizeQuery;
    vkCmdBuildClusterAccelerationStructureIndirectNV = nullptr;
    const bool advertisedWithoutBuildCommand = device().queryFeatureSupport(Feature::RayTracingClusters);
    vkCmdBuildClusterAccelerationStructureIndirectNV = originalBuildCommand;

    EXPECT_FALSE(advertisedWithoutSizeQuery);
    EXPECT_FALSE(advertisedWithoutBuildCommand);
}

TEST_F(ExtensionCommandIngressTest, ClusterExplicitDynamicCountRecordsAndRetainsValidatedArrays){
    if(!device().queryFeatureSupport(Feature::RayTracingClusters))
        GTEST_SKIP() << "Cluster command ingress: VK_NV_cluster_acceleration_structure is unavailable.";

    const RayTracingShaderTableHandle table = __hidden_extension_command_ingress_tests::CreateClusterShaderTable(device(), arena());
    ASSERT_TRUE(table);

    const RayTracingClusterOperationParams params = __hidden_extension_command_ingress_tests::MakeClusterMoveParams();
    const RayTracingClusterOperationSizeInfo sizeInfo = device().getClusterOperationSizeInfo(params);
    if(sizeInfo.resultMaxSizeInBytes == 0u && sizeInfo.scratchSizeInBytes == 0u)
        GTEST_SKIP() << "Cluster command ingress: driver returned no move-operation size information.";

    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(
        device(),
        sizeof(VkClusterAccelerationStructureMoveObjectsInfoNV),
        0u
    );
    const BufferHandle count = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(device(), sizeof(u32), sizeof(u32));
    const BufferHandle addresses = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u64), sizeof(u64));
    const BufferHandle sizes = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u32), sizeof(u32));
    ASSERT_TRUE(source);
    ASSERT_TRUE(count);
    ASSERT_TRUE(addresses);
    ASSERT_TRUE(sizes);

    const u32 sourceReferences = source->getReferenceCount();
    const u32 countReferences = count->getReferenceCount();
    const u32 addressReferences = addresses->getReferenceCount();
    const u32 sizeReferences = sizes->getReferenceCount();
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());

    const RayTracingClusterOperationDesc desc = __hidden_extension_command_ingress_tests::MakeExplicitClusterOperation(
        params,
        *source,
        *count,
        *addresses,
        *sizes,
        sizeInfo.scratchSizeInBytes,
        0u
    );
    commandList->executeMultiIndirectClusterOperation(desc);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(source->getReferenceCount(), sourceReferences + 1u);
    EXPECT_EQ(count->getReferenceCount(), countReferences + 1u);
    EXPECT_EQ(addresses->getReferenceCount(), addressReferences + 1u);
    EXPECT_EQ(sizes->getReferenceCount(), sizeReferences + 1u);
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, ClusterImplicitDestinationsAllowIndependentOptionalResultArrays){
    if(!device().queryFeatureSupport(Feature::RayTracingClusters))
        GTEST_SKIP() << "Cluster command ingress: VK_NV_cluster_acceleration_structure is unavailable.";

    const RayTracingShaderTableHandle table = __hidden_extension_command_ingress_tests::CreateClusterShaderTable(device(), arena());
    ASSERT_TRUE(table);

    RayTracingClusterOperationParams params = __hidden_extension_command_ingress_tests::MakeClusterMoveParams();
    params.mode = RayTracingClusterOperationMode::ImplicitDestinations;
    const RayTracingClusterOperationSizeInfo sizeInfo = device().getClusterOperationSizeInfo(params);
    if(sizeInfo.resultMaxSizeInBytes == 0u)
        GTEST_SKIP() << "Cluster command ingress: driver returned no implicit-destination size information.";

    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(
        device(),
        sizeof(VkClusterAccelerationStructureMoveObjectsInfoNV),
        0u
    );
    const BufferHandle count = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(device(), sizeof(u32), sizeof(u32));
    const BufferHandle addresses = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(
        device(),
        sizeof(VkDeviceAddress),
        sizeof(VkDeviceAddress)
    );
    const BufferHandle sizes = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u32), sizeof(u32));
    const BufferHandle destination = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(
        device(),
        Max<u64>(sizeInfo.resultMaxSizeInBytes, params.move.maxBytes),
        1u
    );
    ASSERT_TRUE(source);
    ASSERT_TRUE(count);
    ASSERT_TRUE(addresses);
    ASSERT_TRUE(sizes);
    ASSERT_TRUE(destination);

    const auto recordImplicitOperation = [&](const bool provideAddresses, const bool provideSizes){
        RayTracingClusterOperationDesc desc;
        desc.params = params;
        desc.scratchSizeInBytes = sizeInfo.scratchSizeInBytes;
        desc.inIndirectArgCountBuffer = count.get();
        desc.inIndirectArgsBuffer = source.get();
        desc.inOutAddressesBuffer = provideAddresses ? addresses.get() : nullptr;
        desc.outSizesBuffer = provideSizes ? sizes.get() : nullptr;
        desc.outAccelerationStructuresBuffer = destination.get();

        const u32 sourceReferences = source->getReferenceCount();
        const u32 countReferences = count->getReferenceCount();
        const u32 addressReferences = addresses->getReferenceCount();
        const u32 sizeReferences = sizes->getReferenceCount();
        const u32 destinationReferences = destination->getReferenceCount();
        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
        commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
        ASSERT_FALSE(commandList->commandRecordingFailed());
        commandList->executeMultiIndirectClusterOperation(desc);
        EXPECT_FALSE(commandList->commandRecordingFailed());
        EXPECT_EQ(source->getReferenceCount(), sourceReferences + 1u);
        EXPECT_EQ(count->getReferenceCount(), countReferences + 1u);
        EXPECT_EQ(addresses->getReferenceCount(), addressReferences + (provideAddresses ? 1u : 0u));
        EXPECT_EQ(sizes->getReferenceCount(), sizeReferences + (provideSizes ? 1u : 0u));
        EXPECT_EQ(destination->getReferenceCount(), destinationReferences + 1u);
        if(!provideAddresses)
            EXPECT_FALSE(commandList->hasExplicitBufferState(addresses.get()));
        if(!provideSizes)
            EXPECT_FALSE(commandList->hasExplicitBufferState(sizes.get()));
        commandList->close();
    };

    recordImplicitOperation(false, true);
    recordImplicitOperation(true, false);
    recordImplicitOperation(false, false);
}

TEST_F(ExtensionCommandIngressTest, ClusterMisalignedDynamicCountRejectsBeforeStateOrRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingClusters))
        GTEST_SKIP() << "Cluster command ingress: VK_NV_cluster_acceleration_structure is unavailable.";

    const RayTracingShaderTableHandle table = __hidden_extension_command_ingress_tests::CreateClusterShaderTable(device(), arena());
    ASSERT_TRUE(table);
    const RayTracingClusterOperationParams params = __hidden_extension_command_ingress_tests::MakeClusterMoveParams();
    const RayTracingClusterOperationSizeInfo sizeInfo = device().getClusterOperationSizeInfo(params);

    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(
        device(),
        sizeof(VkClusterAccelerationStructureMoveObjectsInfoNV),
        0u
    );
    const BufferHandle count = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(device(), 8u, sizeof(u32));
    const BufferHandle addresses = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u64), sizeof(u64));
    const BufferHandle sizes = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u32), sizeof(u32));
    ASSERT_TRUE(source);
    ASSERT_TRUE(count);
    ASSERT_TRUE(addresses);
    ASSERT_TRUE(sizes);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());

    const RayTracingClusterOperationDesc desc = __hidden_extension_command_ingress_tests::MakeExplicitClusterOperation(
        params,
        *source,
        *count,
        *addresses,
        *sizes,
        sizeInfo.scratchSizeInBytes,
        1u
    );
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->executeMultiIndirectClusterOperation(desc);
    }, "");
#else
    const u32 sourceReferences = source->getReferenceCount();
    const u32 countReferences = count->getReferenceCount();
    const u32 addressReferences = addresses->getReferenceCount();
    const u32 sizeReferences = sizes->getReferenceCount();
    commandList->executeMultiIndirectClusterOperation(desc);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(source->getReferenceCount(), sourceReferences);
    EXPECT_EQ(count->getReferenceCount(), countReferences);
    EXPECT_EQ(addresses->getReferenceCount(), addressReferences);
    EXPECT_EQ(sizes->getReferenceCount(), sizeReferences);
    EXPECT_FALSE(commandList->hasExplicitBufferState(source.get()));
    EXPECT_FALSE(commandList->hasExplicitBufferState(count.get()));
    EXPECT_FALSE(commandList->hasExplicitBufferState(addresses.get()));
    EXPECT_FALSE(commandList->hasExplicitBufferState(sizes.get()));
#endif
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, ClusterMutuallyExclusivePreferenceFlagsRejectBeforeRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingClusters))
        GTEST_SKIP() << "Cluster command ingress: VK_NV_cluster_acceleration_structure is unavailable.";

    const RayTracingShaderTableHandle table = __hidden_extension_command_ingress_tests::CreateClusterShaderTable(device(), arena());
    ASSERT_TRUE(table);
    RayTracingClusterOperationParams params = __hidden_extension_command_ingress_tests::MakeClusterMoveParams();
    params.flags = static_cast<RayTracingClusterOperationFlags::Mask>(
        RayTracingClusterOperationFlags::FastTrace | RayTracingClusterOperationFlags::FastBuild
    );
    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(
        device(),
        sizeof(VkClusterAccelerationStructureMoveObjectsInfoNV),
        0u
    );
    const BufferHandle count = __hidden_extension_command_ingress_tests::CreateClusterBuildInputBuffer(device(), sizeof(u32), sizeof(u32));
    const BufferHandle addresses = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u64), sizeof(u64));
    const BufferHandle sizes = __hidden_extension_command_ingress_tests::CreateClusterStorageArrayBuffer(device(), sizeof(u32), sizeof(u32));
    ASSERT_TRUE(source);
    ASSERT_TRUE(count);
    ASSERT_TRUE(addresses);
    ASSERT_TRUE(sizes);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->setRayTracingState(RayTracingState().setShaderTable(table.get()));
    ASSERT_FALSE(commandList->commandRecordingFailed());
    const RayTracingClusterOperationDesc desc = __hidden_extension_command_ingress_tests::MakeExplicitClusterOperation(
        params,
        *source,
        *count,
        *addresses,
        *sizes,
        0u,
        0u
    );
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->executeMultiIndirectClusterOperation(desc);
    }, "");
#else
    const u32 sourceReferences = source->getReferenceCount();
    commandList->executeMultiIndirectClusterOperation(desc);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(source->getReferenceCount(), sourceReferences);
    EXPECT_FALSE(commandList->hasExplicitBufferState(source.get()));
#endif
    commandList->close();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(ExtensionCommandIngressTest, CooperativeVectorValidStandardLayoutsRecordAndRetainBothBuffers){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
    const BufferHandle destination = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    u64 sourceOffset = 0u;
    u64 destinationOffset = 0u;
    ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*source, sourceOffset));
    ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*destination, destinationOffset));

    const u32 sourceReferences = source->getReferenceCount();
    const u32 destinationReferences = destination->getReferenceCount();
    const CooperativeVectorConvertMatrixLayoutDesc desc = __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(
        *source,
        sourceOffset,
        *destination,
        destinationOffset
    );
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->convertCoopVecMatrices(&desc, 1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(source->getReferenceCount(), sourceReferences + 1u);
    EXPECT_EQ(destination->getReferenceCount(), destinationReferences + 1u);
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, CooperativeVectorOptimalLayoutIgnoresNonzeroStride){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    const usize optimalSize = device().getCoopVecMatrixSize(
        CooperativeVectorDataType::Float32,
        CooperativeVectorMatrixLayout::InferencingOptimal,
        2,
        2
    );
    if(optimalSize == 0u)
        GTEST_SKIP() << "Cooperative-vector ingress: the driver returned no optimal-layout size.";

    const BufferHandle source = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
    const BufferHandle destination = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
    ASSERT_TRUE(source);
    ASSERT_TRUE(destination);
    u64 sourceOffset = 0u;
    u64 destinationOffset = 0u;
    ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*source, sourceOffset));
    ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*destination, destinationOffset));
    if(optimalSize > destination->getCreationDescription().byteSize - destinationOffset)
        GTEST_SKIP() << "Cooperative-vector ingress: the queried optimal layout exceeds the smoke buffer.";

    CooperativeVectorConvertMatrixLayoutDesc desc = __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(
        *source,
        sourceOffset,
        *destination,
        destinationOffset
    );
    desc.dst.layout = CooperativeVectorMatrixLayout::InferencingOptimal;
    desc.dst.size = optimalSize;
    desc.dst.stride = Limit<usize>::s_Max;

    const u32 sourceReferences = source->getReferenceCount();
    const u32 destinationReferences = destination->getReferenceCount();
    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    commandList->convertCoopVecMatrices(&desc, 1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
    EXPECT_EQ(source->getReferenceCount(), sourceReferences + 1u);
    EXPECT_EQ(destination->getReferenceCount(), destinationReferences + 1u);
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, CooperativeVectorInvalidLaterDescriptorRejectsWholeBatchTransactionally){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    Array<BufferHandle, 4u> buffers = {
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
    };
    Array<u64, 4u> offsets = {};
    for(usize i = 0u; i < buffers.size(); ++i){
        ASSERT_TRUE(buffers[i]);
        ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*buffers[i], offsets[i]));
    }

    Array<CooperativeVectorConvertMatrixLayoutDesc, 2u> descs = {
        __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(*buffers[0], offsets[0], *buffers[1], offsets[1]),
        __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(*buffers[2], offsets[2], *buffers[3], offsets[3]),
    };
    descs[1].src.stride = 8u;

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->convertCoopVecMatrices(descs.data(), descs.size());
    }, "");
#else
    Array<u32, 4u> referenceCounts = {};
    for(usize i = 0u; i < buffers.size(); ++i)
        referenceCounts[i] = buffers[i]->getReferenceCount();
    commandList->convertCoopVecMatrices(descs.data(), descs.size());
    EXPECT_TRUE(commandList->commandRecordingFailed());
    for(usize i = 0u; i < buffers.size(); ++i){
        EXPECT_EQ(buffers[i]->getReferenceCount(), referenceCounts[i]);
        EXPECT_FALSE(commandList->hasExplicitBufferState(buffers[i].get()));
    }
#endif
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, CooperativeVectorAddressAndRangeBoundariesRejectBeforePublication){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    const auto expectRejection = [&](const bool misalignSource, const bool undersizeSource){
        const BufferHandle source = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
        const BufferHandle destination = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
        ASSERT_TRUE(source);
        ASSERT_TRUE(destination);
        u64 sourceOffset = 0u;
        u64 destinationOffset = 0u;
        ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*source, sourceOffset));
        ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*destination, destinationOffset));

        CooperativeVectorConvertMatrixLayoutDesc desc = __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(
            *source,
            misalignSource ? sourceOffset + 1u : sourceOffset,
            *destination,
            destinationOffset
        );
        if(undersizeSource)
            desc.src.size = 19u;

        CommandListHandle commandList = device().createCommandList();
        ASSERT_TRUE(commandList);
        commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            commandList->convertCoopVecMatrices(&desc, 1u);
        }, "");
#else
        const u32 sourceReferences = source->getReferenceCount();
        const u32 destinationReferences = destination->getReferenceCount();
        commandList->convertCoopVecMatrices(&desc, 1u);
        EXPECT_TRUE(commandList->commandRecordingFailed());
        EXPECT_EQ(source->getReferenceCount(), sourceReferences);
        EXPECT_EQ(destination->getReferenceCount(), destinationReferences);
        EXPECT_FALSE(commandList->hasExplicitBufferState(source.get()));
        EXPECT_FALSE(commandList->hasExplicitBufferState(destination.get()));
#endif
        commandList->close();
    };

    expectRejection(true, false);
    expectRejection(false, true);
}

TEST_F(ExtensionCommandIngressTest, CooperativeVectorIntraDescriptorOverlapRejectsBeforePublication){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    const BufferHandle shared = __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device());
    ASSERT_TRUE(shared);
    u64 offset = 0u;
    ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*shared, offset));
    const CooperativeVectorConvertMatrixLayoutDesc desc = __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(
        *shared,
        offset,
        *shared,
        offset
    );

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->convertCoopVecMatrices(&desc, 1u);
    }, "");
#else
    const u32 references = shared->getReferenceCount();
    commandList->convertCoopVecMatrices(&desc, 1u);
    EXPECT_TRUE(commandList->commandRecordingFailed());
    EXPECT_EQ(shared->getReferenceCount(), references);
    EXPECT_FALSE(commandList->hasExplicitBufferState(shared.get()));
#endif
    commandList->close();
}

TEST_F(ExtensionCommandIngressTest, CooperativeVectorCrossDescriptorOverlapRejectsBeforePublication){
    if(!device().queryFeatureSupport(Feature::CooperativeVectorInferencing))
        GTEST_SKIP() << "Cooperative-vector ingress: VK_NV_cooperative_vector is unavailable.";

    Array<BufferHandle, 3u> buffers = {
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
        __hidden_extension_command_ingress_tests::CreateCooperativeVectorBuffer(device()),
    };
    Array<u64, 3u> offsets = {};
    for(usize i = 0u; i < buffers.size(); ++i){
        ASSERT_TRUE(buffers[i]);
        ASSERT_TRUE(__hidden_extension_command_ingress_tests::GetAlignedMatrixOffset(*buffers[i], offsets[i]));
    }

    const Array<CooperativeVectorConvertMatrixLayoutDesc, 2u> descs = {
        __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(*buffers[0], offsets[0], *buffers[1], offsets[1]),
        __hidden_extension_command_ingress_tests::MakeFloat32MatrixConversion(*buffers[1], offsets[1], *buffers[2], offsets[2]),
    };

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->convertCoopVecMatrices(descs.data(), descs.size());
    }, "");
#else
    Array<u32, 3u> referenceCounts = {};
    for(usize i = 0u; i < buffers.size(); ++i)
        referenceCounts[i] = buffers[i]->getReferenceCount();
    commandList->convertCoopVecMatrices(descs.data(), descs.size());
    EXPECT_TRUE(commandList->commandRecordingFailed());
    for(usize i = 0u; i < buffers.size(); ++i){
        EXPECT_EQ(buffers[i]->getReferenceCount(), referenceCounts[i]);
        EXPECT_FALSE(commandList->hasExplicitBufferState(buffers[i].get()));
    }
#endif
    commandList->close();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

