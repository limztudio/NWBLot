// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_ui_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;


// Every UI presentation route consumes the same acquired-frame identity. Normal overlay work reuses the renderer's
// typed resource, standalone raster paths import that exact TextureHandle, and upload-only standalone work skips the
// external-final texture import entirely so it cannot manufacture an unwritten presentation endpoint.
TEST(EcsGraphics, UiPresentationGraphsBindExactAcquiredTexture){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());

    EXPECT_TRUE(ContainsText(
        uiHeader,
        "virtual bool prepareTaskGraphPresentation(const Core::AcquiredPresentationFrame& frame)override;"
    ));
    EXPECT_TRUE(ContainsText(
        uiHeader,
        "const Core::AcquiredPresentationFrame& frame,\n"
        "        Core::GpuGraphResourceId backbuffer,"
    ));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::AcquiredPresentationFrame m_taskGraphPresentationFrame;"));
    EXPECT_TRUE(ContainsText(
        ui,
        "framebufferDesc.colorAttachments[0].texture == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(
        ui,
        "graph.textureForResource(backbuffer) == frame.backBuffer.texture.get()"
    ));
    EXPECT_TRUE(ContainsText(ui, "ImportPresentationBackBuffer("));
    EXPECT_TRUE(ContainsText(ui, ".setType(Core::GpuGraphResourceType::Texture)"));
    EXPECT_TRUE(ContainsText(ui, ".setInitialState(frame.backBuffer.nativeInitialState)"));
    EXPECT_TRUE(ContainsText(ui, ".setInitialAvailabilityCompletion(availability)"));
    EXPECT_TRUE(ContainsText(ui, ".setExternalFinalState(Core::ResourceStates::Present)"));
    EXPECT_TRUE(ContainsText(ui, ".setToken(frame.backBuffer.availabilityCompletion)"));

    const usize renderTaskOffset = ui.find("struct UiSystem::TaskGraphRenderTask{");
    const usize uploadCompletionOffset = ui.find("struct UiSystem::TaskGraphUploadCompletionTask{", renderTaskOffset);
    const usize legacyTaskOffset = ui.find("struct UiSystem::StandaloneLegacyPresentationTask{");
    const usize nextTaskOffset = ui.find("UiSystem::UiSystem(", legacyTaskOffset);
    ASSERT_NE(renderTaskOffset, AStringView::npos);
    ASSERT_NE(uploadCompletionOffset, AStringView::npos);
    ASSERT_NE(legacyTaskOffset, AStringView::npos);
    ASSERT_NE(nextTaskOffset, AStringView::npos);
    const AStringView renderTask = ui.substr(renderTaskOffset, uploadCompletionOffset - renderTaskOffset);
    const AStringView legacyTask = ui.substr(legacyTaskOffset, nextTaskOffset - legacyTaskOffset);
    for(const AStringView task : { renderTask, legacyTask }){
        EXPECT_TRUE(ContainsText(task, "Core::AcquiredPresentationFrame frame;"));
        EXPECT_TRUE(ContainsText(task, "Core::GpuGraphResourceId backbuffer;"));
    }
    EXPECT_TRUE(ContainsText(renderTask, "payload.backbuffer,\n            context"));
    EXPECT_TRUE(ContainsText(legacyTask, "payload.backbuffer,"));
    EXPECT_TRUE(ContainsText(legacyTask, "context"));

    const usize declarationOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneOffset = ui.find("bool UiSystem::submitStandaloneTaskGraphPresentation", declarationOffset);
    const usize legacyDeclarationOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation",
        standaloneOffset
    );
    const usize legacySubmitOffset = ui.find(
        "bool UiSystem::submitStandaloneLegacyTaskGraphPresentation",
        legacyDeclarationOffset
    );
    const usize uploadGraphOffset = ui.find(
        "Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph",
        legacySubmitOffset
    );
    ASSERT_NE(declarationOffset, AStringView::npos);
    ASSERT_NE(standaloneOffset, AStringView::npos);
    ASSERT_NE(legacyDeclarationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_NE(uploadGraphOffset, AStringView::npos);
    const AStringView declaration = ui.substr(declarationOffset, standaloneOffset - declarationOffset);
    const AStringView standalone = ui.substr(standaloneOffset, legacyDeclarationOffset - standaloneOffset);
    const AStringView legacyDeclaration = ui.substr(
        legacyDeclarationOffset,
        legacySubmitOffset - legacyDeclarationOffset
    );
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, uploadGraphOffset - legacySubmitOffset);

    EXPECT_TRUE(ContainsText(
        declaration,
        "GraphBindsAcquiredPresentationTexture(declarations, frame, backbuffer)"
    ));
    EXPECT_TRUE(ContainsText(
        declaration,
        ".resource = backbuffer,\n"
        "            .range = {},\n"
        "            // Rasterization writes the exact renderer-owned presentation texture."
    ));
    EXPECT_TRUE(ContainsText(declaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(declaration, ".frame = frame,"));
    EXPECT_TRUE(ContainsText(declaration, ".backbuffer = backbuffer,"));

    const usize drawBranchOffset = standalone.find("context->ui->m_taskGraphDrawUploadsPrepared");
    const usize standaloneImportOffset = standalone.find(
        "backbuffer = __hidden_ui::ImportPresentationBackBuffer(",
        drawBranchOffset
    );
    const usize standaloneDeclareOffset = standalone.find(
        "return context->ui->declareTaskGraphPresentation(",
        standaloneImportOffset
    );
    ASSERT_NE(drawBranchOffset, AStringView::npos);
    ASSERT_NE(standaloneImportOffset, AStringView::npos);
    ASSERT_NE(standaloneDeclareOffset, AStringView::npos);
    EXPECT_LT(drawBranchOffset, standaloneImportOffset);
    EXPECT_LT(standaloneImportOffset, standaloneDeclareOffset);
    EXPECT_TRUE(ContainsText(standalone, "Core::GpuGraphResourceId backbuffer;"));
    EXPECT_TRUE(ContainsText(standalone, "graph,\n                    context->frame,"));
    EXPECT_TRUE(ContainsText(standalone, "ImportPresentationBackBuffer("));
    EXPECT_FALSE(ContainsText(standalone, "importHazardDomain"));

    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "const Core::GpuGraphResourceId backbuffer = __hidden_ui::ImportPresentationBackBuffer(\n"
        "        graph,\n"
        "        frame,"
    ));
    EXPECT_TRUE(ContainsText(legacyDeclaration, "ImportPresentationBackBuffer("));
    EXPECT_TRUE(ContainsText(legacyDeclaration, ".requiredState = Core::ResourceStates::RenderTarget,"));
    EXPECT_TRUE(ContainsText(
        legacyDeclaration,
        "GraphBindsAcquiredPresentationTexture(declarations, frame, backbuffer)"
    ));
    EXPECT_EQ(CountText(legacyDeclaration, "importHazardDomain("), 1u);
    EXPECT_TRUE(ContainsText(legacyDeclaration, "ui.imgui_standalone_legacy_presentation.callback"));
    EXPECT_EQ(CountText(ui, ".setType(Core::GpuGraphResourceType::HazardDomain)"), 1u);

    EXPECT_TRUE(ContainsText(
        ui,
        "GraphBindsAcquiredPresentationTexture(context.declarations, frame, backbuffer)"
    ));
    EXPECT_EQ(
        CountText(ui, "failed after its terminal packet was accepted; requesting recreation"),
        2u
    );
    for(const AStringView submit : { standalone, legacySubmit }){
        const usize acceptedFailureOffset = submit.find("if(!m_frameFinished){");
        const usize recreationRequestOffset = submit.find("m_graphics.requestDeviceRecreation();", acceptedFailureOffset);
        ASSERT_NE(acceptedFailureOffset, AStringView::npos);
        ASSERT_NE(recreationRequestOffset, AStringView::npos);
        EXPECT_LT(acceptedFailureOffset, recreationRequestOffset);
    }
}


