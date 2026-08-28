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


namespace __hidden_ray_tracing_build_ingress_tests{


static constexpr RayTracingOpacityMicromapUsageCount s_OmmUsageCount{
    1u,
    0u,
    OpacityMicromapFormat::OC1_2_State,
};


struct NativeBufferAddressQuery{
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceAddress address = 0u;
};

struct NativeBuildScratchCommand{
    VkDeviceAddress scratchAddress = 0u;
    VkAccelerationStructureKHR destination = VK_NULL_HANDLE;
};

struct NativeBuildScratchReuseCapture{
    NativeBufferAddressQuery addressQueries[2u] = {};
    NativeBuildScratchCommand buildCommands[2u] = {};
    u32 addressQueryCount = 0u;
    u32 buildCommandCount = 0u;
};

class ScopedNativeBuildScratchReuseTrace final : NoCopy{
private:
    static thread_local NativeBuildScratchReuseCapture* s_activeCapture;
    static PFN_vkGetBufferDeviceAddress s_forwardGetBufferDeviceAddress;
    static PFN_vkCmdBuildAccelerationStructuresKHR s_forwardCmdBuildAccelerationStructures;

    [[nodiscard]] static VKAPI_ATTR VkDeviceAddress VKAPI_CALL InterceptGetBufferDeviceAddress(
        const VkDevice device,
        const VkBufferDeviceAddressInfo* const addressInfo
    ){
        if(!s_forwardGetBufferDeviceAddress)
            return 0u;

        const VkDeviceAddress address = s_forwardGetBufferDeviceAddress(device, addressInfo);
        NativeBuildScratchReuseCapture* const capture = s_activeCapture;
        if(capture && addressInfo){
            const u32 queryIndex = capture->addressQueryCount;
            if(queryIndex < LengthOf(capture->addressQueries)){
                capture->addressQueries[queryIndex].buffer = addressInfo->buffer;
                capture->addressQueries[queryIndex].address = address;
            }
            ++capture->addressQueryCount;
        }
        return address;
    }
    static VKAPI_ATTR void VKAPI_CALL InterceptCmdBuildAccelerationStructures(
        const VkCommandBuffer commandBuffer,
        const u32 infoCount,
        const VkAccelerationStructureBuildGeometryInfoKHR* const buildInfos,
        const VkAccelerationStructureBuildRangeInfoKHR* const* const buildRangeInfos
    ){
        NativeBuildScratchReuseCapture* const capture = s_activeCapture;
        if(capture && infoCount > 0u && buildInfos){
            const u32 commandIndex = capture->buildCommandCount;
            if(commandIndex < LengthOf(capture->buildCommands)){
                capture->buildCommands[commandIndex].scratchAddress = buildInfos[0u].scratchData.deviceAddress;
                capture->buildCommands[commandIndex].destination = buildInfos[0u].dstAccelerationStructure;
            }
            ++capture->buildCommandCount;
        }
        if(s_forwardCmdBuildAccelerationStructures){
            s_forwardCmdBuildAccelerationStructures(
                commandBuffer,
                infoCount,
                buildInfos,
                buildRangeInfos
            );
        }
    }


public:
    explicit ScopedNativeBuildScratchReuseTrace(NativeBuildScratchReuseCapture& capture)
        : m_originalGetBufferDeviceAddress(vkGetBufferDeviceAddress)
        , m_originalCmdBuildAccelerationStructures(vkCmdBuildAccelerationStructuresKHR)
    {
        capture = {};
        if(s_activeCapture || !m_originalGetBufferDeviceAddress || !m_originalCmdBuildAccelerationStructures)
            return;

        s_forwardGetBufferDeviceAddress = m_originalGetBufferDeviceAddress;
        s_forwardCmdBuildAccelerationStructures = m_originalCmdBuildAccelerationStructures;
        s_activeCapture = &capture;
        vkGetBufferDeviceAddress = &ScopedNativeBuildScratchReuseTrace::InterceptGetBufferDeviceAddress;
        vkCmdBuildAccelerationStructuresKHR = &ScopedNativeBuildScratchReuseTrace::InterceptCmdBuildAccelerationStructures;
        m_armed = true;
    }
    ~ScopedNativeBuildScratchReuseTrace(){
        if(!m_armed)
            return;

        vkGetBufferDeviceAddress = m_originalGetBufferDeviceAddress;
        vkCmdBuildAccelerationStructuresKHR = m_originalCmdBuildAccelerationStructures;
        s_activeCapture = nullptr;
    }


public:
    [[nodiscard]] bool valid()const noexcept{ return m_armed; }


private:
    PFN_vkGetBufferDeviceAddress m_originalGetBufferDeviceAddress = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR m_originalCmdBuildAccelerationStructures = nullptr;
    bool m_armed = false;
};

thread_local NativeBuildScratchReuseCapture* ScopedNativeBuildScratchReuseTrace::s_activeCapture = nullptr;
PFN_vkGetBufferDeviceAddress ScopedNativeBuildScratchReuseTrace::s_forwardGetBufferDeviceAddress = nullptr;
PFN_vkCmdBuildAccelerationStructuresKHR ScopedNativeBuildScratchReuseTrace::s_forwardCmdBuildAccelerationStructures = nullptr;

[[nodiscard]] BufferHandle CreateBuildInputBuffer(
    GraphicsBackend::Device& device,
    const u64 byteSize,
    const u32 structStride
){
    return device.createBuffer(
        BufferDesc()
            .setByteSize(byteSize)
            .setStructStride(structStride)
            .setIsAccelStructBuildInput(true)
            .setCpuAccess(CpuAccessMode::Write)
            .setInitialState(ResourceStates::Common)
    );
}

struct OpacityMicromapBuildInputs{
    BufferHandle input;
    BufferHandle triangleDescs;
    u64 inputOffset = 0u;
    u64 triangleDescOffset = 0u;
};

[[nodiscard]] bool ResolveAlignedBufferOffset(
    const Buffer& buffer,
    const u64 alignment,
    const u64 byteSize,
    u64& outOffset
){
    const u64 baseAddress = buffer.getGpuVirtualAddress();
    u64 alignedAddress = 0u;
    if(baseAddress == 0u || !AlignUpChecked(baseAddress, alignment, alignedAddress))
        return false;

    outOffset = alignedAddress - baseAddress;
    const u64 bufferByteSize = buffer.getCreationDescription().byteSize;
    return outOffset <= bufferByteSize && byteSize <= bufferByteSize - outOffset;
}

[[nodiscard]] bool InitializeOpacityMicromapBuildInputs(
    GraphicsBackend::Device& device,
    OpacityMicromapBuildInputs& outInputs
){
    constexpr u64 s_DeviceAddressAlignment = 256u;
    outInputs = {};
    outInputs.input = CreateBuildInputBuffer(device, s_DeviceAddressAlignment + 1u, 1u);
    outInputs.triangleDescs = CreateBuildInputBuffer(
        device,
        s_DeviceAddressAlignment + sizeof(VkMicromapTriangleEXT),
        sizeof(VkMicromapTriangleEXT)
    );
    if(!outInputs.input || !outInputs.triangleDescs)
        return false;
    if(
        !ResolveAlignedBufferOffset(*outInputs.input, s_DeviceAddressAlignment, 1u, outInputs.inputOffset)
        || !ResolveAlignedBufferOffset(
            *outInputs.triangleDescs,
            s_DeviceAddressAlignment,
            sizeof(VkMicromapTriangleEXT),
            outInputs.triangleDescOffset
        )
    )
        return false;

    auto* inputData = static_cast<u8*>(device.mapBuffer(outInputs.input.get(), CpuAccessMode::Write));
    if(!inputData)
        return false;
    inputData[outInputs.inputOffset] = 0u;
    device.unmapBuffer(outInputs.input.get());

    auto* triangleData = static_cast<u8*>(device.mapBuffer(outInputs.triangleDescs.get(), CpuAccessMode::Write));
    if(!triangleData)
        return false;
    VkMicromapTriangleEXT triangleDesc = {};
    triangleDesc.dataOffset = 0u;
    triangleDesc.subdivisionLevel = 0u;
    triangleDesc.format = static_cast<u16>(VK_OPACITY_MICROMAP_FORMAT_2_STATE_EXT);
    NWB_MEMCPY(
        triangleData + outInputs.triangleDescOffset,
        outInputs.triangleDescs->getCreationDescription().byteSize - outInputs.triangleDescOffset,
        &triangleDesc,
        sizeof(triangleDesc)
    );
    device.unmapBuffer(outInputs.triangleDescs.get());
    return true;
}

[[nodiscard]] RayTracingOpacityMicromapHandle CreateOpacityMicromap(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena
){
    RayTracingOpacityMicromapDesc desc(arena);
    desc.counts.push_back(s_OmmUsageCount);
    return device.createOpacityMicromap(desc);
}

[[nodiscard]] bool RecordOpacityMicromapBuild(
    GraphicsBackend::Device& device,
    Alloc::GlobalArena& arena,
    GraphicsBackend::CommandList& commandList,
    RayTracingOpacityMicromap* const opacityMicromap
){
    OpacityMicromapBuildInputs inputs;
    if(!InitializeOpacityMicromapBuildInputs(device, inputs))
        return false;

    RayTracingOpacityMicromapDesc buildDesc(arena);
    buildDesc.counts.push_back(s_OmmUsageCount);
    buildDesc
        .setInputBuffer(inputs.input.get())
        .setInputBufferOffset(inputs.inputOffset)
        .setPerOmmDescs(inputs.triangleDescs.get())
        .setPerOmmDescsOffset(inputs.triangleDescOffset)
    ;
    commandList.buildOpacityMicromap(opacityMicromap, buildDesc);
    return !commandList.commandRecordingFailed();
}

[[nodiscard]] RayTracingGeometryDesc MakeOpacityMicromapGeometry(
    Buffer* const vertexBuffer,
    RayTracingOpacityMicromap* const opacityMicromap,
    Buffer* const ommIndexBuffer = nullptr,
    const u64 ommIndexBufferOffset = 0u,
    const Format::Enum ommIndexFormat = Format::UNKNOWN
){
    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertexBuffer)
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
        .setOpacityMicromap(opacityMicromap)
        .setOmmIndexBuffer(ommIndexBuffer)
        .setOmmIndexBufferOffset(ommIndexBufferOffset)
        .setOmmIndexFormat(ommIndexFormat)
        .setPOmmUsageCounts(&s_OmmUsageCount)
        .setNumOmmUsageCounts(1u)
    ;

    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
    return geometry;
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class RayTracingBuildIngressTest : public ::testing::Test{
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
                << "validation-enabled ray-tracing build ingress tests emitted a Vulkan error";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_runtimeReady = false;
    }

