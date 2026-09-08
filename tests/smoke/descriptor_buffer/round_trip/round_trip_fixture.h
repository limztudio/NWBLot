// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


// Shared headless-device fixtures for descriptor-buffer smoke domains.
// Both fixtures retain one suite-wide runtime; storage is defined in round_trip_fixture.cpp.

#include <gtest/gtest.h>
#include <global/global.h>
#include <global/process.h>
#include <global/unique_ptr.h>
#include <global/thread.h>
#include <core/common/module.h>
#include <core/alloc/general.h>
#include <core/task/cpu/scheduler.h>
#include <core/task/gpu/scheduler.h>
#include <core/frame/module.h>
#include <core/graphics/runtime/runtime.h>
#include <core/graphics/api.h>
#include <core/task/gpu/capture/command_ir.h>
#include <core/task/gpu/compiler.h>
#include <core/task/gpu/packet_runtime.h>
#include <core/task/gpu/persistent_state.h>
#include <core/perf/timing.h>
#include <impl/assets/graphics/avboit/constants.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <impl/assets/graphics/skinned_mesh/constants.h>
#include <impl/ecs_ui/texture_submission.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>
#include <tests/common/gpu_task_graph_read_views.h>
#include <tests/common/vulkan_test_sync.h>
#include <core/graphics/vulkan/backend.h>
#include <volk/volk.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;
namespace HostSync = Core::GraphicsBackend::VulkanDetail;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Fixture: one headless device shared across the suite. The renderer's global heap stays live so ordinary round-trip
// tests exercise production descriptor-buffer ownership and binding paths.
class DescriptorBufferRoundTripTest : public ::testing::Test{
protected:
    static void SetUpTestSuite(){
        // Worker failures are terminal in every build mode. Re-exec death tests so each child owns its Vulkan
        // device and worker pools, including when diagnostic invariant checks are compiled out.
        GTEST_FLAG_SET(death_test_style, "threadsafe");

        // The device-creation path emits log messages, and every NWB_LOGGER_* macro fatally asserts a logger is
        // installed. Register the capturing logger before bring-up so failures are recorded rather than
        // crashing the process, then keep it registered for the suite's lifetime.
        s_logger.emplace();
        s_loggerGuard.emplace(*s_logger);

        s_scope = MakeUnique<HeadlessGraphicsScope>();
        const bool initialized = s_scope->initialize();

        // No usable validation-enabled Vulkan device on this host -> skip the whole suite. Reported as SKIPPED,
        // not failed.
        if(!initialized){
            GTEST_SKIP() << "Descriptor-buffer round-trip: no usable validation-enabled headless Vulkan device on this host; skipping suite.";
            return;
        }
        s_validationBackedDeviceInitialized = true;

        auto& device = s_scope->graphics().getDevice();
        auto& mgr = device.getDescriptorBufferManager();

        if(!mgr.isEnabled()){
            // This suite proves the required descriptor-buffer path. A host without the extension cannot exercise
            // that contract, so skip instead of treating an ordinary descriptor-set path as equivalent coverage.
            GTEST_SKIP() << "Descriptor-buffer round-trip: VK_EXT_descriptor_buffer is not enabled on this device; "
                            "skipping descriptor-buffer-only coverage.";
        }
    }

    static void TearDownTestSuite(){
        s_scope.reset();
        // Keep unavailable GPU/layer configurations as intentional skips, but a successfully initialized
        // validation-backed runtime must not emit an error while tests or device teardown run.
        if(s_validationBackedDeviceInitialized && s_logger.has_value()){
            EXPECT_FALSE(s_logger->sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
                << "validation-enabled descriptor-buffer smoke emitted a Vulkan severity=error message";
        }
        s_loggerGuard.reset();
        s_logger.reset();
        s_validationBackedDeviceInitialized = false;
    }

    [[nodiscard]] static GraphicsBackend::Device& device(){
        return s_scope->graphics().getDevice();
    }
    [[nodiscard]] static GraphicsBackend::DescriptorBufferManager& manager(){
        return device().getDescriptorBufferManager();
    }
    [[nodiscard]] static Alloc::GlobalArena& arena(){ return s_scope->arena(); }

protected:
    static bool s_validationBackedDeviceInitialized;
    static UniquePtr<HeadlessGraphicsScope> s_scope;
    static Optional<CapturingLogger> s_logger;
    static Optional<Common::LoggerRegistrationGuard> s_loggerGuard;
};


// Direct manager and alternate-heap clients need exclusive ownership of device-bounded ranges. A separate fixture
// lifetime keeps the renderer heap live for ordinary round-trip and binding coverage.
class DescriptorBufferAllocationTest : public DescriptorBufferRoundTripTest{
protected:
    static void SetUpTestSuite(){
        DescriptorBufferRoundTripTest::SetUpTestSuite();
        if(!s_validationBackedDeviceInitialized)
            return;
        if(!manager().isEnabled())
            return;

        auto& heap = device().getDescriptorHeap();
        heap.shutdown();
        ASSERT_FALSE(heap.isInitialized());
        ASSERT_TRUE(manager().isEnabled());
    }

    static void TearDownTestSuite(){ DescriptorBufferRoundTripTest::TearDownTestSuite(); }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