// The graph-owned ImGui terminal task must record from declaration-time data. Re-reading ImGui's mutable command
// arrays after the task declares its sampled textures would allow an undeclared bindless access into the packet.
TEST(EcsGraphics, UiPresentationSnapshotsLateRecordInputs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    const AStringView ui(uiSource.data(), uiSource.size());

    AString uiHeaderSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "struct TaskGraphDrawCommand"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands"));
    EXPECT_TRUE(ContainsText(ui, "recordTaskGraphDrawSnapshot"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));

    const usize recordOffset = ui.find("bool UiSystem::recordTaskGraphPresentation");
    const usize opaqueRecordOffset = ui.find("bool UiSystem::recordStandaloneLegacyTaskGraphPresentation", recordOffset);
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(opaqueRecordOffset, AStringView::npos);
    ASSERT_LT(recordOffset, opaqueRecordOffset);
    const AStringView recordBody = ui.substr(recordOffset, opaqueRecordOffset - recordOffset);
    EXPECT_TRUE(ContainsText(recordBody, "recordTaskGraphDrawSnapshot(commandList, frame, backbuffer, context)"));
    EXPECT_FALSE(ContainsText(recordBody, "ImGui::GetDrawData()"));
    EXPECT_FALSE(ContainsText(recordBody, "renderDrawData(commandList, frame.framebuffer.get()"));

    // The separately named opaque fallback is intentionally the sole graph task allowed to touch live callback
    // storage, and it must guard that synchronous boundary against a changed ImGui frame.
    const usize completionOffset = ui.find("bool UiSystem::recordTaskGraphUploadCompletion", opaqueRecordOffset);
    ASSERT_NE(completionOffset, AStringView::npos);
    const AStringView opaqueRecord = ui.substr(opaqueRecordOffset, completionOffset - opaqueRecordOffset);
    EXPECT_TRUE(ContainsText(opaqueRecord, "ImGui::GetDrawData() != drawData"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "frameGeneration != m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaqueRecord, "renderDrawData(commandList, frame.framebuffer.get(), *drawData)"));
}


// Large immutable UI uploads may already select Transfer/Compute. Their persistent buffers and textures must be
// created with the matching graph-sharing contract before a same-class auxiliary queue can legally record them.
TEST(EcsGraphics, UiGraphUploadsDeclareConcurrentProducerFamilies){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiInternalSource;
    AString uiTextureSource;
    AString uiGraphicsResourceSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "ui_internal.h", uiInternalSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "graphics_resources.cpp", uiGraphicsResourceSource));
    const AStringView uiInternal(uiInternalSource.data(), uiInternalSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiGraphicsResources(uiGraphicsResourceSource.data(), uiGraphicsResourceSource.size());

    EXPECT_TRUE(ContainsText(uiInternal, "allowSameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiInternal, "allowCrossFamilySameClassQueueRouting = preferDedicatedTransport"));
    EXPECT_TRUE(ContainsText(uiTextures, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
    EXPECT_TRUE(ContainsText(uiGraphicsResources, "ResourceQueueSharing::GraphicsAsyncComputeAndTransfer"));
}


// A newly-created retained ImGui texture is natively Unknown until its upload accepts. The graph must preserve that
// origin for the first write, then use the descriptor ShaderResource state only after the accepted batch publishes it.
TEST(EcsGraphics, UiFreshTextureImportsPreserveNativeOrigins){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString uiHeaderSource;
    AString uiInternalSource;
    AString uiSource;
    AString uiTextureSource;
    AString uiSubmissionSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "ui_internal.h", uiInternalSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_submission.h", uiSubmissionSource));
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView uiInternal(uiInternalSource.data(), uiInternalSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());
    const AStringView uiSubmission(uiSubmissionSource.data(), uiSubmissionSource.size());

    EXPECT_TRUE(ContainsText(uiHeader, "bool initialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiHeader, "bool textureInitialUploadAccepted = false;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "bool* initialUploadAccepted = nullptr;"));
    EXPECT_TRUE(ContainsText(uiSubmission, "void add(ImTextureData& textureData, bool* const initialUploadAccepted = nullptr)"));

    const usize createStatusOffset = uiSubmission.find("case ImTextureStatus_WantCreate:");
    const usize updateStatusOffset = uiSubmission.find("case ImTextureStatus_WantUpdates:", createStatusOffset);
    const usize okStatusOffset = uiSubmission.find("case ImTextureStatus_OK:", updateStatusOffset);
    ASSERT_NE(createStatusOffset, AStringView::npos);
    ASSERT_NE(updateStatusOffset, AStringView::npos);
    ASSERT_NE(okStatusOffset, AStringView::npos);
    ASSERT_LT(createStatusOffset, updateStatusOffset);
    ASSERT_LT(updateStatusOffset, okStatusOffset);
    const AStringView createStatus = uiSubmission.substr(createStatusOffset, updateStatusOffset - createStatusOffset);
    const AStringView updateStatus = uiSubmission.substr(updateStatusOffset, okStatusOffset - updateStatusOffset);
    EXPECT_TRUE(ContainsText(
        createStatus,
        "if(request.initialUploadAccepted)\n                        *request.initialUploadAccepted = true;"
    ));
    EXPECT_FALSE(ContainsText(updateStatus, "initialUploadAccepted"));

    EXPECT_TRUE(ContainsText(
        uiInternal,
        "TextureResourceDesc(\n    const Core::TextureDesc& textureDesc,\n    const bool initialUploadAccepted\n)"
    ));
    EXPECT_TRUE(ContainsText(
        uiInternal,
        ".setInitialState(initialUploadAccepted ? textureDesc.initialState : Core::ResourceStates::Unknown)"
    ));
    EXPECT_TRUE(ContainsText(
        uiTextures,
        "UiDetail::TextureResourceDesc(resource.texture->getCreationDescription(), resource.initialUploadAccepted)"
    ));
    EXPECT_TRUE(ContainsText(uiTextures, "m_textureUploadBatch.add(*textureData, &resource->initialUploadAccepted)"));
    EXPECT_TRUE(ContainsText(ui, ".textureInitialUploadAccepted = textureResource->initialUploadAccepted,"));
    EXPECT_TRUE(ContainsText(ui, "const auto appendDrawTextureUse = [&](const TaskGraphDrawCommand& drawCommand){"));
    EXPECT_TRUE(ContainsText(
        ui,
        "UiDetail::TextureResourceDesc(\n"
        "                    drawCommand.texture->getCreationDescription(),\n"
        "                    drawCommand.textureInitialUploadAccepted\n"
        "                )"
    ));
    EXPECT_FALSE(ContainsText(uiTextures, "__hidden_ui::TextureResourceDesc"));
    EXPECT_FALSE(ContainsText(ui, "__hidden_ui::TextureResourceDesc"));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand)"));
    EXPECT_TRUE(ContainsText(ui, "m_textureUploadBatch.complete(true);"));
}


// Every ImGui presentation route remains graph-owned. Callback-free rejection keeps the exact live frame until a
// later acquired image can rebuild its snapshot; an opaque callback rejection cannot be replayed and fails closed.
TEST(EcsGraphics, UiPresentationRetriesOnlyThroughStandaloneGraphs){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadGraphicsRuntimeSources(repoRoot, graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "StandaloneTaskGraphDeclaration"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(graphics, "GraphicsRuntime::submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneTextureUploadCompletionTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneTextureUploadGraph"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneLegacyPresentationTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneLegacyTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "OpaquePresentationQueueRequest"));
    EXPECT_TRUE(ContainsText(ui, "Standalone ImGui Presentation Back Buffer"));
    EXPECT_TRUE(ContainsText(ui, "ImGui Opaque Callback Domain"));
    EXPECT_TRUE(ContainsText(ui, "if(prepareTaskGraphPresentation(frame))"));
    EXPECT_TRUE(ContainsText(uiHeader, "m_taskGraphPresentationRetryPending"));
    EXPECT_FALSE(ContainsText(uiHeader, "ensureRenderCommandList"));
    EXPECT_FALSE(ContainsText(uiHeader, "m_renderCommandList"));
    EXPECT_FALSE(ContainsText(ui, "executeCommandLists("));
    EXPECT_FALSE(ContainsText(ui, "ensureRenderCommandList"));

    const usize opaquePresentationOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneLegacyTaskGraphPresentation");
    const usize legacySubmitOffset = ui.find("bool UiSystem::submitPreparedLegacyTextureUploads");
    ASSERT_NE(opaquePresentationOffset, AStringView::npos);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_LT(opaquePresentationOffset, legacySubmitOffset);
    const AStringView opaquePresentation = ui.substr(opaquePresentationOffset, legacySubmitOffset - opaquePresentationOffset);
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "importTaskGraphTexture(graph, *textureResource)"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "m_frameGeneration"));
    EXPECT_TRUE(ContainsText(opaquePresentation, "setQueue(__hidden_ui::OpaquePresentationQueueRequest())"));
    EXPECT_TRUE(ContainsText(ui, "recordStandaloneLegacyTaskGraphPresentation"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(opaquePresentation, "createCommandList"));

    const usize renderOffset = ui.find("void UiSystem::render", legacySubmitOffset);
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, renderOffset - legacySubmitOffset);
    EXPECT_TRUE(ContainsText(legacySubmit, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(legacySubmit, "getPrimaryPhysicalQueue(Core::CommandQueue::Graphics)"));
    EXPECT_TRUE(ContainsText(legacySubmit, "submissionToken,\n        graphicsQueue"));
    EXPECT_FALSE(ContainsText(legacySubmit, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(legacySubmit, "createCommandList"));
    EXPECT_FALSE(ContainsText(legacySubmit, "prepareTextureRequests"));
    EXPECT_FALSE(ContainsText(ui, "m_prepareCommandList"));
    EXPECT_FALSE(ContainsText(uiTextures, "recordTextureUpload"));
    EXPECT_TRUE(ContainsText(uiTextures, "if(previousTask.valid())"));

    const usize presentationDeclareOffset = ui.find("Core::GpuTaskId UiSystem::declareTaskGraphPresentation");
    const usize standaloneTextureOffset = ui.find("Core::GpuTaskId UiSystem::declareStandaloneTextureUploadGraph");
    ASSERT_NE(presentationDeclareOffset, AStringView::npos);
    ASSERT_NE(standaloneTextureOffset, AStringView::npos);
    const AStringView presentationDeclare = ui.substr(
        presentationDeclareOffset,
        standaloneTextureOffset - presentationDeclareOffset
    );
    EXPECT_FALSE(ContainsText(presentationDeclare, "|| !previousTask.valid()"));
    EXPECT_TRUE(ContainsText(presentationDeclare, "if(previousTask.valid())"));

    const usize resizeOffset = ui.find("void UiSystem::backBufferResizing", renderOffset);
    ASSERT_NE(resizeOffset, AStringView::npos);
    const AStringView presentationRenderBody = ui.substr(renderOffset, resizeOffset - renderOffset);
    const usize standalonePresentationOffset = presentationRenderBody.find("submitStandaloneTaskGraphPresentation(frame)");
    const usize opaquePresentationFallbackOffset = presentationRenderBody.find("submitStandaloneLegacyTaskGraphPresentation(frame)");
    const usize retainedRetryOffset = presentationRenderBody.find("retainTaskGraphPresentationForRetry();");
    const usize textureOnlyOffset = presentationRenderBody.find("submitPreparedLegacyTextureUploads(*drawData)");
    ASSERT_NE(standalonePresentationOffset, AStringView::npos);
    ASSERT_NE(opaquePresentationFallbackOffset, AStringView::npos);
    ASSERT_NE(retainedRetryOffset, AStringView::npos);
    ASSERT_NE(textureOnlyOffset, AStringView::npos);
    EXPECT_LT(standalonePresentationOffset, opaquePresentationFallbackOffset);
    EXPECT_LT(opaquePresentationFallbackOffset, retainedRetryOffset);
    EXPECT_LT(retainedRetryOffset, textureOnlyOffset);
    EXPECT_TRUE(ContainsText(presentationRenderBody, "const bool retrySafe = m_taskGraphDrawUploadsPrepared;"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "if(m_graphics.isDeviceRecreationRequested())"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "if(!retrySafe){"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "after an opaque callback may have executed; requesting recreation"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "retaining callback-free frame for graph retry"));

    const usize visibleDrawOffset = presentationRenderBody.find("if(hasVisibleDraw){");
    ASSERT_NE(visibleDrawOffset, AStringView::npos);
    ASSERT_LT(visibleDrawOffset, textureOnlyOffset);
    const AStringView visibleDrawBody = presentationRenderBody.substr(visibleDrawOffset, textureOnlyOffset - visibleDrawOffset);
    const usize legacyAttemptOffset = visibleDrawBody.find("submitStandaloneLegacyTaskGraphPresentation(frame)");
    const usize acceptedTerminalExclusionOffset = visibleDrawBody.find("if(!m_frameFinished)", legacyAttemptOffset);
    const usize recreationExclusionOffset = visibleDrawBody.find(
        "if(m_graphics.isDeviceRecreationRequested())",
        acceptedTerminalExclusionOffset
    );
    const usize callbackPolicyOffset = visibleDrawBody.find("if(!retrySafe){", recreationExclusionOffset);
    const usize callbackFreeRetryOffset = visibleDrawBody.find(
        "retainTaskGraphPresentationForRetry();",
        callbackPolicyOffset
    );
    ASSERT_NE(legacyAttemptOffset, AStringView::npos);
    ASSERT_NE(acceptedTerminalExclusionOffset, AStringView::npos);
    ASSERT_NE(recreationExclusionOffset, AStringView::npos);
    ASSERT_NE(callbackPolicyOffset, AStringView::npos);
    ASSERT_NE(callbackFreeRetryOffset, AStringView::npos);
    EXPECT_LT(legacyAttemptOffset, acceptedTerminalExclusionOffset);
    EXPECT_LT(acceptedTerminalExclusionOffset, recreationExclusionOffset);
    EXPECT_LT(recreationExclusionOffset, callbackPolicyOffset);
    EXPECT_LT(callbackPolicyOffset, callbackFreeRetryOffset);
    EXPECT_FALSE(ContainsText(visibleDrawBody, "submitPreparedLegacyTextureUploads"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "if(!m_frameFinished)"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "if(m_graphics.isDeviceRecreationRequested())"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "m_graphics.requestDeviceRecreation();"));
    EXPECT_TRUE(ContainsText(visibleDrawBody, "retainTaskGraphPresentationForRetry();"));

    const usize updateOffset = ui.find("void UiSystem::update");
    const usize beginFrameOffset = ui.find("void UiSystem::beginFrame", updateOffset);
    ASSERT_NE(updateOffset, AStringView::npos);
    ASSERT_NE(beginFrameOffset, AStringView::npos);
    const AStringView updateBody = ui.substr(updateOffset, beginFrameOffset - updateOffset);
    const usize retryGateOffset = updateBody.find("if(m_taskGraphPresentationRetryPending)");
    const usize beginCallOffset = updateBody.find("beginFrame(delta)");
    ASSERT_NE(retryGateOffset, AStringView::npos);
    ASSERT_NE(beginCallOffset, AStringView::npos);
    EXPECT_LT(retryGateOffset, beginCallOffset);

    const usize confirmOffset = ui.find("void UiSystem::confirmTaskGraphPresentationSubmission");
    const usize retainDefinitionOffset = ui.find("void UiSystem::retainTaskGraphPresentationForRetry", confirmOffset);
    const usize discardOffset = ui.find("void UiSystem::discardStandaloneLegacyTaskGraphPresentation", retainDefinitionOffset);
    ASSERT_NE(confirmOffset, AStringView::npos);
    ASSERT_NE(retainDefinitionOffset, AStringView::npos);
    ASSERT_NE(discardOffset, AStringView::npos);
    const AStringView confirmBody = ui.substr(confirmOffset, retainDefinitionOffset - confirmOffset);
    const AStringView retainBody = ui.substr(retainDefinitionOffset, discardOffset - retainDefinitionOffset);
    EXPECT_TRUE(ContainsText(confirmBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_textureUploadBatch.complete(false);"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationRetryPending = true;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationPrepared = false;"));
    EXPECT_TRUE(ContainsText(retainBody, "m_taskGraphPresentationFrame = {};"));
    EXPECT_TRUE(ContainsText(retainBody, "clearTaskGraphDrawSnapshot();"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameStarted = false;"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameFinished = false;"));
    EXPECT_FALSE(ContainsText(retainBody, "m_frameGeneration"));

    const usize textureCompletionTaskOffset = ui.find("struct UiSystem::StandaloneTextureUploadCompletionTask");
    const usize legacyTaskOffset = ui.find("struct UiSystem::StandaloneLegacyPresentationTask", textureCompletionTaskOffset);
    ASSERT_NE(textureCompletionTaskOffset, AStringView::npos);
    ASSERT_NE(legacyTaskOffset, AStringView::npos);
    const AStringView textureCompletionTask = ui.substr(
        textureCompletionTaskOffset,
        legacyTaskOffset - textureCompletionTaskOffset
    );
    const AStringView textureOnlyRenderBody = presentationRenderBody.substr(textureOnlyOffset);
    EXPECT_TRUE(ContainsText(textureCompletionTask, "m_textureUploadBatch.complete(true);"));
    EXPECT_FALSE(ContainsText(textureCompletionTask, "m_frameStarted = false;"));
    EXPECT_FALSE(ContainsText(textureCompletionTask, "m_frameFinished = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_frameStarted = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_frameFinished = false;"));
    EXPECT_TRUE(ContainsText(textureOnlyRenderBody, "m_taskGraphPresentationRetryPending = false;"));

    const usize invalidateOffset = ui.find("void UiSystem::invalidateResources");
    const usize prepareResourcesOffset = ui.find("bool UiSystem::prepareResources", invalidateOffset);
    ASSERT_NE(invalidateOffset, AStringView::npos);
    ASSERT_NE(prepareResourcesOffset, AStringView::npos);
    const AStringView invalidateBody = ui.substr(invalidateOffset, prepareResourcesOffset - invalidateOffset);
    const AStringView resizeBody = ui.substr(resizeOffset);
    EXPECT_TRUE(ContainsText(invalidateBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(resizeBody, "m_taskGraphPresentationRetryPending = false;"));
    EXPECT_TRUE(ContainsText(presentationRenderBody, "m_taskGraphPresentationRetryPending = false;"));
}


// Public setup uploads return only a resource handle, so a Transfer/Compute producer must still establish queue
// order for later legacy consumers. Keep those readiness packets inside the same graph transaction instead of
// issuing an opaque direct zero-command submission after graph acceptance.
TEST(EcsGraphics, SetupUploadReadinessBridgeRemainsGraphOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsSource;
    AString textureUploadSource;
    ASSERT_TRUE(ReadGraphicsRuntimeSources(repoRoot, graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "runtime" / "runtime_texture_upload.cpp", textureUploadSource));
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView textureUpload(textureUploadSource.data(), textureUploadSource.size());

    EXPECT_TRUE(ContainsText(graphics, "SetupUploadReadinessBridgeGraphTask"));
    EXPECT_TRUE(ContainsText(graphics, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(graphics, "graphics.setup_upload.readiness_bridge"));
    EXPECT_FALSE(ContainsText(graphics, "BridgeSetupUploadToConsumerQueues"));

    const usize setupGraphOffset = graphics.find("GpuTaskId DeclareSetupUploadGraph");
    const usize timingResetOffset = graphics.find("struct FrameTimingResetGraphTask", setupGraphOffset);
    const usize setupUploadOffset = graphics.find("bool SubmitGraphOwnedSetupUpload");
    const usize standaloneGraphOffset = graphics.find("bool GraphicsRuntime::submitStandaloneTaskGraph", setupUploadOffset);
    ASSERT_NE(setupGraphOffset, AStringView::npos);
    ASSERT_NE(timingResetOffset, AStringView::npos);
    ASSERT_NE(setupUploadOffset, AStringView::npos);
    ASSERT_NE(standaloneGraphOffset, AStringView::npos);
    const AStringView setupGraph = graphics.substr(setupGraphOffset, timingResetOffset - setupGraphOffset);
    const AStringView setupUpload = graphics.substr(setupUploadOffset, standaloneGraphOffset - setupUploadOffset);
    EXPECT_TRUE(ContainsText(setupGraph, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(setupUpload, "bridgePrimaryUploadQueue"));
    EXPECT_TRUE(ContainsText(setupUpload, "requiredTerminalQueue"));
    EXPECT_FALSE(ContainsText(setupUpload, "executeCommandLists"));
    EXPECT_TRUE(ContainsText(graphics, "outSubmissionToken = transaction.taskToken(compiledPlan, terminalTask);"));
    EXPECT_FALSE(ContainsText(graphics, "outSubmissionToken = transaction.packetToken(terminalPacket);"));
    EXPECT_TRUE(ContainsText(graphics, "ResolveSetupUploadSameClassRouting"));
    EXPECT_TRUE(ContainsText(graphics, "preferNonPrimarySameClassQueue"));
    EXPECT_TRUE(ContainsText(graphics, "sameClassRouting.enabled ? sameClassRouting.primaryQueue"));

    const usize textureBatchOffset = graphics.find("bool GraphicsRuntime::uploadTextureBatch");
    const usize meshSetupOffset = graphics.find("GraphicsRuntime::MeshResource GraphicsRuntime::setupMesh", textureBatchOffset);
    ASSERT_NE(textureBatchOffset, AStringView::npos);
    ASSERT_NE(meshSetupOffset, AStringView::npos);
    const AStringView textureBatch = graphics.substr(textureBatchOffset, meshSetupOffset - textureBatchOffset);
    EXPECT_TRUE(ContainsText(textureUpload, "preserveSameClassQueueWithDirectDependency"));
    EXPECT_TRUE(ContainsText(textureBatch, "sameClassRouting.crossesQueueFamily"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

