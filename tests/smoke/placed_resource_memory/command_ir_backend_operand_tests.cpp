// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command-IR selected-packet backend operand preflight coverage.


#include <gtest/gtest.h>

#include <global/global.h>
#include <global/unique_ptr.h>
#include <core/graphics/capture/command_ir.h>
#include <core/graphics/task_graph/compiler.h>
#include <core/graphics/task_graph/task_graph.h>
#include <core/graphics/vulkan/backend.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_command_ir_backend_operand_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace BackendOperandOperation{
    enum Enum : u8{ CopyBuffer, CopyTexture, ClearBuffer, ClearTexture, ClearTextureRectUInt };
};

struct PermanentConflictCase{
    BackendOperandOperation::Enum operation = BackendOperandOperation::CopyBuffer;
    bool conflictSource = false;
    bool directVulkan = false;
};

struct BackendOperandGraph{
    GraphicsBackend::Device& m_device;
    BufferHandle m_readyBufferSource;
    BufferHandle m_readyBufferDestination;
    BufferHandle m_unboundBufferDestination;
    BufferHandle m_unboundPrefixSource;
    TextureHandle m_readyTextureSource;
    TextureHandle m_readyTextureDestination;
    TextureHandle m_unboundTextureDestination;
    GpuTaskGraph m_graph;
    GpuTaskGraphAnalysis m_analysis;
    GpuTaskGraphQueueAssignments m_assignments;
    GpuCompiledGraph m_compiledGraph;
    GpuGraphResourceId m_readyBufferSourceResource;
    GpuGraphResourceId m_readyBufferDestinationResource;
    GpuGraphResourceId m_unboundBufferDestinationResource;
    GpuGraphResourceId m_unboundPrefixSourceResource;
    GpuGraphResourceId m_readyTextureSourceResource;
    GpuGraphResourceId m_readyTextureDestinationResource;
    GpuGraphResourceId m_unboundTextureDestinationResource;
    GpuTaskId m_prefixTask;
    GpuTaskId m_targetTask;
    GpuSubmissionPacketId m_prefixPacket;
    GpuSubmissionPacketId m_targetPacket;
    GpuPhysicalQueueId m_queue;

    BackendOperandGraph(GraphicsBackend::Device& device, GraphicsArena& arena)
        : m_device(device)
        , m_graph(arena)
        , m_analysis(arena)
        , m_assignments(arena)
        , m_compiledGraph(arena)
    {}