    virtual void SetUp()override{
        if(!s_runtimeReady)
            GTEST_SKIP() << "Ray-tracing build ingress: no usable headless graphics device.";
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_runtimeReady;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool RayTracingBuildIngressTest::s_runtimeReady = false;
UniquePtr<HeadlessGraphicsScope> RayTracingBuildIngressTest::s_scope;
Optional<CapturingLogger> RayTracingBuildIngressTest::s_logger;
Optional<Common::LoggerRegistrationGuard> RayTracingBuildIngressTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RayTracingBuildContractTest, OpacityMicromapWriteAfterWriteBarrierMatchesVulkanDependency){
    const VkMemoryBarrier2 barrier = GraphicsBackend::VulkanDetail::BuildOpacityMicromapWriteAfterWriteBarrier();
    EXPECT_EQ(barrier.srcStageMask, VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT);
    EXPECT_EQ(barrier.srcAccessMask, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT);
    EXPECT_EQ(barrier.dstStageMask, VK_PIPELINE_STAGE_2_MICROMAP_BUILD_BIT_EXT);
    EXPECT_EQ(barrier.dstAccessMask, VK_ACCESS_2_MICROMAP_WRITE_BIT_EXT);
}

TEST_F(RayTracingBuildIngressTest, AccelerationStructureCommandDependenciesAreRetainedRegardlessOfTrackingMode){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    for(u32 trackingMode = 0u; trackingMode < 2u; ++trackingMode){
        const bool trackLiveness = trackingMode != 0u;
        const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
            device(),
            3u * 3u * sizeof(f32),
            3u * sizeof(f32)
        );
        const BufferHandle index = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
            device(),
            3u * sizeof(u16),
            sizeof(u16)
        );
        ASSERT_TRUE(vertex);
        ASSERT_TRUE(index);

