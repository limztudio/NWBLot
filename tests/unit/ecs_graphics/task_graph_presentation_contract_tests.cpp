// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_presentation_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// One typed CPU snapshot binds an acquired WSI image to its exact owning framebuffer. Graphics publishes it only
// after attachment identity validation, clears it on every lifecycle boundary, and never asks mutable backend
// current-image state which framebuffer should render.
TEST(EcsGraphics, PresentationAcquisitionPublishesOneValidatedSnapshot){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString backendContractSource;
    AString backendOrchestrationSource;
    AString backendPresentationSource;
    AString rendererResourcesSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.cpp", graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "backend_contract.h", backendContractSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend_context_orchestration.cpp", backendOrchestrationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp", backendPresentationSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rendererResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView backendContract(backendContractSource.data(), backendContractSource.size());
    const AStringView backendOrchestration(backendOrchestrationSource.data(), backendOrchestrationSource.size());
    const AStringView backendPresentation(backendPresentationSource.data(), backendPresentationSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "const AcquiredPresentationFrame& acquiredPresentationFrame()const noexcept"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "AcquiredPresentationFrame m_acquiredPresentationFrame;"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBuffer"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentBackBufferIndex"));
    EXPECT_FALSE(ContainsText(graphicsHeader, "getCurrentFramebuffer"));
    EXPECT_TRUE(ContainsText(backendContract, "{ backend.abandonAcquiredFrame() }->SameAs<bool>;"));

    const usize abandonmentOffset = backendPresentation.find("bool BackendContext::abandonAcquiredFrame(){");
    const usize presentDefinitionOffset = backendPresentation.find("bool BackendContext::present(){", abandonmentOffset);
    ASSERT_NE(abandonmentOffset, AStringView::npos);
    ASSERT_NE(presentDefinitionOffset, AStringView::npos);
    const AStringView abandonment = backendPresentation.substr(
        abandonmentOffset,
        presentDefinitionOffset - abandonmentOffset
    );
    const usize imageQuarantineOffset = abandonment.find("m_swapChainIndex = Limit<u32>::s_Max;");
    const usize presentationUnlockOffset = abandonment.find("presentationLock.unlock();", imageQuarantineOffset);
    const usize deferredCancellationOffset = abandonment.find(
        "if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){",
        presentationUnlockOffset
    );
    const usize lifecycleQuarantineOffset = abandonment.find(
        "m_swapChainLifecycleState = SwapChainLifecycleState::NeedsDestroy;",
        deferredCancellationOffset
    );
    const usize deviceQuarantineOffset = abandonment.find("device->quarantineDevice();", lifecycleQuarantineOffset);
    const usize completionOffset = abandonment.find("m_frameAbandonmentComplete = true;", deviceQuarantineOffset);
    ASSERT_NE(imageQuarantineOffset, AStringView::npos);
    ASSERT_NE(presentationUnlockOffset, AStringView::npos);
    ASSERT_NE(deferredCancellationOffset, AStringView::npos);
    ASSERT_NE(lifecycleQuarantineOffset, AStringView::npos);
    ASSERT_NE(deviceQuarantineOffset, AStringView::npos);
    ASSERT_NE(completionOffset, AStringView::npos);
    EXPECT_LT(imageQuarantineOffset, presentationUnlockOffset);
    EXPECT_LT(presentationUnlockOffset, deferredCancellationOffset);
    EXPECT_LT(deferredCancellationOffset, lifecycleQuarantineOffset);
    EXPECT_LT(lifecycleQuarantineOffset, deviceQuarantineOffset);
    EXPECT_LT(deviceQuarantineOffset, completionOffset);
    EXPECT_FALSE(ContainsText(abandonment, "forceNativeSubmission"));
    EXPECT_FALSE(ContainsText(abandonment, "executeCommandLists(nullptr, 0u"));
    EXPECT_FALSE(ContainsText(abandonment, "waitForIdle()"));
    EXPECT_FALSE(ContainsText(abandonment, "replaceFramePresentationSemaphoreAfterIdle()"));
    EXPECT_FALSE(ContainsText(abandonment, "resetFramePresentationSignal();"));
    EXPECT_TRUE(ContainsText(abandonment, "if(!m_frameAcquired)\n        return true;"));
    EXPECT_TRUE(ContainsText(abandonment, "if(m_frameAbandonmentComplete){"));
    EXPECT_TRUE(ContainsText(abandonment, "cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)"));
    EXPECT_TRUE(ContainsText(abandonment, "device->captureDeviceLoss(\"abandoned presentation signal idle\")"));
    EXPECT_TRUE(ContainsText(abandonment, "m_frameAbandonmentComplete = true;"));
    EXPECT_FALSE(ContainsText(abandonment, "m_frameAcquired = false"));

    const AStringView present = backendPresentation.substr(presentDefinitionOffset);
    const usize nonConsumedOffset = present.find(
        "presentWaitDisposition != VulkanDetail::QueuePresentWaitDisposition::Consumed"
    );
    const usize deviceLostOffset = present.find(
        "presentWaitDisposition == VulkanDetail::QueuePresentWaitDisposition::DeviceLost",
        nonConsumedOffset
    );
    const usize unconsumedUnlockOffset = present.find("presentationLock.unlock();", deviceLostOffset);
    const usize unconsumedCancellationOffset = present.find(
        "if(!cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)){",
        unconsumedUnlockOffset
    );
    const usize unconsumedFailureOffset = present.find(
        "captureDeviceLossAfterUnlock(\"unconsumed presentation signal idle\", &presentationLock);",
        unconsumedCancellationOffset
    );
    ASSERT_NE(nonConsumedOffset, AStringView::npos);
    ASSERT_NE(deviceLostOffset, AStringView::npos);
    ASSERT_NE(unconsumedUnlockOffset, AStringView::npos);
    ASSERT_NE(unconsumedCancellationOffset, AStringView::npos);
    ASSERT_NE(unconsumedFailureOffset, AStringView::npos);
    EXPECT_LT(nonConsumedOffset, deviceLostOffset);
    EXPECT_LT(deviceLostOffset, unconsumedUnlockOffset);
    EXPECT_LT(unconsumedUnlockOffset, unconsumedCancellationOffset);
    EXPECT_LT(unconsumedCancellationOffset, unconsumedFailureOffset);
    EXPECT_TRUE(ContainsText(present, "if(!frameSignalAccepted){"));
    EXPECT_TRUE(ContainsText(present, "if(!m_rhiDevice){"));
    EXPECT_FALSE(ContainsText(
        present.substr(nonConsumedOffset, unconsumedFailureOffset - nonConsumedOffset),
        "resetFramePresentationSignal();"
    ));

    const usize renderOffset = graphics.find("void GraphicsRuntime::render(){");
    const usize averageOffset = graphics.find("void GraphicsRuntime::updateAverageFrameTime", renderOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    ASSERT_NE(averageOffset, AStringView::npos);
    const AStringView render = graphics.substr(renderOffset, averageOffset - renderOffset);
    EXPECT_TRUE(ContainsText(render, "Framebuffer* const framebuffer = m_acquiredPresentationFrame.framebuffer.get();"));
    EXPECT_FALSE(ContainsText(render, "getCurrent"));
    EXPECT_TRUE(ContainsText(render, "CpuTaskScope frameTasks(m_cpuScheduler, m_frameTaskProfileLabel);"));
    EXPECT_TRUE(ContainsText(render, ".priority = CpuTaskPriority::Critical"));
    EXPECT_TRUE(ContainsText(render, ".target = CpuTaskTarget::MainThread"));
    EXPECT_TRUE(ContainsText(render, "previous.valid() ? &previous : nullptr, previous.valid() ? 1u : 0u"));
    const usize submitOffset = render.find("previous = frameTasks.submit(");
    const usize prepareOffset = render.find("renderPass->prepareResources(framebuffer)", submitOffset);
    const usize drawOffset = render.find("renderPass->render(framebuffer);", prepareOffset);
    const usize joinOffset = render.find("frameTasks.wait();", drawOffset);
    ASSERT_NE(submitOffset, AStringView::npos);
    ASSERT_NE(prepareOffset, AStringView::npos);
    ASSERT_NE(drawOffset, AStringView::npos);
    ASSERT_NE(joinOffset, AStringView::npos);
    EXPECT_LT(submitOffset, prepareOffset);
    EXPECT_LT(prepareOffset, drawOffset);
    EXPECT_LT(drawOffset, joinOffset);

    const usize animateOffset = graphics.find("bool GraphicsRuntime::animateRenderPresentInternal");
    ASSERT_NE(animateOffset, AStringView::npos);
    const AStringView animate = graphics.substr(animateOffset);
    const usize entryClearOffset = animate.find("m_acquiredPresentationFrame = {};");
    const usize beginResultOffset = animate.find("BeginFrameResult beginFrameResult;");
    const usize acquireOffset = animate.find("AcquiredBackBuffer acquiredBackBuffer = Move(beginFrameResult.backBuffer);");
    const usize identityOffset = animate.find("acquiredFramebufferDesc.colorAttachments[0].texture != acquiredBackBuffer.texture.get()");
    const usize publishOffset = animate.find("m_acquiredPresentationFrame = {", acquireOffset);
    const usize resetOffset = animate.find("ScopedAcquiredPresentationFrameReset acquiredFrameReset", publishOffset);
    const usize preambleOffset = animate.find("prepareFramePreamble()", resetOffset);
    ASSERT_NE(entryClearOffset, AStringView::npos);
    ASSERT_NE(beginResultOffset, AStringView::npos);
    ASSERT_NE(acquireOffset, AStringView::npos);
    ASSERT_NE(identityOffset, AStringView::npos);
    ASSERT_NE(publishOffset, AStringView::npos);
    ASSERT_NE(resetOffset, AStringView::npos);
    ASSERT_NE(preambleOffset, AStringView::npos);
    EXPECT_LT(entryClearOffset, beginResultOffset);
    EXPECT_LT(beginResultOffset, acquireOffset);
    EXPECT_LT(acquireOffset, identityOffset);
    EXPECT_LT(identityOffset, publishOffset);
    EXPECT_LT(publishOffset, resetOffset);
    EXPECT_LT(resetOffset, preambleOffset);
    EXPECT_TRUE(ContainsText(animate, "acquiredFramebufferDesc.colorAttachments.size() != 1u"));
    EXPECT_TRUE(ContainsText(animate, "beginFrameResult = m_backend->beginFrame();"));
    EXPECT_TRUE(ContainsText(animate, "beginFrameResult.status == BeginFrameStatus::ResizeRequired"));
    EXPECT_TRUE(ContainsText(animate, "if(!beginFrameResult.acquired()){"));
    EXPECT_FALSE(ContainsText(animate, "BackBufferResizeCallbacks"));
    const usize missingFramebufferWarningOffset = animate.find("acquired swap-chain image has no matching framebuffer");
    const usize missingFramebufferAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", missingFramebufferWarningOffset);
    const usize missingFramebufferRecreationOffset = animate.find("requestDeviceRecreation()", missingFramebufferAbandonOffset);
    const usize attachmentWarningOffset = animate.find("acquired swap-chain image mismatches its framebuffer attachment");
    const usize attachmentAbandonOffset = animate.find("m_backend->abandonAcquiredFrame()", attachmentWarningOffset);
    const usize attachmentRecreationOffset = animate.find("requestDeviceRecreation()", attachmentAbandonOffset);
    ASSERT_NE(missingFramebufferWarningOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferAbandonOffset, AStringView::npos);
    ASSERT_NE(missingFramebufferRecreationOffset, AStringView::npos);
    ASSERT_NE(attachmentWarningOffset, AStringView::npos);
    ASSERT_NE(attachmentAbandonOffset, AStringView::npos);
    ASSERT_NE(attachmentRecreationOffset, AStringView::npos);
    EXPECT_LT(missingFramebufferWarningOffset, missingFramebufferAbandonOffset);
    EXPECT_LT(missingFramebufferAbandonOffset, missingFramebufferRecreationOffset);
    EXPECT_LT(missingFramebufferRecreationOffset, attachmentWarningOffset);
    EXPECT_LT(attachmentWarningOffset, attachmentAbandonOffset);
    EXPECT_LT(attachmentAbandonOffset, attachmentRecreationOffset);
    EXPECT_LT(attachmentRecreationOffset, publishOffset);
    const usize renderCallOffset = animate.find("render();", preambleOffset);
    const usize postRenderExitOffset = animate.find("if(m_deviceRecreationRequested || device.requiresRecreation()){", renderCallOffset);
    const usize postRenderAbandonOffset = animate.find("else if(!m_backend->abandonAcquiredFrame())", postRenderExitOffset);
    const usize presentCallOffset = animate.find("const bool presented = m_backend->present();", postRenderAbandonOffset);
    const usize presentFailureOffset = animate.find("if(!presented){", presentCallOffset);
    const usize presentAbandonOffset = animate.find("!device.requiresRecreation() && !m_backend->abandonAcquiredFrame()", presentFailureOffset);
    ASSERT_NE(renderCallOffset, AStringView::npos);
    ASSERT_NE(postRenderExitOffset, AStringView::npos);
    ASSERT_NE(postRenderAbandonOffset, AStringView::npos);
    ASSERT_NE(presentCallOffset, AStringView::npos);
    ASSERT_NE(presentFailureOffset, AStringView::npos);
    ASSERT_NE(presentAbandonOffset, AStringView::npos);
    EXPECT_LT(renderCallOffset, postRenderExitOffset);
    EXPECT_LT(postRenderExitOffset, postRenderAbandonOffset);
    EXPECT_LT(postRenderAbandonOffset, presentCallOffset);
    EXPECT_LT(presentCallOffset, presentFailureOffset);
    EXPECT_LT(presentFailureOffset, presentAbandonOffset);
    EXPECT_EQ(CountText(animate, "m_backend->abandonAcquiredFrame()"), 4u);
    EXPECT_TRUE(ContainsText(animate, "prepareFramePreamble() returns false only after the device requires recreation"));
    EXPECT_TRUE(ContainsText(animate, "required device teardown owns the unresolved acquired image and synchronization"));
    EXPECT_TRUE(ContainsText(graphics, "~ScopedAcquiredPresentationFrameReset(){ m_frame = {}; }"));
    EXPECT_TRUE(ContainsText(graphics, "bool GraphicsRuntime::init(const Common::FrameData& data){\n    m_acquiredPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(graphics, "bool GraphicsRuntime::createHeadlessDevice(){\n    m_acquiredPresentationFrame = {};"));

    const usize destroyLifecycleOffset = graphics.find("bool GraphicsRuntime::destroy(){");
    const usize destroyJobJoinOffset = graphics.find("waitTasks();", destroyLifecycleOffset);
    const usize destroyPrepareOffset = graphics.find(
        "prepareSwapChainTransition(SwapChainTransitionKind::Destroy, transitionTicket)",
        destroyJobJoinOffset
    );
    const usize destroySnapshotClearOffset = graphics.find("m_acquiredPresentationFrame = {};", destroyPrepareOffset);
    const usize destroyCommitOffset = graphics.find(
        "m_backend->commitDestroy(Move(transitionTicket))",
        destroySnapshotClearOffset
    );
    ASSERT_NE(destroyLifecycleOffset, AStringView::npos);
    ASSERT_NE(destroyJobJoinOffset, AStringView::npos);
    ASSERT_NE(destroyPrepareOffset, AStringView::npos);
    ASSERT_NE(destroySnapshotClearOffset, AStringView::npos);
    ASSERT_NE(destroyCommitOffset, AStringView::npos);
    EXPECT_LT(destroyLifecycleOffset, destroyJobJoinOffset);
    EXPECT_LT(destroyJobJoinOffset, destroyPrepareOffset);
    EXPECT_LT(destroyPrepareOffset, destroySnapshotClearOffset);
    EXPECT_LT(destroySnapshotClearOffset, destroyCommitOffset);

    const usize resizeLifecycleOffset = graphics.find("bool GraphicsRuntime::backBufferResizing(SwapChainTransitionTicket& outTicket){");
    const usize resizeJobJoinOffset = graphics.find("waitTasks();", resizeLifecycleOffset);
    const usize resizePrepareOffset = graphics.find(
        "prepareSwapChainTransition(SwapChainTransitionKind::Resize, outTicket)",
        resizeJobJoinOffset
    );
    const usize resizeSnapshotClearOffset = graphics.find("m_acquiredPresentationFrame = {};", resizePrepareOffset);
    ASSERT_NE(resizeLifecycleOffset, AStringView::npos);
    ASSERT_NE(resizeJobJoinOffset, AStringView::npos);
    ASSERT_NE(resizePrepareOffset, AStringView::npos);
    ASSERT_NE(resizeSnapshotClearOffset, AStringView::npos);
    EXPECT_LT(resizeLifecycleOffset, resizeJobJoinOffset);
    EXPECT_LT(resizeJobJoinOffset, resizePrepareOffset);
    EXPECT_LT(resizePrepareOffset, resizeSnapshotClearOffset);

    const usize backendPrepareOffset = backendOrchestration.find("bool BackendContext::prepareSwapChainTransition(");
    const usize backendAcquireJoinOffset = backendOrchestration.find(
        "waitAcquireSyncSlotsForLifecycle()",
        backendPrepareOffset
    );
    const usize backendDeviceJoinOffset = backendOrchestration.find(
        "const VkResult idleResult = m_rhiDevice->waitForNativeIdle();",
        backendAcquireJoinOffset
    );
    ASSERT_NE(backendPrepareOffset, AStringView::npos);
    ASSERT_NE(backendAcquireJoinOffset, AStringView::npos);
    ASSERT_NE(backendDeviceJoinOffset, AStringView::npos);
    EXPECT_LT(backendPrepareOffset, backendAcquireJoinOffset);
    EXPECT_LT(backendAcquireJoinOffset, backendDeviceJoinOffset);
    EXPECT_TRUE(ContainsText(
        backendOrchestration.substr(backendDeviceJoinOffset),
        "idleResult != VK_SUCCESS && !m_rhiDevice->isDeviceLost()"
    ));

    for(const AStringView setupSource : { rendererResources, ui }){
        EXPECT_TRUE(ContainsText(setupSource, "Pipeline compatibility setup uses the stable framebuffer-zero prototype"));
        EXPECT_TRUE(ContainsText(setupSource, "m_graphics.getFramebuffer(0u)"));
        EXPECT_FALSE(ContainsText(setupSource, "getCurrentFramebuffer"));
    }
}


// A compatibility present is still a real native transition submission. It must stay entirely behind the missing
// graph-signal branch, target the exact acquired WSI texture on the primary Graphics transport, and fail closed
// before vkQueuePresentKHR whenever recording, signal claiming, or submission cannot prove that transition.
TEST(EcsGraphics, CompatibilityPresentTransitionsExactAcquiredImageBeforeSignal){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString backendPresentationSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "backend_context_presentation.cpp",
        backendPresentationSource
    ));
    const AStringView backendPresentation(backendPresentationSource.data(), backendPresentationSource.size());

    const usize presentOffset = backendPresentation.find("bool BackendContext::present(){");
    ASSERT_NE(presentOffset, AStringView::npos);
    const AStringView present = backendPresentation.substr(presentOffset);
    const usize compatibilityBranchOffset = present.find("if(!frameSignalAccepted){");
    const usize presentInfoOffset = present.find("VkPresentInfoKHR presentInfo = {};");
    ASSERT_NE(compatibilityBranchOffset, AStringView::npos);
    ASSERT_NE(presentInfoOffset, AStringView::npos);
    ASSERT_LT(compatibilityBranchOffset, presentInfoOffset);

    const AStringView acceptedGraphSignalPath = present.substr(0u, compatibilityBranchOffset);
    const AStringView compatibilityBranch = present.substr(
        compatibilityBranchOffset,
        presentInfoOffset - compatibilityBranchOffset
    );
    EXPECT_TRUE(ContainsText(present, "if(!m_rhiDevice || !m_frameAcquired || !m_swapChain"));
    EXPECT_TRUE(ContainsText(
        acceptedGraphSignalPath,
        "m_framePresentationSignalState == FramePresentationSignalState::Accepted"
    ));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "ResolveCompatibilityPresentTransitionPolicy"));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "createCommandList"));
    EXPECT_FALSE(ContainsText(acceptedGraphSignalPath, "setTextureState"));
    EXPECT_EQ(CountText(present, "ResolveCompatibilityPresentTransitionPolicy"), 1u);
    EXPECT_EQ(CountText(present, "createCommandList"), 1u);
    EXPECT_EQ(CountText(present, "setTextureState"), 1u);

    const usize acquiredImageOffset = compatibilityBranch.find(
        "SwapChainImage& swapChainImage = m_swapChainImages[m_swapChainIndex];"
    );
    const usize policyOffset = compatibilityBranch.find("ResolveCompatibilityPresentTransitionPolicy(");
    const usize primaryQueueOffset = compatibilityBranch.find(
        "m_rhiDevice->getPrimaryPhysicalQueue(CommandQueue::Graphics)"
    );
    const usize exactQueueOffset = compatibilityBranch.find("commandListParams.setPhysicalQueue(primaryGraphicsQueue);");
    const usize createOffset = compatibilityBranch.find("m_rhiDevice->createCommandList(commandListParams)");
    const usize openOffset = compatibilityBranch.find("compatibilityCommandList->open();");
    const usize seedOffset = compatibilityBranch.find("compatibilityCommandList->beginTrackingTextureState(");
    const usize transitionOffset = compatibilityBranch.find("compatibilityCommandList->setTextureState(", seedOffset);
    const usize commitOffset = compatibilityBranch.find("compatibilityCommandList->commitBarriers();", transitionOffset);
    const usize closeOffset = compatibilityBranch.find("compatibilityCommandList->close();", commitOffset);
    const usize claimOffset = compatibilityBranch.find("claimFramePresentationSignal();", closeOffset);
    const usize hookOffset = compatibilityBranch.find("submitDesc.setPreSubmitHook(presentationSignalHook);", claimOffset);
    const usize listOffset = compatibilityBranch.find(
        "CommandList* const compatibilityCommandLists[] = { compatibilityCommandList.get() };",
        hookOffset
    );
    const usize executeOffset = compatibilityBranch.find("m_rhiDevice->executeCommandListsInternal(", listOffset);
    const usize confirmOffset = compatibilityBranch.find(
        "confirmFramePresentationSignal(presentationSignalHook, fallbackToken)",
        executeOffset
    );
    const usize acceptedOffset = compatibilityBranch.find("frameSignalAccepted = true;", confirmOffset);
    ASSERT_NE(acquiredImageOffset, AStringView::npos);
    ASSERT_NE(policyOffset, AStringView::npos);
    ASSERT_NE(primaryQueueOffset, AStringView::npos);
    ASSERT_NE(exactQueueOffset, AStringView::npos);
    ASSERT_NE(createOffset, AStringView::npos);
    ASSERT_NE(openOffset, AStringView::npos);
    ASSERT_NE(seedOffset, AStringView::npos);
    ASSERT_NE(transitionOffset, AStringView::npos);
    ASSERT_NE(commitOffset, AStringView::npos);
    ASSERT_NE(closeOffset, AStringView::npos);
    ASSERT_NE(claimOffset, AStringView::npos);
    ASSERT_NE(hookOffset, AStringView::npos);
    ASSERT_NE(listOffset, AStringView::npos);
    ASSERT_NE(executeOffset, AStringView::npos);
    ASSERT_NE(confirmOffset, AStringView::npos);
    ASSERT_NE(acceptedOffset, AStringView::npos);
    EXPECT_LT(acquiredImageOffset, policyOffset);
    EXPECT_LT(policyOffset, primaryQueueOffset);
    EXPECT_LT(primaryQueueOffset, exactQueueOffset);
    EXPECT_LT(exactQueueOffset, createOffset);
    EXPECT_LT(createOffset, openOffset);
    EXPECT_LT(openOffset, seedOffset);
    EXPECT_LT(seedOffset, transitionOffset);
    EXPECT_LT(transitionOffset, commitOffset);
    EXPECT_LT(commitOffset, closeOffset);
    EXPECT_LT(closeOffset, claimOffset);
    EXPECT_LT(claimOffset, hookOffset);
    EXPECT_LT(hookOffset, listOffset);
    EXPECT_LT(listOffset, executeOffset);
    EXPECT_LT(executeOffset, confirmOffset);
    EXPECT_LT(confirmOffset, acceptedOffset);
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "false,\n"
        "            Device::DeviceLossDiagnosticPolicy::Defer"
    ));

    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "swapChainImage.presentationState.nativeInitialState()"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::PreservePresent"
    ));
    EXPECT_EQ(CountText(compatibilityBranch, "swapChainImage.rhiHandle.get()"), 2u);
    EXPECT_EQ(CountText(compatibilityBranch, "s_AllSubresources"), 2u);
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!swapChainImage.rhiHandle\n"
        "            || transitionPolicy == VulkanDetail::CompatibilityPresentTransitionPolicy::Invalid\n"
        "            || !primaryGraphicsQueue.valid()"
    ));
    EXPECT_TRUE(ContainsText(compatibilityBranch, "if(!compatibilityCommandList){"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!compatibilityCommandList->hasCommandBuffer()\n"
        "            || !compatibilityCommandList->isRecording()\n"
        "            || compatibilityCommandList->commandRecordingFailed()"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "!compatibilityCommandList->hasCommandBuffer()\n"
        "            || compatibilityCommandList->isRecording()\n"
        "            || compatibilityCommandList->commandRecordingFailed()"
    ));
    EXPECT_TRUE(ContainsText(compatibilityBranch, "if(!presentationSignalHook.valid()){"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "compatibilityCommandLists,\n"
        "            LengthOf(compatibilityCommandLists),\n"
        "            primaryGraphicsQueue,\n"
        "            submitDesc"
    ));
    EXPECT_FALSE(ContainsText(compatibilityBranch, "executeCommandLists(nullptr, 0u"));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "if(!fallbackToken.valid()){\n"
        "            if(!cancelFramePresentationSignalDeferred(&presentationSignalHook, lifecycleLock))\n"
        "                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT(\"Vulkan: Failed to cancel presentation synchronization after submit rejection.\"));\n"
        "            captureDeviceLossAfterUnlock(\"queue submit\");\n"
        "            NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: Compatibility presentation transition/signal "
        "submission was rejected.\"));\n"
        "            return false;\n"
        "        }"
    ));
    EXPECT_TRUE(ContainsText(
        compatibilityBranch,
        "if(!confirmFramePresentationSignal(presentationSignalHook, fallbackToken)){\n"
        "            if(!cancelFramePresentationSignalDeferred(&presentationSignalHook, lifecycleLock))\n"
        "                NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT(\"Vulkan: Failed to cancel presentation synchronization after confirmation rejection.\"));\n"
        "            captureDeviceLossAfterUnlock(\"presentation signal cancellation\");\n"
        "            NWB_LOGGER_ERROR(NWB_TEXT(\"Vulkan: Accepted compatibility presentation submission "
        "failed signal confirmation/tracking.\"));\n"
        "            return false;\n"
        "        }"
    ));
    EXPECT_EQ(CountText(compatibilityBranch, "return false;"), 10u);
    EXPECT_EQ(CountText(compatibilityBranch, "cancelFramePresentationSignalDeferred(nullptr, lifecycleLock)"), 3u);
    EXPECT_EQ(CountText(compatibilityBranch, "cancelFramePresentationSignalDeferred(&presentationSignalHook, lifecycleLock)"), 2u);
}


