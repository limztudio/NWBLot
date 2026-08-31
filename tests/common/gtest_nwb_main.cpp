// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Shared GoogleTest entry point for the NWB test executables. Each test binary links this single object, which
// performs the engine's common initialization once (Core::Common::InitializerGuard) and then runs every gtest
// case registered in the linked test sources. GoogleTest's own gtest_main is intentionally not used — the
// engine needs its own application entry point (NWB_DEFINE_APPLICATION_ENTRY_POINT) for startup.


#include <cstdlib>
#include <gtest/gtest.h>

#include <core/common/application_entry.h>

#include <global/global.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_gtest_nwb_main{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool ConfigureWindowsArm64VulkanLayerPolicy(){
#if defined(NWB_PLATFORM_WINDOWS) && (defined(__aarch64__) || defined(_M_ARM64)) && !defined(_M_ARM64EC)
    char* existingValue = nullptr;
    usize existingValueSize = 0u;
    const errno_t readResult = ::_dupenv_s(&existingValue, &existingValueSize, "VK_DISABLE_VKON12_DRIVER_SORTING");
    if(readResult != 0){
        ::free(existingValue);
        return false;
    }
    if(existingValue){
        ::free(existingValue);
        return true;
    }

    // D3DMappingLayers 1.2506.2.0 omits vkGetDeviceProcAddr from its ARM64 driver-sorting layer. Overlapping Vulkan
    // instances then corrupt loader bookkeeping during device teardown. Use the layer manifest's official disable key.
    return ::_putenv_s("VK_DISABLE_VKON12_DRIVER_SORTING", "1") == 0;
#else
    return true;
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static int GoogleTestEntryPoint(const isize argc, tchar** argv, void*){
    if(!__hidden_gtest_nwb_main::ConfigureWindowsArm64VulkanLayerPolicy()){
        NWB_CERR << "test Vulkan layer policy initialization failed\n";
        return -1;
    }

    Core::Common::InitializerGuard commonInitializerGuard;
    if(!commonInitializerGuard.initialize()){
        NWB_CERR << "test common initialization failed\n";
        return -1;
    }

    int googleTestArgc = static_cast<int>(argc);
    ::testing::InitGoogleTest(&googleTestArgc, argv);
    // Common initialization starts worker threads before individual suites run.  Re-exec death tests so GoogleTest
    // never forks that live threaded runtime; v1.18 diagnoses the unsafe fast-style fork and can skip its death body.
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    return RUN_ALL_TESTS();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_DEFINE_APPLICATION_ENTRY_POINT(::NWB::Tests::GoogleTestEntryPoint)


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