        RayTracingGeometryTriangles triangles;
        triangles
            .setVertexBuffer(vertex.get())
            .setVertexFormat(Format::RGB32_FLOAT)
            .setVertexStride(3u * sizeof(f32))
            .setVertexCount(3u)
            .setIndexBuffer(index.get())
            .setIndexFormat(Format::R16_UINT)
            .setIndexCount(3u)
        ;
        RayTracingGeometryDesc geometry;
        geometry.setTriangles(triangles);

        RayTracingAccelStructDesc blasDesc(arena());
        blasDesc.addBottomLevelGeometry(geometry).setTrackLiveness(trackLiveness);
        const RayTracingAccelStructHandle blas = device().createAccelStruct(blasDesc);
        ASSERT_TRUE(blas);

        const u32 vertexReferences = vertex->getReferenceCount();
        const u32 indexReferences = index->getReferenceCount();
        CommandListHandle blasBuild = device().createCommandList();
        ASSERT_TRUE(blasBuild);
        blasBuild->open();
        blasBuild->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
        ASSERT_FALSE(blasBuild->commandRecordingFailed());
        EXPECT_GT(vertex->getReferenceCount(), vertexReferences);
        EXPECT_GT(index->getReferenceCount(), indexReferences);
        blasBuild->close();
        blasBuild.reset();
        EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
        EXPECT_EQ(index->getReferenceCount(), indexReferences);

        RayTracingAccelStructDesc tlasDesc(arena());
        tlasDesc.setTopLevelMaxInstances(1u).setTrackLiveness(trackLiveness);
        const RayTracingAccelStructHandle tlas = device().createAccelStruct(tlasDesc);
        ASSERT_TRUE(tlas);
        RayTracingInstanceDesc instance;
        instance.setBLAS(blas.get()).setInstanceMask(0xffu);

        const u32 blasReferences = blas->getReferenceCount();
        CommandListHandle tlasBuild = device().createCommandList();
        ASSERT_TRUE(tlasBuild);
        tlasBuild->open();
        tlasBuild->buildTopLevelAccelStruct(tlas.get(), &instance, 1u, RayTracingAccelStructBuildFlags::None);
        ASSERT_FALSE(tlasBuild->commandRecordingFailed());
        EXPECT_GT(blas->getReferenceCount(), blasReferences);
        tlasBuild->close();
        tlasBuild.reset();
        EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    }
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapCommandDependenciesAreRetainedRegardlessOfTrackingMode){
    if(
        !device().queryFeatureSupport(Feature::RayTracingOpacityMicromap)
        || !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
    )
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    using namespace __hidden_ray_tracing_build_ingress_tests;
    for(u32 trackingMode = 0u; trackingMode < 2u; ++trackingMode){
        const bool trackLiveness = trackingMode != 0u;
        const RayTracingOpacityMicromapHandle opacityMicromap = CreateOpacityMicromap(device(), arena());
        OpacityMicromapBuildInputs inputs;
        ASSERT_TRUE(opacityMicromap);
        ASSERT_TRUE(InitializeOpacityMicromapBuildInputs(device(), inputs));

        RayTracingOpacityMicromapDesc buildDesc(arena());
        buildDesc.counts.push_back(s_OmmUsageCount);
        buildDesc
            .setInputBuffer(inputs.input.get())
            .setInputBufferOffset(inputs.inputOffset)
            .setPerOmmDescs(inputs.triangleDescs.get())
            .setPerOmmDescsOffset(inputs.triangleDescOffset)
            .setTrackLiveness(trackLiveness)
        ;

        const u32 inputReferences = inputs.input->getReferenceCount();
        const u32 triangleReferences = inputs.triangleDescs->getReferenceCount();
        CommandListHandle micromapBuild = device().createCommandList();
        ASSERT_TRUE(micromapBuild);
        micromapBuild->open();
        micromapBuild->buildOpacityMicromap(opacityMicromap.get(), buildDesc);
        ASSERT_FALSE(micromapBuild->commandRecordingFailed());
        EXPECT_GT(inputs.input->getReferenceCount(), inputReferences);
        EXPECT_GT(inputs.triangleDescs->getReferenceCount(), triangleReferences);
        micromapBuild->close();

        CommandList* const micromapBuilds[]{ micromapBuild.get() };
        const QueueSubmissionToken token = device().executeCommandLists(
            micromapBuilds,
            LengthOf(micromapBuilds),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(token.valid());
        ASSERT_TRUE(device().waitForIdle());
        micromapBuild.reset();
        EXPECT_EQ(inputs.input->getReferenceCount(), inputReferences);
        EXPECT_EQ(inputs.triangleDescs->getReferenceCount(), triangleReferences);

        const BufferHandle vertex = CreateBuildInputBuffer(device(), 3u * 3u * sizeof(f32), 3u * sizeof(f32));
        const BufferHandle ommIndex = CreateBuildInputBuffer(device(), sizeof(u16), sizeof(u16));
        ASSERT_TRUE(vertex);
        ASSERT_TRUE(ommIndex);
        const RayTracingGeometryDesc geometry = MakeOpacityMicromapGeometry(
            vertex.get(),
            opacityMicromap.get(),
            ommIndex.get(),
            0u,
            Format::R16_UINT
        );
        RayTracingAccelStructDesc blasDesc(arena());
        blasDesc.addBottomLevelGeometry(geometry).setTrackLiveness(trackLiveness);
        const RayTracingAccelStructHandle blas = device().createAccelStruct(blasDesc);
        ASSERT_TRUE(blas);

        const u32 vertexReferences = vertex->getReferenceCount();
        const u32 indexReferences = ommIndex->getReferenceCount();
        const u32 micromapReferences = opacityMicromap->getReferenceCount();
        CommandListHandle blasBuild = device().createCommandList();
        ASSERT_TRUE(blasBuild);
        blasBuild->open();
        blasBuild->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
        ASSERT_FALSE(blasBuild->commandRecordingFailed());
        EXPECT_GT(vertex->getReferenceCount(), vertexReferences);
        EXPECT_GT(ommIndex->getReferenceCount(), indexReferences);
        EXPECT_GT(opacityMicromap->getReferenceCount(), micromapReferences);
        blasBuild->close();
        blasBuild.reset();
        EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
        EXPECT_EQ(ommIndex->getReferenceCount(), indexReferences);
        EXPECT_EQ(opacityMicromap->getReferenceCount(), micromapReferences);
    }
}


TEST_F(RayTracingBuildIngressTest, AccelerationStructureQueueSharingIsValidatedBeforeFeatureGate){
    constexpr u8 s_UnknownQueueSharingBit = 1u << 7u;
    const ResourceQueueSharing::Mask invalidQueueSharing = static_cast<ResourceQueueSharing::Mask>(
        static_cast<u8>(ResourceQueueSharing::GraphicsAndTransfer) | s_UnknownQueueSharingBit
    );
    RayTracingAccelStructDesc desc(arena());
    desc.setQueueSharing(invalidQueueSharing);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        EXPECT_FALSE(device().createAccelStruct(desc));
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    EXPECT_FALSE(device().createAccelStruct(desc));
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("queue sharing")));
#endif
}