// Renderer presentation must import the exact acquired swap-chain texture, preserve its captured native origin,
// and own the RenderTarget-to-Present state closure. The late record callback revalidates both graph identity and
// framebuffer attachment identity before touching the image.
TEST(EcsGraphics, RendererPresentationGraphBindsExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString contributorHeaderSource;
    AString rendererHeaderSource;
    AString rendererResourcesSource;
    AString rendererSource;
    AString presentationBuildSource;
    AString presentationTaskHeaderSource;
    AString presentationTaskSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "task" / "gpu" / "presentation_contributor.h",
        contributorHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", rendererHeaderSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp",
        rendererResourcesSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", rendererSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp",
        presentationBuildSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.h",
        presentationTaskHeaderSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_present_task.cpp",
        presentationTaskSource
    ));
    const AStringView contributorHeader(contributorHeaderSource.data(), contributorHeaderSource.size());
    const AStringView rendererHeader(rendererHeaderSource.data(), rendererHeaderSource.size());
    const AStringView rendererResources(rendererResourcesSource.data(), rendererResourcesSource.size());
    const AStringView renderer(rendererSource.data(), rendererSource.size());
    const AStringView presentationBuild(presentationBuildSource.data(), presentationBuildSource.size());
    const AStringView presentationTaskHeader(presentationTaskHeaderSource.data(), presentationTaskHeaderSource.size());
    const AStringView presentationTask(presentationTaskSource.data(), presentationTaskSource.size());

    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "prepareTaskGraphPresentation(const AcquiredPresentationFrame& frame)"
    ));
    EXPECT_TRUE(ContainsText(
        contributorHeader,
        "const AcquiredPresentationFrame& frame,\n"
        "        GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(contributorHeader, "exact typed acquired texture imported by"));
    EXPECT_FALSE(ContainsText(contributorHeader, "presentation hazard domain"));
    EXPECT_TRUE(ContainsText(rendererHeader, "const Core::AcquiredPresentationFrame& presentationFrame,"));
    EXPECT_TRUE(ContainsText(
        rendererResources,
        "else if(m_graphics.isDeviceRecreationRequested()){\n"
        "            NWB_LOGGER_CRITICAL_WARNING(NWB_TEXT(\"RendererSystem: presentation contributor requested recreation during preparation\"));\n"
        "            return false;"
    ));

    EXPECT_FALSE(ContainsText(
        presentationBuild,
        "HazardDomainDesc(Name(\"render.deferred_present.backbuffer\")"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        ".setInitialState(presentationFrame.backBuffer.nativeInitialState)\n"
        "        .setInitialAvailabilityCompletion(backBufferAvailability)\n"
        "        .setExternalFinalState(Core::ResourceStates::Present)"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        ".setToken(presentationFrame.backBuffer.availabilityCompletion)"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "const Core::GpuGraphResourceId backbuffer = m_deferredLightingTaskGraph.importTexture(\n"
        "        presentationFrame.backBuffer.texture,"
    ));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "WriteTextureUse(\n"
        "            backbuffer,\n"
        "            presentationFramebufferDesc.colorAttachments[0].subresources,\n"
        "            Core::ResourceStates::RenderTarget"
    ));
    EXPECT_TRUE(ContainsText(presentationBuild, ".presentationFrame = presentationFrame,"));
    EXPECT_TRUE(ContainsText(presentationBuild, ".backBuffer = backbuffer,"));
    EXPECT_TRUE(ContainsText(
        presentationBuild,
        "declarePresentEndpoint(Core::GpuPresentEndpoint{\n"
        "        .producer = m_deferredFrameTimingEndTask,\n"
        "        .backBuffer = backbuffer,"
    ));

    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::AcquiredPresentationFrame presentationFrame;"));
    EXPECT_TRUE(ContainsText(presentationTaskHeader, "Core::GpuGraphResourceId backBuffer;"));
    EXPECT_TRUE(ContainsText(presentationTask, "!payload.presentationFrame.valid()"));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "presentationFramebufferDesc.colorAttachments[0].texture != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "context.declarations.textureForResource(payload.backBuffer) != payload.presentationFrame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        presentationTask,
        "payload.deferredSystem->renderDeferredPresent(\n"
        "        commandList,\n"
        "        *payload.targets,\n"
        "        payload.presentationFrame"
    ));

    EXPECT_TRUE(ContainsText(
        renderer,
        "const Core::AcquiredPresentationFrame presentationFrame = m_graphics.acquiredPresentationFrame();"
    ));
    EXPECT_TRUE(ContainsText(renderer, "presentationFrame.framebuffer.get() != framebuffer"));
    EXPECT_TRUE(ContainsText(
        renderer,
        "presentationFramebufferDesc.colorAttachments[0].texture != presentationFrame.backBuffer.texture.get()"
    ));

    // Once the exact back-buffer writer accepted, generic suffix recovery cannot make the acquired image reusable.
    // The renderer must escalate that partial-acceptance edge to Graphics' recreation/abandonment path.
    const usize partialAcceptanceOffset = renderer.find(
        "const Core::QueueSubmissionToken deferredPresentSubmissionToken ="
    );
    const usize acceptedWriterOffset = renderer.find("m_deferredPresentTask", partialAcceptanceOffset);
    const usize recreationOffset = renderer.find(
        "if(deferredPresentSubmissionToken.valid() && !finalPresentationSubmissionToken.valid()){",
        acceptedWriterOffset
    );
    const usize recoveryFailureOffset = renderer.find("failFrameRenderRecovery();", recreationOffset);
    ASSERT_NE(partialAcceptanceOffset, AStringView::npos);
    ASSERT_NE(acceptedWriterOffset, AStringView::npos);
    ASSERT_NE(recreationOffset, AStringView::npos);
    ASSERT_NE(recoveryFailureOffset, AStringView::npos);
    EXPECT_LT(partialAcceptanceOffset, acceptedWriterOffset);
    EXPECT_LT(acceptedWriterOffset, recreationOffset);
    EXPECT_LT(recreationOffset, recoveryFailureOffset);
    EXPECT_TRUE(ContainsText(
        renderer.substr(recreationOffset, recoveryFailureOffset - recreationOffset),
        "acquired back buffer was written before presentation suffix rejection; requesting recreation"
    ));
}


