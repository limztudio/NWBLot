// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/common/frame_data.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_frame_data_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(FrameData, CopiesIndependentWindowState){
#if defined(NWB_PLATFORM_WINDOWS)
    NWB::Core::Common::WinFrame source;
    source.width() = 1234u;
    source.height() = 567u;
    source.setActive(true);
    const auto instance = reinterpret_cast<HINSTANCE>(static_cast<usize>(0x1234u));
    const auto window = reinterpret_cast<HWND>(static_cast<usize>(0x5678u));
    source.setInstance(instance);
    source.setHwnd(window);
    source.setWindowClass(42u);
    NWB::Core::Common::WinFrame copy;
    copy.frameParam() = source.frameParam();
    copy.setActive(false);
    EXPECT_EQ(copy.width(), 1234u);
    EXPECT_EQ(copy.height(), 567u);
    EXPECT_EQ(copy.instance(), instance);
    EXPECT_EQ(copy.hwnd(), window);
    EXPECT_EQ(copy.windowClass(), 42u);
    EXPECT_TRUE(source.isActive());
    EXPECT_FALSE(copy.isActive());
#elif defined(NWB_PLATFORM_LINUX)
    NWB::Core::Common::LinuxFrame source;
    source.width() = 1234u;
    source.height() = 567u;
    source.setActive(true);
    source.setBackend(NWB::Core::Common::LinuxFrameBackend::Wayland);
    auto* const display = reinterpret_cast<void*>(static_cast<usize>(0x1234u));
    auto* const state = reinterpret_cast<void*>(static_cast<usize>(0x5678u));
    source.nativeDisplay() = display;
    source.nativeWindowHandle() = 9876u;
    source.nativeState() = state;
    source.nativeAuxValue() = 42u;
    NWB::Core::Common::LinuxFrame copy;
    copy.frameParam() = source.frameParam();
    copy.setActive(false);
    EXPECT_EQ(copy.width(), 1234u);
    EXPECT_EQ(copy.height(), 567u);
    EXPECT_EQ(copy.backend(), NWB::Core::Common::LinuxFrameBackend::Wayland);
    EXPECT_EQ(copy.nativeDisplay(), display);
    EXPECT_EQ(copy.nativeWindowHandle(), 9876u);
    EXPECT_EQ(copy.nativeState(), state);
    EXPECT_EQ(copy.nativeAuxValue(), 42u);
    EXPECT_TRUE(source.isActive());
    EXPECT_FALSE(copy.isActive());
#endif
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