TEST_F(RayTracingBuildIngressTest, AccelerationStructureBuildFlagsAreValidatedBeforeAllocationAndRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    const auto conflictingFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        RayTracingAccelStructBuildFlags::PreferFastTrace | RayTracingAccelStructBuildFlags::PreferFastBuild
    );
    {
        RayTracingAccelStructDesc desc(arena());
        desc.setBuildFlags(conflictingFlags);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(device().createAccelStruct(desc));
        }, "");
#else
        CapturingLogger logger;
        Common::LoggerRegistrationGuard loggerGuard(logger);
        EXPECT_FALSE(device().createAccelStruct(desc));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("fast-trace and fast-build flags are mutually exclusive")));
#endif
    }
    {
        RayTracingAccelStructDesc desc(arena());
        desc.setBuildFlags(static_cast<RayTracingAccelStructBuildFlags::Mask>(0x02u));
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
        EXPECT_DEATH_IF_SUPPORTED({
            EXPECT_FALSE(device().createAccelStruct(desc));
        }, "");
#else
        CapturingLogger logger;
        Common::LoggerRegistrationGuard loggerGuard(logger);
        EXPECT_FALSE(device().createAccelStruct(desc));
        EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("acceleration-structure build flags contain unknown bits")));
#endif
    }

    RayTracingAccelStructDesc lowMemoryDesc(arena());
    lowMemoryDesc.setBuildFlags(RayTracingAccelStructBuildFlags::MinimizeMemory);
    RayTracingAccelStructHandle blas = device().createAccelStruct(lowMemoryDesc);
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(blas);
    ASSERT_TRUE(vertex);

    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertex.get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
    ;
    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    const u32 blasReferences = blas->getReferenceCount();
    const u32 vertexReferences = vertex->getReferenceCount();
#endif

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, conflictingFlags);
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, conflictingFlags);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("fast-trace and fast-build flags are mutually exclusive")));
    EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, StandardAccelerationStructureAddressAndStrideAlignmentRejectsBeforeRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    const RayTracingAccelStructHandle blas = device().createAccelStruct(RayTracingAccelStructDesc(arena()));
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(device(), 64u, 12u);
    const BufferHandle index = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(device(), 16u, sizeof(u16));
    const BufferHandle aabb = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(device(), 64u, 24u);
    ASSERT_TRUE(blas);
    ASSERT_TRUE(vertex);
    ASSERT_TRUE(index);
    ASSERT_TRUE(aabb);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
#endif

    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertex.get())
        .setVertexOffset(1u)
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(12u)
        .setVertexCount(3u)
    ;
    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("triangle vertex buffer device address is not 4-byte aligned")));
#endif

    triangles
        .setVertexOffset(0u)
        .setIndexBuffer(index.get())
        .setIndexOffset(1u)
        .setIndexFormat(Format::R16_UINT)
        .setIndexCount(3u)
    ;
    geometry.setTriangles(triangles);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("triangle index buffer device address is not 2-byte aligned")));
#endif

    RayTracingGeometryAABBs aabbs;
    aabbs.setBuffer(aabb.get()).setCount(1u).setStride(28u);
    geometry.setAABBs(aabbs);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("AABB stride is not a multiple of 8 bytes")));
#endif

    aabbs.setStride(24u).setOffset(4u);
    geometry.setAABBs(aabbs);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("AABB buffer device address is not 8-byte aligned")));
#endif

    RayTracingAccelStructDesc tlasDesc(arena());
    tlasDesc.setTopLevelMaxInstances(1u);
    const RayTracingAccelStructHandle tlas = device().createAccelStruct(tlasDesc);
    const BufferHandle instances = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        sizeof(VkAccelerationStructureInstanceKHR) + 1u,
        sizeof(VkAccelerationStructureInstanceKHR)
    );
    ASSERT_TRUE(tlas);
    ASSERT_TRUE(instances);
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildTopLevelAccelStructFromBuffer(
            tlas.get(),
            instances.get(),
            1u,
            1u,
            RayTracingAccelStructBuildFlags::None
        );
    }, "");