    [[nodiscard]] bool initialize(){
        const BufferDesc readyBufferDesc = BufferDesc()
            .setByteSize(64u)
            .setInitialState(ResourceStates::Common)
        ;
        const BufferDesc virtualBufferDesc = BufferDesc()
            .setByteSize(64u)
            .setIsVirtual(true)
            .setInitialState(ResourceStates::Common)
        ;
        m_readyBufferSource = m_device.createBuffer(readyBufferDesc);
        m_readyBufferDestination = m_device.createBuffer(readyBufferDesc);
        m_unboundBufferDestination = m_device.createBuffer(virtualBufferDesc);
        m_unboundPrefixSource = m_device.createBuffer(virtualBufferDesc);

        const TextureDesc readyTextureDesc = TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setFormat(Format::RGBA8_UINT)
            .setInitialState(ResourceStates::Common)
        ;
        TextureDesc virtualTextureDesc = readyTextureDesc;
        virtualTextureDesc.isVirtual = true;
        m_readyTextureSource = m_device.createTexture(readyTextureDesc);
        m_readyTextureDestination = m_device.createTexture(readyTextureDesc);
        m_unboundTextureDestination = m_device.createTexture(virtualTextureDesc);
        if(
            !m_readyBufferSource
            || !m_readyBufferDestination
            || !m_unboundBufferDestination
            || !m_unboundPrefixSource
            || !m_readyTextureSource
            || !m_readyTextureDestination
            || !m_unboundTextureDestination
        )
            return false;
        if(
            !m_device.isBufferReadyForGpuUse(m_readyBufferSource.get())
            || !m_device.isBufferReadyForGpuUse(m_readyBufferDestination.get())
            || m_device.isBufferReadyForGpuUse(m_unboundBufferDestination.get())
            || m_device.isBufferReadyForGpuUse(m_unboundPrefixSource.get())
            || !m_device.isTextureReadyForGpuUse(m_readyTextureSource.get())
            || !m_device.isTextureReadyForGpuUse(m_readyTextureDestination.get())
            || m_device.isTextureReadyForGpuUse(m_unboundTextureDestination.get())
        )
            return false;

        m_readyBufferSourceResource = m_graph.importBuffer(
            m_readyBufferSource,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/ready_buffer_source"))
                .setMarkerLabel("Command IR Ready Buffer Source")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_readyBufferDestinationResource = m_graph.importBuffer(
            m_readyBufferDestination,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/ready_buffer_destination"))
                .setMarkerLabel("Command IR Ready Buffer Destination")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_unboundBufferDestinationResource = m_graph.importBuffer(
            m_unboundBufferDestination,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/unbound_buffer_destination"))
                .setMarkerLabel("Command IR Unbound Buffer Destination")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_unboundPrefixSourceResource = m_graph.importBuffer(
            m_unboundPrefixSource,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/unbound_prefix_source"))
                .setMarkerLabel("Command IR Unbound Prefix Source")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_readyTextureSourceResource = m_graph.importTexture(
            m_readyTextureSource,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/ready_texture_source"))
                .setMarkerLabel("Command IR Ready Texture Source")
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::Common)
        );
        m_readyTextureDestinationResource = m_graph.importTexture(
            m_readyTextureDestination,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/ready_texture_destination"))
                .setMarkerLabel("Command IR Ready Texture Destination")
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::Common)
        );
        m_unboundTextureDestinationResource = m_graph.importTexture(
            m_unboundTextureDestination,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend/unbound_texture_destination"))
                .setMarkerLabel("Command IR Unbound Texture Destination")
                .setType(GpuGraphResourceType::Texture)
                .setInitialState(ResourceStates::Common)
        );
        if(
            !m_readyBufferSourceResource.valid()
            || !m_readyBufferDestinationResource.valid()
            || !m_unboundBufferDestinationResource.valid()
            || !m_unboundPrefixSourceResource.valid()
            || !m_readyTextureSourceResource.valid()
            || !m_readyTextureDestinationResource.valid()
            || !m_unboundTextureDestinationResource.valid()
        )
            return false;

        const GpuTaskResourceUse prefixUses[] = {
            {
                .resource = m_unboundPrefixSourceResource, .range = {},
                .requiredState = ResourceStates::CopySource,
                .access = GpuTaskResourceAccess::Read,
            },
            {
                .resource = m_readyBufferDestinationResource, .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
        };
        const GpuQueueRequest queueRequest{
            static_cast<GpuQueueCapability::Mask>(GpuQueueCapability::Transfer | GpuQueueCapability::Compute),
            GpuQueuePreference::Graphics, false, false
        };
        GpuTaskDesc prefixDesc;
        prefixDesc
            .setIdentity(Name("tests/command_ir_backend/unselected_prefix"))
            .setMarkerLabel("Command IR Unselected Prefix")
            .setQueue(queueRequest)
            .setResourceUses(prefixUses, LengthOf(prefixUses))
        ;
        m_prefixTask = m_graph.addTask(prefixDesc);
        if(!m_prefixTask.valid())
            return false;

        const GpuTaskResourceUse targetUses[] = {
            {
                .resource = m_readyBufferSourceResource, .range = {},
                .requiredState = ResourceStates::CopySource,
                .access = GpuTaskResourceAccess::Read,
            },
            {
                .resource = m_readyBufferDestinationResource, .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
            {
                .resource = m_unboundBufferDestinationResource, .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
            {
                .resource = m_readyTextureSourceResource, .range = {},
                .requiredState = ResourceStates::CopySource,
                .access = GpuTaskResourceAccess::Read,
            },
            {
                .resource = m_readyTextureDestinationResource, .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
            {
                .resource = m_unboundTextureDestinationResource, .range = {},
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
        };
        GpuTaskDesc targetDesc;
        targetDesc
            .setIdentity(Name("tests/command_ir_backend/selected_target"))
            .setMarkerLabel("Command IR Selected Target")
            .setQueue(queueRequest)
            .setDependencies(&m_prefixTask, 1u)
            .setResourceUses(targetUses, LengthOf(targetUses))
        ;
        m_targetTask = m_graph.addTask(targetDesc);
        if(!m_targetTask.valid())
            return false;
        Alloc::ScratchArena scratchArena(Name("tests/command_ir_backend/compiler_scratch"));
        GpuTaskGraphCompileOptions compileOptions;
        compileOptions.allowMetadataOnlyTasks = true;
        const GpuTaskGraphCompiler compiler;
        if(!compiler.compile(
            m_graph,
            m_analysis,
            m_device.getPhysicalQueueTopology(),
            m_assignments,
            m_compiledGraph,
            scratchArena,
            compileOptions
        ))
            return false;

        m_prefixPacket = m_compiledGraph.packetForTask(m_prefixTask);
        m_targetPacket = m_compiledGraph.packetForTask(m_targetTask);
        if(!m_prefixPacket.valid() || !m_targetPacket.valid() || m_prefixPacket == m_targetPacket)
            return false;
        m_queue = m_compiledGraph.packet(m_targetPacket).queue;
        return m_compiledGraph.packet(m_prefixPacket).queue == m_queue;
    }

    [[nodiscard]] bool captureInvalidPrefix(GpuCommandIrCapture& capture)const{
        return capture.captureCopyBuffer(
            m_prefixTask,
            m_prefixPacket,
            m_queue,
            m_unboundPrefixSourceResource,
            0u,
            m_readyBufferDestinationResource,
            0u,
            16u
        );
    }

    [[nodiscard]] bool captureReadyBufferCopy(GpuCommandIrCapture& capture)const{
        return capture.captureCopyBuffer(
            m_targetTask,
            m_targetPacket,
            m_queue,
            m_readyBufferSourceResource,
            0u,
            m_readyBufferDestinationResource,
            0u,
            16u
        );
    }

    [[nodiscard]] bool captureOperation(
        GpuCommandIrCapture& capture,
        const BackendOperandOperation::Enum operation
    )const{
        switch(operation){
        case BackendOperandOperation::CopyBuffer:
            return captureReadyBufferCopy(capture);
        case BackendOperandOperation::CopyTexture:
            return capture.captureCopyTexture(
                m_targetTask,
                m_targetPacket,
                m_queue,
                m_readyTextureSourceResource,
                TextureSlice{},
                m_readyTextureDestinationResource,
                TextureSlice{}
            );
        case BackendOperandOperation::ClearBuffer:
            return capture.captureClearBuffer(
                m_targetTask,
                m_targetPacket,
                m_queue,
                m_readyBufferDestinationResource,
                0x5ac3f17du
            );
        case BackendOperandOperation::ClearTexture:{
            GpuClearTextureTaskDesc clearDesc;
            clearDesc.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
            clearDesc.valueType = GpuClearTextureTaskValueType::UInt;
            clearDesc.uintValue = UIntColor(1u, 2u, 3u, 4u);
            return capture.captureClearTexture(
                m_targetTask,
                m_targetPacket,
                m_queue,
                m_readyTextureDestinationResource,
                clearDesc
            );
        }
        case BackendOperandOperation::ClearTextureRectUInt:{
            GpuClearTextureRectUIntTaskDesc clearDesc;
            clearDesc.subresources = TextureSubresourceSet(0u, 1u, 0u, 1u);
            clearDesc.rect = Rect(4, 4);
            clearDesc.uintValue = UIntColor(5u, 6u, 7u, 8u);
            return capture.captureClearTextureRectUInt(
                m_targetTask,
                m_targetPacket,
                m_queue,
                m_readyTextureDestinationResource,
                clearDesc
            );
        }
        default:
            return false;
        }
    }
};

struct ReplayMutationSnapshot{
    static constexpr usize s_MaxCaptureBytes = 512u;

    u8 m_captureBytes[s_MaxCaptureBytes] = {};
    usize m_captureByteCount = 0u;
    usize m_captureRecordCount = 0u;
    u32 m_referenceCounts[7u] = {};
    ResourceStates::Mask m_bufferStates[2u] = {};
    ResourceStates::Mask m_textureStates[2u] = {};
    ResourceStates::Mask m_permanentBufferStates[2u] = {};
    ResourceStates::Mask m_permanentTextureStates[2u] = {};
    u64 m_recordingLease = 0u;
    bool m_isRecording = false;
    bool m_recordingFailed = false;
};


[[nodiscard]] static ReplayMutationSnapshot CaptureReplayMutationSnapshot(
    const BackendOperandGraph& resources,
    const GpuCommandIrCapture& capture,
    CommandList& commandList
){
    ReplayMutationSnapshot snapshot;
    const BinaryByteView bytes = capture.commandBytes();
    NWB_ASSERT(bytes.size() <= ReplayMutationSnapshot::s_MaxCaptureBytes);
    snapshot.m_captureByteCount = bytes.size();
    snapshot.m_captureRecordCount = capture.recordCount();
    NWB_MEMCPY(snapshot.m_captureBytes, sizeof(snapshot.m_captureBytes), bytes.data(), bytes.size());
    snapshot.m_referenceCounts[0u] = resources.m_readyBufferSource->getReferenceCount();
    snapshot.m_referenceCounts[1u] = resources.m_readyBufferDestination->getReferenceCount();
    snapshot.m_referenceCounts[2u] = resources.m_unboundBufferDestination->getReferenceCount();
    snapshot.m_referenceCounts[3u] = resources.m_unboundPrefixSource->getReferenceCount();
    snapshot.m_referenceCounts[4u] = resources.m_readyTextureSource->getReferenceCount();
    snapshot.m_referenceCounts[5u] = resources.m_readyTextureDestination->getReferenceCount();
    snapshot.m_referenceCounts[6u] = resources.m_unboundTextureDestination->getReferenceCount();
    snapshot.m_bufferStates[0u] = commandList.getBufferState(resources.m_readyBufferSource.get());
    snapshot.m_bufferStates[1u] = commandList.getBufferState(resources.m_readyBufferDestination.get());
    snapshot.m_textureStates[0u] = commandList.getTextureSubresourceState(resources.m_readyTextureSource.get(), 0u, 0u);
    snapshot.m_textureStates[1u] = commandList.getTextureSubresourceState(
        resources.m_readyTextureDestination.get(),
        0u,
        0u
    );
    snapshot.m_permanentBufferStates[0u] = commandList.getPermanentBufferState(resources.m_readyBufferSource.get());
    snapshot.m_permanentBufferStates[1u] = commandList.getPermanentBufferState(
        resources.m_readyBufferDestination.get()
    );
    snapshot.m_permanentTextureStates[0u] = commandList.getPermanentTextureState(resources.m_readyTextureSource.get());
    snapshot.m_permanentTextureStates[1u] = commandList.getPermanentTextureState(
        resources.m_readyTextureDestination.get()
    );
    snapshot.m_recordingLease = commandList.recordingLeaseSerial();
    snapshot.m_isRecording = commandList.isRecording();
    snapshot.m_recordingFailed = commandList.commandRecordingFailed();
    return snapshot;
}

static void ExpectCaptureUnchanged(
    const ReplayMutationSnapshot& before,
    const GpuCommandIrCapture& capture
){
    const BinaryByteView bytes = capture.commandBytes();
    EXPECT_EQ(capture.recordCount(), before.m_captureRecordCount);
    ASSERT_EQ(bytes.size(), before.m_captureByteCount);
    EXPECT_EQ(NWB_MEMCMP(bytes.data(), before.m_captureBytes, bytes.size()), 0);
}

static void ExpectReplayMutationSnapshotUnchanged(
    const ReplayMutationSnapshot& before,
    const BackendOperandGraph& resources,
    const GpuCommandIrCapture& capture,
    CommandList& commandList
){
    ExpectCaptureUnchanged(before, capture);
    const u32 referenceCounts[] = {
        resources.m_readyBufferSource->getReferenceCount(), resources.m_readyBufferDestination->getReferenceCount(),
        resources.m_unboundBufferDestination->getReferenceCount(), resources.m_unboundPrefixSource->getReferenceCount(),
        resources.m_readyTextureSource->getReferenceCount(), resources.m_readyTextureDestination->getReferenceCount(),
        resources.m_unboundTextureDestination->getReferenceCount(),
    };
    for(usize resourceIndex = 0u; resourceIndex < LengthOf(referenceCounts); ++resourceIndex)
        EXPECT_EQ(referenceCounts[resourceIndex], before.m_referenceCounts[resourceIndex]);
    EXPECT_EQ(commandList.getBufferState(resources.m_readyBufferSource.get()), before.m_bufferStates[0u]);
    EXPECT_EQ(commandList.getBufferState(resources.m_readyBufferDestination.get()), before.m_bufferStates[1u]);
    EXPECT_EQ(
        commandList.getTextureSubresourceState(resources.m_readyTextureSource.get(), 0u, 0u),
        before.m_textureStates[0u]
    );
    EXPECT_EQ(
        commandList.getTextureSubresourceState(resources.m_readyTextureDestination.get(), 0u, 0u),
        before.m_textureStates[1u]
    );
    EXPECT_EQ(
        commandList.getPermanentBufferState(resources.m_readyBufferSource.get()),
        before.m_permanentBufferStates[0u]
    );
    EXPECT_EQ(
        commandList.getPermanentBufferState(resources.m_readyBufferDestination.get()),
        before.m_permanentBufferStates[1u]
    );
    EXPECT_EQ(
        commandList.getPermanentTextureState(resources.m_readyTextureSource.get()),
        before.m_permanentTextureStates[0u]
    );
    EXPECT_EQ(
        commandList.getPermanentTextureState(resources.m_readyTextureDestination.get()),
        before.m_permanentTextureStates[1u]
    );
    EXPECT_EQ(commandList.recordingLeaseSerial(), before.m_recordingLease);
    EXPECT_EQ(commandList.isRecording(), before.m_isRecording);
    EXPECT_EQ(commandList.commandRecordingFailed(), before.m_recordingFailed);
}

static void SetPermanentConflict(
    const PermanentConflictCase& testCase,
    BackendOperandGraph& resources,
    CommandList& commandList
){
    switch(testCase.operation){
    case BackendOperandOperation::CopyBuffer:
        commandList.setPermanentBufferState(
            testCase.conflictSource
                ? resources.m_readyBufferSource.get()
                : resources.m_readyBufferDestination.get(),
            testCase.conflictSource ? ResourceStates::CopyDest : ResourceStates::CopySource
        );
        return;
    case BackendOperandOperation::CopyTexture:
        commandList.setPermanentTextureState(
            testCase.conflictSource
                ? resources.m_readyTextureSource.get()
                : resources.m_readyTextureDestination.get(),
            testCase.conflictSource ? ResourceStates::CopyDest : ResourceStates::CopySource
        );
        return;
    case BackendOperandOperation::ClearBuffer:
        commandList.setPermanentBufferState(resources.m_readyBufferDestination.get(), ResourceStates::CopySource);
        return;
    case BackendOperandOperation::ClearTexture:
    case BackendOperandOperation::ClearTextureRectUInt:
        commandList.setPermanentTextureState(resources.m_readyTextureDestination.get(), ResourceStates::CopySource);
        return;
    default:
        return;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandIrBackendOperandTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Command IR backend operand preflight: no usable validation-enabled Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled command-IR backend operand smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){ return s_scope->graphics().getDevice(); }
    [[nodiscard]] static Core::Alloc::GlobalArena& arena(){ return s_scope->arena(); }


protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};

bool CommandIrBackendOperandTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> CommandIrBackendOperandTest::s_scope;
Optional<CapturingLogger> CommandIrBackendOperandTest::s_logger;
Optional<Common::LoggerRegistrationGuard> CommandIrBackendOperandTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CommandIrBackendOperandTest, EveryOpcodeAndOperandRoleRejectsPermanentStateConflictAtomically){
    using namespace __hidden_command_ir_backend_operand_tests;

    BackendOperandGraph resources(CommandIrBackendOperandTest::device(), CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.initialize());
    static constexpr PermanentConflictCase s_Cases[] = {
        { BackendOperandOperation::CopyBuffer, true, false }, { BackendOperandOperation::CopyBuffer, false, false },
        { BackendOperandOperation::CopyBuffer, true, true }, { BackendOperandOperation::CopyBuffer, false, true },
        { BackendOperandOperation::CopyTexture, true, false }, { BackendOperandOperation::CopyTexture, false, false },
        { BackendOperandOperation::ClearBuffer, false, false }, { BackendOperandOperation::ClearTexture, false, false },
        { BackendOperandOperation::ClearTextureRectUInt, false, false },
    };

    for(usize caseIndex = 0u; caseIndex < LengthOf(s_Cases); ++caseIndex){
        SCOPED_TRACE(caseIndex);
        const PermanentConflictCase& testCase = s_Cases[caseIndex];
        GpuCommandIrCapture capture(CommandIrBackendOperandTest::arena());
        ASSERT_TRUE(resources.captureInvalidPrefix(capture));
        ASSERT_TRUE(resources.captureOperation(capture, testCase.operation));

        CommandListParameters parameters;
        parameters.setPhysicalQueue(resources.m_queue);
        CommandListHandle commandList = CommandIrBackendOperandTest::device().createCommandList(parameters);
        ASSERT_TRUE(commandList);
        commandList->open();
        ASSERT_TRUE(commandList->isRecording());
        SetPermanentConflict(testCase, resources, *commandList);
        ASSERT_FALSE(commandList->commandRecordingFailed());
        const ReplayMutationSnapshot before = CaptureReplayMutationSnapshot(resources, capture, *commandList);

        const GpuCommandIrReplayResult replay = testCase.directVulkan
            ? ReplayGpuCommandIrPacketDirectVulkan(
                capture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
            : ReplayGpuCommandIrPacket(
                capture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
        ;
        EXPECT_EQ(replay.error, GpuCommandIrReplayError::PermanentResourceStateMismatch);
        EXPECT_EQ(replay.recordIndex, 1u);
        EXPECT_TRUE(replay.streamValidation.valid());
        ExpectReplayMutationSnapshotUnchanged(before, resources, capture, *commandList);
        commandList->close();
        EXPECT_FALSE(commandList->commandRecordingFailed());
    }
}


TEST_F(CommandIrBackendOperandTest, LateUnboundBufferIsAtomicForNormalAndDirectReplayAndRetry){
    using namespace __hidden_command_ir_backend_operand_tests;

    BackendOperandGraph resources(CommandIrBackendOperandTest::device(), CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.initialize());
    GpuCommandIrCapture rejectedCapture(CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.captureInvalidPrefix(rejectedCapture));
    ASSERT_TRUE(resources.captureReadyBufferCopy(rejectedCapture));
    ASSERT_TRUE(rejectedCapture.captureCopyBuffer(
        resources.m_targetTask,
        resources.m_targetPacket,
        resources.m_queue,
        resources.m_readyBufferSourceResource,
        0u,
        resources.m_unboundBufferDestinationResource,
        0u,
        16u
    ));
    GpuCommandIrCapture retryCapture(CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.captureInvalidPrefix(retryCapture));
    ASSERT_TRUE(resources.captureReadyBufferCopy(retryCapture));

    for(u32 directIndex = 0u; directIndex < 2u; ++directIndex){
        SCOPED_TRACE(directIndex);
        const bool directVulkan = directIndex != 0u;
        CommandListParameters parameters;
        parameters.setPhysicalQueue(resources.m_queue);
        CommandListHandle commandList = CommandIrBackendOperandTest::device().createCommandList(parameters);
        ASSERT_TRUE(commandList);
        commandList->open();
        if(directVulkan){
            commandList->setBufferState(resources.m_readyBufferSource.get(), ResourceStates::CopySource);
            commandList->setBufferState(resources.m_readyBufferDestination.get(), ResourceStates::CopyDest);
            commandList->commitBarriers();
        }
        ASSERT_FALSE(commandList->commandRecordingFailed());
        const ReplayMutationSnapshot before = CaptureReplayMutationSnapshot(
            resources,
            rejectedCapture,
            *commandList
        );

        const GpuCommandIrReplayResult rejected = directVulkan
            ? ReplayGpuCommandIrPacketDirectVulkan(
                rejectedCapture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
            : ReplayGpuCommandIrPacket(
                rejectedCapture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
        ;
        EXPECT_EQ(rejected.error, GpuCommandIrReplayError::BackendResourceNotReady);
        EXPECT_EQ(rejected.recordIndex, 2u);
        EXPECT_TRUE(rejected.streamValidation.valid());
        ExpectReplayMutationSnapshotUnchanged(before, resources, rejectedCapture, *commandList);

        const ReplayMutationSnapshot retryBefore = CaptureReplayMutationSnapshot(resources, retryCapture, *commandList);
        const GpuCommandIrReplayResult retry = directVulkan
            ? ReplayGpuCommandIrPacketDirectVulkan(
                retryCapture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
            : ReplayGpuCommandIrPacket(
                retryCapture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_targetPacket,
                *commandList
            )
        ;
        EXPECT_TRUE(retry.valid());
        EXPECT_EQ(retry.recordIndex, 2u);
        EXPECT_TRUE(retry.streamValidation.valid());
        ExpectCaptureUnchanged(retryBefore, retryCapture);
        EXPECT_TRUE(commandList->matchesRecordingLease(before.m_recordingLease));
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
    }
}


TEST_F(CommandIrBackendOperandTest, LateUnboundTextureLeavesEarlierSelectedBufferCopyUnlowered){
    using namespace __hidden_command_ir_backend_operand_tests;

    BackendOperandGraph resources(CommandIrBackendOperandTest::device(), CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.initialize());
    GpuCommandIrCapture rejectedCapture(CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.captureInvalidPrefix(rejectedCapture));
    ASSERT_TRUE(resources.captureReadyBufferCopy(rejectedCapture));
    ASSERT_TRUE(rejectedCapture.captureCopyTexture(
        resources.m_targetTask,
        resources.m_targetPacket,
        resources.m_queue,
        resources.m_readyTextureSourceResource,
        TextureSlice{},
        resources.m_unboundTextureDestinationResource,
        TextureSlice{}
    ));
    GpuCommandIrCapture retryCapture(CommandIrBackendOperandTest::arena());
    ASSERT_TRUE(resources.captureInvalidPrefix(retryCapture));
    ASSERT_TRUE(resources.captureReadyBufferCopy(retryCapture));

    CommandListParameters parameters;
    parameters.setPhysicalQueue(resources.m_queue);
    CommandListHandle commandList = CommandIrBackendOperandTest::device().createCommandList(parameters);
    ASSERT_TRUE(commandList);
    commandList->open();
    const ReplayMutationSnapshot before = CaptureReplayMutationSnapshot(resources, rejectedCapture, *commandList);
    const GpuCommandIrReplayResult rejected = ReplayGpuCommandIrPacket(
        rejectedCapture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_targetPacket,
        *commandList
    );
    EXPECT_EQ(rejected.error, GpuCommandIrReplayError::BackendResourceNotReady);
    EXPECT_EQ(rejected.recordIndex, 2u);
    EXPECT_TRUE(rejected.streamValidation.valid());
    ExpectReplayMutationSnapshotUnchanged(before, resources, rejectedCapture, *commandList);

    const ReplayMutationSnapshot retryBefore = CaptureReplayMutationSnapshot(resources, retryCapture, *commandList);
    const GpuCommandIrReplayResult retry = ReplayGpuCommandIrPacket(
        retryCapture.commandBytes(),
        resources.m_graph,
        resources.m_compiledGraph,
        resources.m_targetPacket,
        *commandList
    );
    EXPECT_TRUE(retry.valid());
    EXPECT_EQ(retry.recordIndex, 2u);
    EXPECT_TRUE(retry.streamValidation.valid());
    ExpectCaptureUnchanged(retryBefore, retryCapture);
    EXPECT_TRUE(commandList->matchesRecordingLease(before.m_recordingLease));
    EXPECT_FALSE(commandList->commandRecordingFailed());
    commandList->close();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

