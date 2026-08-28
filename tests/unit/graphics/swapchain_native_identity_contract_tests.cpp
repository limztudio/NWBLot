// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_swapchain_native_identity_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct SwapchainNativeIdentityContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<SwapchainNativeIdentityContractTestArenaTag>;


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


// Swapchain teardown must invalidate retained wrappers before the driver can destroy their images, while reserving
// each native identity until destruction is complete. This source contract covers the WSI-only interval that a
// headless public fixture cannot enter without exposing a production mutation seam.
TEST(SwapChainPresentation, NativeTextureRetirementBracketsNativeDestructionAndClearsViews){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString surfaceSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_surface.cpp",
        surfaceSource
    ));
    const AStringView fullSurfaceSource(surfaceSource.data(), surfaceSource.size());
    const usize destroyFunctionBegin = fullSurfaceSource.find("void BackendContext::destroySwapChain(){");
    const usize destroyFunctionEnd = fullSurfaceSource.find(
        "bool BackendContext::createVulkanSwapChain(){",
        destroyFunctionBegin
    );
    ASSERT_NE(destroyFunctionBegin, AStringView::npos);
    ASSERT_NE(destroyFunctionEnd, AStringView::npos);
    ASSERT_LT(destroyFunctionBegin, destroyFunctionEnd);
    const AStringView destroyFunction = fullSurfaceSource.substr(
        destroyFunctionBegin,
        destroyFunctionEnd - destroyFunctionBegin
    );
    const usize revokeOffset = destroyFunction.find(
        "swapChainImage.rhiHandle->revokeUnmanagedNativeImage(swapChainImage.image)"
    );
    const usize nativeDestroyOffset = destroyFunction.find(
        "vkDestroySwapchainKHR(m_vulkanDevice, m_swapChain, nullptr)"
    );
    const usize releaseOffset = destroyFunction.find(
        "swapChainImage.rhiHandle->releaseRevokedNativeImageIdentity(swapChainImage.image)"
    );
    const usize wrapperClearOffset = destroyFunction.find("m_swapChainImages.clear();");
    ASSERT_NE(revokeOffset, AStringView::npos);
    ASSERT_NE(nativeDestroyOffset, AStringView::npos);
    ASSERT_NE(releaseOffset, AStringView::npos);
    ASSERT_NE(wrapperClearOffset, AStringView::npos);
    EXPECT_LT(revokeOffset, nativeDestroyOffset);
    EXPECT_LT(nativeDestroyOffset, releaseOffset);
    EXPECT_LT(releaseOffset, wrapperClearOffset);

    AString textureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "texture.cpp", textureSource));
    const AStringView fullTextureSource(textureSource.data(), textureSource.size());
    const usize revokeFunctionBegin = fullTextureSource.find("bool Texture::revokeUnmanagedNativeImage(");
    const usize releaseFunctionBegin = fullTextureSource.find(
        "void Texture::releaseRevokedNativeImageIdentity(",
        revokeFunctionBegin
    );
    const usize releaseFunctionEnd = fullTextureSource.find(
        "void Texture::initializeRetainedSubresourceStates(",
        releaseFunctionBegin
    );
    ASSERT_NE(revokeFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseFunctionBegin, AStringView::npos);
    ASSERT_NE(releaseFunctionEnd, AStringView::npos);
    ASSERT_LT(revokeFunctionBegin, releaseFunctionBegin);
    ASSERT_LT(releaseFunctionBegin, releaseFunctionEnd);

    const AStringView revokeFunction = fullTextureSource.substr(
        revokeFunctionBegin,
        releaseFunctionBegin - revokeFunctionBegin
    );
    const usize viewDestroyOffset = revokeFunction.find("vkDestroyImageView(");
    const usize viewClearOffset = revokeFunction.find("m_views.clear();");
    const usize imageClearOffset = revokeFunction.find("m_image = VK_NULL_HANDLE;");
    ASSERT_NE(viewDestroyOffset, AStringView::npos);
    ASSERT_NE(viewClearOffset, AStringView::npos);
    ASSERT_NE(imageClearOffset, AStringView::npos);
    EXPECT_LT(viewDestroyOffset, viewClearOffset);
    EXPECT_LT(viewClearOffset, imageClearOffset);
    EXPECT_EQ(revokeFunction.find("unregisterTextureNativeIdentity"), AStringView::npos);

    const AStringView releaseFunction = fullTextureSource.substr(
        releaseFunctionBegin,
        releaseFunctionEnd - releaseFunctionBegin
    );
    const usize revokedStateCheckOffset = releaseFunction.find("if(m_managed || m_image != VK_NULL_HANDLE)");
    const usize identityReleaseOffset = releaseFunction.find(
        "m_allocator.unregisterTextureNativeIdentity(expectedNativeImage, *this);"
    );
    ASSERT_NE(revokedStateCheckOffset, AStringView::npos);
    ASSERT_NE(identityReleaseOffset, AStringView::npos);
    EXPECT_LT(revokedStateCheckOffset, identityReleaseOffset);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