#else
    commandList->buildTopLevelAccelStructFromBuffer(
        tlas.get(),
        instances.get(),
        1u,
        1u,
        RayTracingAccelStructBuildFlags::None
    );
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("instance data device address must be 16-byte aligned")));

    EXPECT_EQ(blas->getReferenceCount(), 1u);
    EXPECT_EQ(vertex->getReferenceCount(), 1u);
    EXPECT_EQ(index->getReferenceCount(), 1u);
    EXPECT_EQ(aabb->getReferenceCount(), 1u);
    EXPECT_EQ(tlas->getReferenceCount(), 1u);
    EXPECT_EQ(instances->getReferenceCount(), 1u);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, CpuTlasEmptyInstancesRequireExplicitOptIn){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    const RayTracingAccelStructHandle blas = device().createAccelStruct(RayTracingAccelStructDesc(arena()));
    RayTracingAccelStructDesc tlasDesc(arena());
    tlasDesc.setTopLevelMaxInstances(2u);
    const RayTracingAccelStructHandle tlas = device().createAccelStruct(tlasDesc);
    ASSERT_TRUE(blas);
    ASSERT_TRUE(tlas);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
#endif

    RayTracingInstanceDesc nullInstance;
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildTopLevelAccelStruct(tlas.get(), &nullInstance, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildTopLevelAccelStruct(tlas.get(), &nullInstance, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has a null bottom-level acceleration structure")));
#endif

    RayTracingInstanceDesc zeroMaskInstance;
    zeroMaskInstance.setBLAS(blas.get());
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildTopLevelAccelStruct(tlas.get(), &zeroMaskInstance, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildTopLevelAccelStruct(tlas.get(), &zeroMaskInstance, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has a zero mask")));
#endif

#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    const u32 errorCountBeforeOptIn = logger.errorCount();
#endif
    const RayTracingInstanceDesc emptyInstances[]{ nullInstance, zeroMaskInstance };
    commandList->buildTopLevelAccelStruct(
        tlas.get(),
        emptyInstances,
        LengthOf(emptyInstances),
        RayTracingAccelStructBuildFlags::AllowEmptyInstances
    );
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    EXPECT_EQ(logger.errorCount(), errorCountBeforeOptIn);
#endif
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, AcceptedAccelerationStructureBuildSignatureRejectsMismatchedUpdates){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        6u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(vertex);

    RayTracingGeometryTriangles initialTriangles;
    initialTriangles
        .setVertexBuffer(vertex.get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
    ;
    RayTracingGeometryDesc initialGeometry;
    initialGeometry.setTriangles(initialTriangles);

    RayTracingGeometryTriangles maximumTriangles = initialTriangles;
    maximumTriangles.setVertexCount(6u);
    RayTracingGeometryDesc maximumGeometry;
    maximumGeometry.setTriangles(maximumTriangles);

    const auto initialBuildFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::PreferFastTrace
    );
    const auto matchingUpdateFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        initialBuildFlags | RayTracingAccelStructBuildFlags::PerformUpdate
    );
    const auto mismatchedUpdateFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::PerformUpdate
    );

    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(maximumGeometry).setBuildFlags(initialBuildFlags);
    const RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

    CommandListHandle initialBuild = device().createCommandList();
    ASSERT_TRUE(initialBuild);
    initialBuild->open();
    initialBuild->buildBottomLevelAccelStruct(blas.get(), &initialGeometry, 1u, initialBuildFlags);
    ASSERT_FALSE(initialBuild->commandRecordingFailed());
    initialBuild->close();

    CommandList* const initialBuilds[]{ initialBuild.get() };
    const QueueSubmissionToken acceptedToken = device().executeCommandLists(
        initialBuilds,
        LengthOf(initialBuilds),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(acceptedToken.valid());
    ASSERT_TRUE(device().waitForIdle());

    CommandListHandle update = device().createCommandList();
    ASSERT_TRUE(update);
    update->open();
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    const u32 blasReferences = blas->getReferenceCount();
    const u32 vertexReferences = vertex->getReferenceCount();
#endif

#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        update->buildBottomLevelAccelStruct(blas.get(), &initialGeometry, 1u, mismatchedUpdateFlags);
    }, "");
    EXPECT_DEATH_IF_SUPPORTED({
        update->buildBottomLevelAccelStruct(blas.get(), &maximumGeometry, 1u, matchingUpdateFlags);
    }, "");
#else
    update->buildBottomLevelAccelStruct(blas.get(), &initialGeometry, 1u, mismatchedUpdateFlags);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("update build signature does not match the prior build")));
    const u32 errorCountAfterFlagMismatch = logger.errorCount();

    update->buildBottomLevelAccelStruct(blas.get(), &maximumGeometry, 1u, matchingUpdateFlags);
    EXPECT_GT(logger.errorCount(), errorCountAfterFlagMismatch);
    EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
    EXPECT_FALSE(update->commandRecordingFailed());
#endif
    update->close();
}

TEST_F(RayTracingBuildIngressTest, InjectedNativeSubmissionFailureDoesNotPublishAccelerationStructureBuildState){
    if(!device().queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "AS ingress: VK_KHR_acceleration_structure is unavailable.";

    const GpuPhysicalQueueId graphicsQueue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device().getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver;
    ASSERT_TRUE(submissionObserver.valid());

    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(vertex);
    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertex.get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
    ;
    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(geometry).setBuildFlags(RayTracingAccelStructBuildFlags::AllowUpdate);
    const RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

    const auto updateFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::PerformUpdate
    );
    const auto initialBuildFlags = static_cast<RayTracingAccelStructBuildFlags::Mask>(
        RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::AllowEmptyInstances
    );
    CapturingLogger initialLogger;
    Common::LoggerRegistrationGuard initialLoggerGuard(initialLogger);
    CommandListHandle initialBuild = device().createCommandList();
    ASSERT_TRUE(initialBuild);
    initialBuild->open();
    initialBuild->buildBottomLevelAccelStruct(
        blas.get(),
        &geometry,
        1u,
        initialBuildFlags
    );
    initialBuild->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, updateFlags);
    EXPECT_FALSE(initialLogger.sawErrorContaining(NWB_TEXT("requires a previously accepted build")));
    ASSERT_FALSE(initialBuild->commandRecordingFailed());
    initialBuild->close();

    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    CommandList* const initialBuilds[]{ initialBuild.get() };
    const QueueSubmissionToken rejectedToken = device().executeCommandLists(
        initialBuilds,
        LengthOf(initialBuilds),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    const bool unexpectedlySubmitted = rejectedToken.valid();
    const bool idleAfterUnexpectedSubmission = !unexpectedlySubmitted || device().waitForIdle();
    ASSERT_FALSE(unexpectedlySubmitted);
    ASSERT_TRUE(idleAfterUnexpectedSubmission);
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);

    CommandListHandle rejectedUpdate = device().createCommandList();
    ASSERT_TRUE(rejectedUpdate);
    rejectedUpdate->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        rejectedUpdate->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, updateFlags);
    }, "");