// The deferred presentation triangle covers the complete acquired image, so only this proven full-overwrite pass may
// discard its incoming tiles. Automatic and barrier-resumed passes keep the preserving Load/Store defaults.
TEST(EcsGraphics, PresentPassDiscardsOnlyItsFullOverwriteLoad){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString presentSource;
    AString graphicsPipelineSource;
    AString stateTrackingSource;
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_composite.cpp",
        presentSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "graphics_pipeline.cpp",
        graphicsPipelineSource
    ));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "core" / "graphics" / "vulkan" / "state_tracking_barriers.cpp",
        stateTrackingSource
    ));
    const AStringView presentFile(presentSource.data(), presentSource.size());
    const AStringView graphicsPipeline(graphicsPipelineSource.data(), graphicsPipelineSource.size());
    const AStringView stateTracking(stateTrackingSource.data(), stateTrackingSource.size());

    const usize presentStart = presentFile.find("bool RendererDeferredSystem::renderDeferredPresent(");
    const usize presentEnd = presentFile.find(
        "////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////",
        presentStart
    );
    ASSERT_NE(presentStart, AStringView::npos);
    ASSERT_NE(presentEnd, AStringView::npos);
    const AStringView present = presentFile.substr(presentStart, presentEnd - presentStart);

    EXPECT_TRUE(ContainsText(
        present,
        "presentationFramebuffer.getFramebufferInfo().getViewport()"
    ));
    EXPECT_TRUE(ContainsText(
        present,
        "renderPassParameters.colorAttachmentActions[0u].loadAction = Core::RenderPassLoadAction::Discard;"
    ));
    EXPECT_TRUE(ContainsText(
        present,
        "renderPassParameters.colorAttachmentActions[0u].storeAction = Core::RenderPassStoreAction::Store;"
    ));
    const usize beginRenderPass = present.find(
        "commandList.beginRenderPass(presentationFramebuffer, renderPassParameters);"
    );
    const usize setGraphicsState = present.find("commandList.setGraphicsState(graphicsState);");
    const usize draw = present.find("commandList.draw(drawArgs);");
    const usize endRenderPass = present.find("commandList.endRenderPass();");
    ASSERT_NE(beginRenderPass, AStringView::npos);
    ASSERT_NE(setGraphicsState, AStringView::npos);
    ASSERT_NE(draw, AStringView::npos);
    ASSERT_NE(endRenderPass, AStringView::npos);
    EXPECT_LT(beginRenderPass, setGraphicsState);
    EXPECT_LT(draw, endRenderPass);
    EXPECT_TRUE(ContainsText(present, "return !commandList.isRenderPassActive();"));

    EXPECT_EQ(CountText(graphicsPipeline, "RenderPassParameters params = {};"), 1u);
    EXPECT_EQ(CountText(stateTracking, "RenderPassParameters params = {};"), 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

