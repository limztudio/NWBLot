// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <gtest/gtest.h>

#include <global/global.h>
#include <core/graphics/vulkan/backend_context.h>
#include <impl/assets/graphics/bindless/runtime_abi.h>
#include <tests/common/capturing_logger.h>
#include <tests/common/headless_graphics_scope.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace Core;


namespace __hidden_native_mesh_policy_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline constexpr Name s_BackendArenaName("tests/smoke/native_mesh_policy/backend");


class BackendContextDestroyGuard final : NoCopy{
public:
    explicit BackendContextDestroyGuard(GraphicsBackend::BackendContext& context)
        : m_context(context)
    {}
    ~BackendContextDestroyGuard(){ m_context.destroy(); }


private:
    GraphicsBackend::BackendContext& m_context;
};


#if !defined(NWB_FINAL)
[[nodiscard]] TStringView ExpectedMeshDiagnostic(const bool requested, const bool effective){
    if(requested)
        return effective
            ? NWB_TEXT("meshShaderRequested=yes meshShader=yes")
            : NWB_TEXT("meshShaderRequested=yes meshShader=no");

    return NWB_TEXT("meshShaderRequested=no meshShader=no");
}


void ExpectNoValidationErrors(const CapturingLogger& logger){
    EXPECT_FALSE(logger.sawMessageContaining(NWB_TEXT("Vulkan debug: [severity=error")))
        << "native mesh policy smoke coverage emitted a Vulkan validation error";
}
#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(NativeMeshPolicy, SetterFreezesAtInstanceCreationAndUnfreezesAfterDestroy){
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope scope;

    ASSERT_TRUE(scope.graphics().setNativeMeshShadersEnabled(true));

    InstanceParameters parameters(scope.arena());
    parameters.headlessDevice = true;
    if(!scope.graphics().createInstance(parameters))
        GTEST_SKIP() << "Native mesh policy lifecycle: no usable Vulkan instance.";

    EXPECT_FALSE(scope.graphics().setNativeMeshShadersEnabled(false));
    scope.graphics().destroy();
    EXPECT_TRUE(scope.graphics().setNativeMeshShadersEnabled(false));
}

TEST(NativeMeshPolicy, WindowsArm64DefaultPublishesComputeFallback){
#if defined(_WIN32) && (defined(__aarch64__) || defined(_M_ARM64))
    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    HeadlessGraphicsScope scope;
    if(!scope.initialize())
        GTEST_SKIP() << "Native mesh policy default: no usable validation-backed headless device.";

    EXPECT_FALSE(scope.graphics().getDevice().queryFeatureSupport(Feature::Meshlets));
#if !defined(NWB_FINAL)
    EXPECT_TRUE(logger.sawMessageContaining(NWB_TEXT("meshShaderRequested=no meshShader=no")));
#endif
    scope.graphics().destroy();
#if !defined(NWB_FINAL)
    __hidden_native_mesh_policy_tests::ExpectNoValidationErrors(logger);
#endif
#else
    GTEST_SKIP() << "The conservative native mesh default is specific to Windows ARM64.";
#endif
}

TEST(NativeMeshPolicy, ExplicitChoicesPublishRequestedAndEffectiveState){
    {
        CapturingLogger logger;
        Common::LoggerRegistrationGuard loggerGuard(logger);
        HeadlessGraphicsScope scope;
        ASSERT_TRUE(scope.graphics().setNativeMeshShadersEnabled(false));
        if(!scope.initialize())
            GTEST_SKIP() << "Native mesh policy opt-out: no usable validation-backed headless device.";

        EXPECT_FALSE(scope.graphics().getDevice().queryFeatureSupport(Feature::Meshlets));
#if !defined(NWB_FINAL)
        EXPECT_TRUE(logger.sawMessageContaining(
            __hidden_native_mesh_policy_tests::ExpectedMeshDiagnostic(false, false)
        ));
#endif
        scope.graphics().destroy();
#if !defined(NWB_FINAL)
        __hidden_native_mesh_policy_tests::ExpectNoValidationErrors(logger);
#endif
    }

    {
        CapturingLogger logger;
        Common::LoggerRegistrationGuard loggerGuard(logger);
        HeadlessGraphicsScope scope;
        ASSERT_TRUE(scope.graphics().setNativeMeshShadersEnabled(true));
        if(!scope.initialize())
            GTEST_SKIP() << "Native mesh policy opt-in: no usable validation-backed headless device.";

        const bool effective = scope.graphics().getDevice().queryFeatureSupport(Feature::Meshlets);
        EXPECT_EQ(scope.graphics().queryFeatureSupport(Feature::Meshlets), effective);
#if !defined(NWB_FINAL)
        EXPECT_TRUE(logger.sawMessageContaining(
            __hidden_native_mesh_policy_tests::ExpectedMeshDiagnostic(true, effective)
        ));
#endif
        scope.graphics().destroy();
#if !defined(NWB_FINAL)
        __hidden_native_mesh_policy_tests::ExpectNoValidationErrors(logger);
#endif
    }
}

TEST(NativeMeshPolicy, RawOptionalExtensionCannotBypassDisabledPolicy){
#if !defined(NWB_FINAL)
    if(!HasKhronosValidationLayer())
        GTEST_SKIP() << "Native mesh raw extension policy: VK_LAYER_KHRONOS_validation is unavailable.";
#endif

    CapturingLogger logger;
    Common::LoggerRegistrationGuard loggerGuard(logger);
    {
        Alloc::GlobalArena arena(__hidden_native_mesh_policy_tests::s_BackendArenaName);
        GraphicsAllocator allocator(arena);
        Alloc::ThreadPool threadPool(2u, CpuAffinity::Any);
        DeviceCreationParameters parameters(arena);
        parameters.headlessDevice = true;
        parameters.enableGpuCrashDiagnostics = false;
        parameters.enableNativeMeshShaders = false;
#if !defined(NWB_FINAL)
        parameters.enableDebugRuntime = true;
#endif
        parameters.bindlessHeapAbi = Impl::AssetsGraphicsBindless::MakeGpuDescriptorHeapAbi();
        parameters.optionalBackendDeviceExtensions.emplace_back(VK_EXT_MESH_SHADER_EXTENSION_NAME, arena);

        SwapChainRuntimeState swapChainState;
        GraphicsBackend::BackendContext context(parameters, swapChainState, allocator, threadPool);
        __hidden_native_mesh_policy_tests::BackendContextDestroyGuard contextGuard(context);
        if(!context.createInstance())
            GTEST_SKIP() << "Native mesh raw extension policy: no usable Vulkan instance.";
        if(!context.createDevice())
            GTEST_SKIP() << "Native mesh raw extension policy: no usable headless Vulkan device.";
        if(!context.isDeviceExtensionEnabled(VK_EXT_MESH_SHADER_EXTENSION_NAME))
            GTEST_SKIP() << "Native mesh raw extension policy: VK_EXT_mesh_shader is unavailable.";

        ASSERT_NE(context.getDevice(), nullptr);
        EXPECT_FALSE(context.getDevice()->queryFeatureSupport(Feature::Meshlets));
#if !defined(NWB_FINAL)
        EXPECT_TRUE(logger.sawMessageContaining(
            __hidden_native_mesh_policy_tests::ExpectedMeshDiagnostic(false, false)
        ));
#endif
    }
#if !defined(NWB_FINAL)
    __hidden_native_mesh_policy_tests::ExpectNoValidationErrors(logger);
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