#else
    rejectedUpdate->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, updateFlags);
    EXPECT_TRUE(initialLogger.sawErrorContaining(NWB_TEXT("requires a previously accepted build")));
    EXPECT_FALSE(rejectedUpdate->commandRecordingFailed());
#endif
    rejectedUpdate->close();
}

// The test-owned Vulkan trace proves exact scratch-address reuse while the accepted retry and later TLAS build prove
// that the resulting BLAS remains usable. Production exposes no scratch identity, counter, or diagnostic friend.
TEST_F(RayTracingBuildIngressTest, InjectedNativeSubmissionFailureReusesBuildScratchAtNativeBoundary){
    HeadlessGraphicsScope scratchScope;
    ASSERT_TRUE(scratchScope.initialize());

    GraphicsBackend::Device& scratchDevice = scratchScope.graphics().getDevice();
    if(!scratchDevice.queryFeatureSupport(Feature::RayTracingAccelStruct))
        GTEST_SKIP() << "Build scratch reuse: VK_KHR_acceleration_structure is unavailable.";

    const GpuPhysicalQueueId graphicsQueue = scratchDevice.getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        scratchDevice.getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver;
    ASSERT_TRUE(submissionObserver.valid());

    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        scratchDevice,
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(vertex);
    RayTracingGeometryTriangles triangles;
    triangles
        .setVertexBuffer(vertex.get())
        .setVertexFormat(Format::RGB32_FLOAT)
        .setVertexStride(3u * sizeof(f32))
        .setVertexCount(3u)
    ;
    RayTracingGeometryDesc geometry;
    geometry.setTriangles(triangles);
    RayTracingAccelStructDesc createDesc(scratchScope.arena());
    createDesc.addBottomLevelGeometry(geometry);
    const RayTracingAccelStructHandle blas = scratchDevice.createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

    {
        __hidden_ray_tracing_build_ingress_tests::NativeBuildScratchReuseCapture nativeCapture;
        __hidden_ray_tracing_build_ingress_tests::ScopedNativeBuildScratchReuseTrace nativeTrace(nativeCapture);
        ASSERT_TRUE(nativeTrace.valid());

        CommandListHandle rejectedBuild = scratchDevice.createCommandList();
        ASSERT_TRUE(rejectedBuild);
        rejectedBuild->open();
        rejectedBuild->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
        ASSERT_FALSE(rejectedBuild->commandRecordingFailed());
        rejectedBuild->close();
        ASSERT_EQ(nativeCapture.addressQueryCount, 1u);
        ASSERT_EQ(nativeCapture.buildCommandCount, 1u);
        ASSERT_NE(nativeCapture.addressQueries[0u].buffer, VK_NULL_HANDLE);
        ASSERT_NE(nativeCapture.addressQueries[0u].address, 0u);
        EXPECT_GE(nativeCapture.buildCommands[0u].scratchAddress, nativeCapture.addressQueries[0u].address);
        ASSERT_NE(nativeCapture.buildCommands[0u].destination, VK_NULL_HANDLE);

        ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
        CommandList* const rejectedBuilds[]{ rejectedBuild.get() };
        const QueueSubmissionToken rejectedToken = scratchDevice.executeCommandLists(
            rejectedBuilds,
            LengthOf(rejectedBuilds),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        const bool unexpectedlySubmitted = rejectedToken.valid();
        const bool idleAfterUnexpectedSubmission = !unexpectedlySubmitted || scratchDevice.waitForIdle();
        ASSERT_FALSE(unexpectedlySubmitted);
        ASSERT_TRUE(idleAfterUnexpectedSubmission);
        EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
        EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);

        CommandListHandle retryBuild = scratchDevice.createCommandList();
        ASSERT_TRUE(retryBuild);
        retryBuild->open();
        retryBuild->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
        ASSERT_FALSE(retryBuild->commandRecordingFailed());
        retryBuild->close();
        EXPECT_EQ(nativeCapture.addressQueryCount, 1u);
        ASSERT_EQ(nativeCapture.buildCommandCount, 2u);
        EXPECT_EQ(nativeCapture.buildCommands[1u].scratchAddress, nativeCapture.buildCommands[0u].scratchAddress);
        EXPECT_EQ(nativeCapture.buildCommands[1u].destination, nativeCapture.buildCommands[0u].destination);

        CommandList* const retryBuilds[]{ retryBuild.get() };
        const QueueSubmissionToken acceptedToken = scratchDevice.executeCommandLists(
            retryBuilds,
            LengthOf(retryBuilds),
            CommandQueue::Graphics,
            QueueSubmissionDesc{}
        );
        ASSERT_TRUE(acceptedToken.valid());
        ASSERT_TRUE(scratchDevice.waitForIdle());
    }

    RayTracingAccelStructDesc tlasDesc(scratchScope.arena());
    tlasDesc.setTopLevelMaxInstances(1u);
    const RayTracingAccelStructHandle tlas = scratchDevice.createAccelStruct(tlasDesc);
    ASSERT_TRUE(tlas);
    RayTracingInstanceDesc instance;
    instance.setBLAS(blas.get()).setInstanceMask(0xffu);

    CommandListHandle useBuild = scratchDevice.createCommandList();
    ASSERT_TRUE(useBuild);
    useBuild->open();
    useBuild->buildTopLevelAccelStruct(tlas.get(), &instance, 1u, RayTracingAccelStructBuildFlags::None);
    ASSERT_FALSE(useBuild->commandRecordingFailed());
    useBuild->close();
    CommandList* const useBuilds[]{ useBuild.get() };
    const QueueSubmissionToken useToken = scratchDevice.executeCommandLists(
        useBuilds,
        LengthOf(useBuilds),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(useToken.valid());
    ASSERT_TRUE(scratchDevice.waitForIdle());
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapSizingDoesNotDependOnHostRecordOrder){
    if(
        !device().queryFeatureSupport(Feature::RayTracingOpacityMicromap)
        || !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
    )
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(vertex);
    const RayTracingGeometryDesc geometry = __hidden_ray_tracing_build_ingress_tests::MakeOpacityMicromapGeometry(
        vertex.get(),
        opacityMicromap.get()
    );

    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(geometry);
    EXPECT_TRUE(device().createAccelStruct(createDesc));
    EXPECT_FALSE(logger.sawErrorContaining(NWB_TEXT("triangle opacity micromap is invalid")));
}

TEST_F(RayTracingBuildIngressTest, RepeatedOpacityMicromapBuildsAreWriteOrdered){
    if(!device().queryFeatureSupport(Feature::RayTracingOpacityMicromap))
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    ASSERT_TRUE(opacityMicromap);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(__hidden_ray_tracing_build_ingress_tests::RecordOpacityMicromapBuild(
        device(),
        arena(),
        *commandList,
        opacityMicromap.get()
    ));
    ASSERT_TRUE(__hidden_ray_tracing_build_ingress_tests::RecordOpacityMicromapBuild(
        device(),
        arena(),
        *commandList,
        opacityMicromap.get()
    ));
    commandList->close();

    CommandList* const commandLists[]{ commandList.get() };
    const QueueSubmissionToken token = device().executeCommandLists(
        commandLists,
        LengthOf(commandLists),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    ASSERT_TRUE(token.valid());
    EXPECT_TRUE(device().waitForIdle());
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapBlasUseRequiresConstructedState){
    if(
        !device().queryFeatureSupport(Feature::RayTracingOpacityMicromap)
        || !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
    )
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(vertex);
    const RayTracingGeometryDesc geometry = __hidden_ray_tracing_build_ingress_tests::MakeOpacityMicromapGeometry(
        vertex.get(),
        opacityMicromap.get()
    );
    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(geometry);
    const RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    const u32 opacityMicromapReferences = opacityMicromap->getReferenceCount();
    const u32 blasReferences = blas->getReferenceCount();
    const u32 vertexReferences = vertex->getReferenceCount();
#endif
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has not been constructed by an accepted or earlier same-command-buffer build")));
    EXPECT_EQ(opacityMicromap->getReferenceCount(), opacityMicromapReferences);
    EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapDeviceAddressAlignmentRejectsBeforeRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingOpacityMicromap))
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    using namespace __hidden_ray_tracing_build_ingress_tests;
    const RayTracingOpacityMicromapHandle opacityMicromap = CreateOpacityMicromap(device(), arena());
    OpacityMicromapBuildInputs inputs;
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(InitializeOpacityMicromapBuildInputs(device(), inputs));
#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    const u32 opacityMicromapReferences = opacityMicromap->getReferenceCount();
    const u32 inputReferences = inputs.input->getReferenceCount();
    const u32 triangleReferences = inputs.triangleDescs->getReferenceCount();
#endif

    RayTracingOpacityMicromapDesc buildDesc(arena());
    buildDesc.counts.push_back(s_OmmUsageCount);
    buildDesc
        .setInputBuffer(inputs.input.get())
        .setInputBufferOffset(inputs.inputOffset + 1u)
        .setPerOmmDescs(inputs.triangleDescs.get())
        .setPerOmmDescsOffset(inputs.triangleDescOffset)
    ;

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildOpacityMicromap(opacityMicromap.get(), buildDesc);
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    commandList->buildOpacityMicromap(opacityMicromap.get(), buildDesc);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("input data device address is not 256-byte aligned")));
    EXPECT_EQ(opacityMicromap->getReferenceCount(), opacityMicromapReferences);
    EXPECT_EQ(inputs.input->getReferenceCount(), inputReferences);
    EXPECT_EQ(inputs.triangleDescs->getReferenceCount(), triangleReferences);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, InjectedNativeSubmissionFailureDoesNotPublishOpacityMicromapConstructionState){
    if(
        !device().queryFeatureSupport(Feature::RayTracingOpacityMicromap)
        || !device().queryFeatureSupport(Feature::RayTracingAccelStruct)
    )
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const GpuPhysicalQueueId graphicsQueue = device().getPrimaryPhysicalQueue(CommandQueue::Graphics);
    ASSERT_TRUE(graphicsQueue.valid());
    const VkQueue nativeGraphicsQueue = static_cast<VkQueue>(
        device().getNativeQueue(GraphicsBackend::ObjectTypes::VK_Queue, graphicsQueue).pointer
    );
    ASSERT_NE(nativeGraphicsQueue, VK_NULL_HANDLE);
    VulkanTestQueueSubmit2Observer submissionObserver;
    ASSERT_TRUE(submissionObserver.valid());

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(vertex);
    const RayTracingGeometryDesc geometry = __hidden_ray_tracing_build_ingress_tests::MakeOpacityMicromapGeometry(
        vertex.get(),
        opacityMicromap.get()
    );
    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(geometry);
    const RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

    CommandListHandle rejectedBuild = device().createCommandList();
    ASSERT_TRUE(rejectedBuild);
    rejectedBuild->open();
    ASSERT_TRUE(__hidden_ray_tracing_build_ingress_tests::RecordOpacityMicromapBuild(
        device(),
        arena(),
        *rejectedBuild,
        opacityMicromap.get()
    ));
    rejectedBuild->close();

    ASSERT_TRUE(submissionObserver.armSubmissionFailures(nativeGraphicsQueue));
    CommandList* const rejectedBuilds[]{ rejectedBuild.get() };
    const QueueSubmissionToken rejectedToken = device().executeCommandLists(
        rejectedBuilds,
        LengthOf(rejectedBuilds),
        CommandQueue::Graphics,
        QueueSubmissionDesc{}
    );
    const bool unexpectedlySubmitted = rejectedToken.valid();
    const bool idleAfterUnexpectedSubmission = !unexpectedlySubmitted || device().waitForIdle();
    ASSERT_FALSE(unexpectedlySubmitted);
    ASSERT_TRUE(idleAfterUnexpectedSubmission);
    EXPECT_EQ(submissionObserver.injectedSubmissionFailureCount(), 1u);
    EXPECT_EQ(submissionObserver.pendingSubmissionFailureCount(), 0u);

    CommandListHandle rejectedUse = device().createCommandList();
    ASSERT_TRUE(rejectedUse);
    rejectedUse->open();
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        rejectedUse->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    rejectedUse->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("has not been constructed by an accepted or earlier same-command-buffer build")));
    EXPECT_FALSE(rejectedUse->commandRecordingFailed());
