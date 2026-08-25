// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Command-IR backend operand alias preflight coverage.


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


namespace __hidden_command_ir_backend_alias_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct SameBufferCopyGraph{
    GraphicsBackend::Device& m_device;
    BufferHandle m_buffer;
    GpuTaskGraph m_graph;
    GpuTaskGraphAnalysis m_analysis;
    GpuTaskGraphQueueAssignments m_assignments;
    GpuCompiledGraph m_compiledGraph;
    GpuGraphResourceId m_sourceResource;
    GpuGraphResourceId m_destinationResource;
    GpuTaskId m_task;
    GpuSubmissionPacketId m_packet;
    GpuPhysicalQueueId m_queue;

    SameBufferCopyGraph(GraphicsBackend::Device& device, GraphicsArena& arena)
        : m_device(device)
        , m_graph(arena)
        , m_analysis(arena)
        , m_assignments(arena)
        , m_compiledGraph(arena)
    {}

    [[nodiscard]] bool initialize(){
        m_buffer = m_device.createBuffer(
            BufferDesc()
                .setByteSize(64u)
                .setInitialState(ResourceStates::Common)
        );
        if(!m_buffer || !m_device.isBufferReadyForGpuUse(m_buffer.get()))
            return false;

        m_sourceResource = m_graph.importBuffer(
            m_buffer,
            GpuGraphResourceDesc{}
                .setIdentity(Name("tests/command_ir_backend_alias/source"))
                .setMarkerLabel("Command IR Same Buffer Source")
                .setType(GpuGraphResourceType::Buffer)
                .setInitialState(ResourceStates::Common)
        );
        m_destinationResource = m_sourceResource;
        if(
            !m_sourceResource.valid()
            || m_graph.bufferForResource(m_sourceResource) != m_buffer.get()
        )
            return false;

        GpuTaskResourceRange sourceRange;
        sourceRange.bufferRange = BufferRange(0u, 16u);
        GpuTaskResourceRange destinationRange;
        destinationRange.bufferRange = BufferRange(32u, 16u);
        const GpuTaskResourceUse uses[] = {
            {
                .resource = m_sourceResource,
                .range = sourceRange,
                .requiredState = ResourceStates::CopySource,
                .access = GpuTaskResourceAccess::Read,
            },
            {
                .resource = m_destinationResource,
                .range = destinationRange,
                .requiredState = ResourceStates::CopyDest,
                .access = GpuTaskResourceAccess::Write,
            },
        };
        const GpuQueueRequest queueRequest{
            GpuQueueCapability::Transfer, GpuQueuePreference::Graphics, false, false
        };
        GpuTaskDesc taskDesc;
        taskDesc
            .setIdentity(Name("tests/command_ir_backend_alias/copy"))
            .setMarkerLabel("Command IR Same Buffer Copy")
            .setQueue(queueRequest)
            .setResourceUses(uses, LengthOf(uses))
        ;
        m_task = m_graph.addTask(taskDesc);
        if(!m_task.valid())
            return false;

        Alloc::ScratchArena scratchArena(Name("tests/command_ir_backend_alias/compiler_scratch"));
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

        m_packet = m_compiledGraph.packetForTask(m_task);
        if(!m_packet.valid())
            return false;
        m_queue = m_compiledGraph.packet(m_packet).queue;
        return m_queue.valid();
    }

    [[nodiscard]] bool capture(GpuCommandIrCapture& capture)const{
        return capture.captureCopyBuffer(
            m_task,
            m_packet,
            m_queue,
            m_sourceResource,
            0u,
            m_destinationResource,
            32u,
            16u
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class CommandIrBackendAliasTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        if(!s_scope->initialize()){
            GTEST_SKIP() << "Command IR backend alias preflight: no usable validation-enabled Vulkan device.";
            return;
        }
        s_validationBackedDeviceInitialized = true;
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled command-IR backend alias smoke emitted a Vulkan severity=error message";
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

bool CommandIrBackendAliasTest::s_validationBackedDeviceInitialized = false;
UniquePtr<HeadlessGraphicsScope> CommandIrBackendAliasTest::s_scope;
Optional<CapturingLogger> CommandIrBackendAliasTest::s_logger;
Optional<Common::LoggerRegistrationGuard> CommandIrBackendAliasTest::s_loggerGuard;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(CommandIrBackendAliasTest, NonOverlappingSameBufferCopyAcceptsCombinedPermanentState){
    using namespace __hidden_command_ir_backend_alias_tests;

    SameBufferCopyGraph resources(CommandIrBackendAliasTest::device(), CommandIrBackendAliasTest::arena());
    ASSERT_TRUE(resources.initialize());
    GpuCommandIrCapture capture(CommandIrBackendAliasTest::arena());
    ASSERT_TRUE(resources.capture(capture));
    ASSERT_EQ(capture.recordCount(), 1u);
    constexpr ResourceStates::Mask s_CombinedState = ResourceStates::CopySource | ResourceStates::CopyDest;

    for(u32 directIndex = 0u; directIndex < 2u; ++directIndex){
        SCOPED_TRACE(directIndex);
        const bool directVulkan = directIndex != 0u;
        CommandListParameters parameters;
        parameters.setPhysicalQueue(resources.m_queue);
        CommandListHandle commandList = CommandIrBackendAliasTest::device().createCommandList(parameters);
        ASSERT_TRUE(commandList);
        commandList->open();
        ASSERT_TRUE(commandList->isRecording());
        commandList->setPermanentBufferState(resources.m_buffer.get(), s_CombinedState);
        if(directVulkan){
            commandList->setBufferState(resources.m_buffer.get(), s_CombinedState);
            commandList->commitBarriers();
        }
        ASSERT_FALSE(commandList->commandRecordingFailed());

        const GpuCommandIrReplayResult replay = directVulkan
            ? ReplayGpuCommandIrPacketDirectVulkan(
                capture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_packet,
                *commandList
            )
            : ReplayGpuCommandIrPacket(
                capture.commandBytes(),
                resources.m_graph,
                resources.m_compiledGraph,
                resources.m_packet,
                *commandList
            )
        ;
        EXPECT_TRUE(replay.valid());
        EXPECT_EQ(replay.recordIndex, 1u);
        EXPECT_TRUE(replay.streamValidation.valid());
        EXPECT_EQ(commandList->getPermanentBufferState(resources.m_buffer.get()), s_CombinedState);
        EXPECT_FALSE(commandList->commandRecordingFailed());
        commandList->close();
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