#endif
    rejectedUse->close();
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapIndexRangeRejectsBeforeBuildRetention){
    if(!device().queryFeatureSupport(Feature::RayTracingOpacityMicromap))
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    const BufferHandle ommIndex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        sizeof(u16),
        sizeof(u16)
    );
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(vertex);
    ASSERT_TRUE(ommIndex);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(__hidden_ray_tracing_build_ingress_tests::RecordOpacityMicromapBuild(
        device(),
        arena(),
        *commandList,
        opacityMicromap.get()
    ));

    const RayTracingGeometryDesc geometry = __hidden_ray_tracing_build_ingress_tests::MakeOpacityMicromapGeometry(
        vertex.get(),
        opacityMicromap.get(),
        ommIndex.get(),
        sizeof(u16),
        Format::R16_UINT
    );
    RayTracingAccelStructDesc createDesc(arena());
    createDesc.addBottomLevelGeometry(geometry);
    RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);

#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    const u32 blasReferences = blas->getReferenceCount();
    const u32 vertexReferences = vertex->getReferenceCount();
    const u32 micromapReferences = opacityMicromap->getReferenceCount();
    const u32 indexReferences = ommIndex->getReferenceCount();
#endif
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    commandList->buildBottomLevelAccelStruct(blas.get(), &geometry, 1u, RayTracingAccelStructBuildFlags::None);
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("triangle OMM index buffer range is outside the buffer")));
    EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
    EXPECT_EQ(opacityMicromap->getReferenceCount(), micromapReferences);
    EXPECT_EQ(ommIndex->getReferenceCount(), indexReferences);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}

TEST_F(RayTracingBuildIngressTest, OpacityMicromapRefitRejectsWithoutPersistedGeometrySnapshot){
    if(!device().queryFeatureSupport(Feature::RayTracingOpacityMicromap))
        GTEST_SKIP() << "OMM ingress: VK_EXT_opacity_micromap is unavailable.";

    const RayTracingOpacityMicromapHandle opacityMicromap =
        __hidden_ray_tracing_build_ingress_tests::CreateOpacityMicromap(device(), arena());
    const BufferHandle vertex = __hidden_ray_tracing_build_ingress_tests::CreateBuildInputBuffer(
        device(),
        3u * 3u * sizeof(f32),
        3u * sizeof(f32)
    );
    ASSERT_TRUE(opacityMicromap);
    ASSERT_TRUE(vertex);

    CommandListHandle commandList = device().createCommandList();
    ASSERT_TRUE(commandList);
    commandList->open();
    ASSERT_TRUE(__hidden_ray_tracing_build_ingress_tests::RecordOpacityMicromapBuild(
        device(),
        arena(),
        *commandList,
        opacityMicromap.get()
    ));

    const RayTracingGeometryDesc geometry = __hidden_ray_tracing_build_ingress_tests::MakeOpacityMicromapGeometry(
        vertex.get(),
        opacityMicromap.get()
    );
    RayTracingAccelStructDesc createDesc(arena());
    createDesc
        .addBottomLevelGeometry(geometry)
        .setBuildFlags(RayTracingAccelStructBuildFlags::AllowUpdate)
    ;
    RayTracingAccelStructHandle blas = device().createAccelStruct(createDesc);
    ASSERT_TRUE(blas);
    commandList->buildBottomLevelAccelStruct(
        blas.get(),
        &geometry,
        1u,
        RayTracingAccelStructBuildFlags::AllowUpdate
    );
    ASSERT_FALSE(commandList->commandRecordingFailed());

#if !defined(NWB_DEBUG) && !defined(NWB_OPTIMIZE)
    const u32 blasReferences = blas->getReferenceCount();
    const u32 vertexReferences = vertex->getReferenceCount();
    const u32 micromapReferences = opacityMicromap->getReferenceCount();
#endif
#if defined(NWB_DEBUG) || defined(NWB_OPTIMIZE)
    EXPECT_DEATH_IF_SUPPORTED({
        commandList->buildBottomLevelAccelStruct(
            blas.get(),
            &geometry,
            1u,
            static_cast<RayTracingAccelStructBuildFlags::Mask>(
                RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::PerformUpdate
            )
        );
    }, "");
#else
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    commandList->buildBottomLevelAccelStruct(
        blas.get(),
        &geometry,
        1u,
        static_cast<RayTracingAccelStructBuildFlags::Mask>(
            RayTracingAccelStructBuildFlags::AllowUpdate | RayTracingAccelStructBuildFlags::PerformUpdate
        )
    );
    EXPECT_TRUE(logger.sawErrorContaining(NWB_TEXT("opacity-micromap geometry updates are unsupported")));
    EXPECT_EQ(blas->getReferenceCount(), blasReferences);
    EXPECT_EQ(vertex->getReferenceCount(), vertexReferences);
    EXPECT_EQ(opacityMicromap->getReferenceCount(), micromapReferences);
    EXPECT_FALSE(commandList->commandRecordingFailed());
#endif
    commandList->close();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

