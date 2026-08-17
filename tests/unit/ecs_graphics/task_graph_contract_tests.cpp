// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_ecs_graphics_task_graph_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct TaskGraphContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<TaskGraphContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    AString normalized;
    normalized.reserve(text.size());
    for(const char ch : text){
        if(ch != '\r')
            normalized += ch;
    }
    return AStringView(normalized.data(), normalized.size()).find(expected) != AStringView::npos;
}

static usize CountText(const AStringView text, const AStringView expected){
    if(expected.empty())
        return 0u;
    usize count = 0u;
    usize offset = 0u;
    while(offset < text.size()){
        const usize found = text.find(expected, offset);
        if(found == AStringView::npos)
            break;
        ++count;
        offset = found + expected.size();
    }
    return count;
}

static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


// Caustics and Surfel GI choose a semantic producer task at graph declaration. Keep their normal-frame merge and
// presence validation task-based so a later packet split cannot leak compiler packet identities back into the
// renderer's effect policy.
TEST(EcsGraphics, EffectsTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "const Core::GpuTaskId causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticPhotonTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredCausticResolveUpsampleTask,\n            causticsTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiIrradianceClearTask,\n            m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "m_deferredSurfelGiResolveTask,\n                m_deferredSurfelGiTask"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSurfelGiTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredHardwareCausticsTask)"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId hardwareCausticsPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticPhotonPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId surfelGiPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId causticsPacket"));
}


// Prefix and shadow record spans are task-addressed. The renderer can still query the compiler for the exact
// terminal presentation packet elsewhere, but ordinary readiness and merge validation must not mirror packet IDs.
TEST(EcsGraphics, PrefixAndShadowTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowPrepareTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_graphicsPrefixDeferredClearFirstTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredShadowVisibilityTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredSoftwareCausticsTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_graphicsPrefixDeferredClearFirstTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowPreparePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId graphicsPrefixPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId shadowVisibilityPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId softwareCausticsPacket"));
}


// The AVBOIT routing choice can still produce one or five submissions, but validation must ask whether semantic
// stages compiled and share their declared packet rather than duplicate packet IDs for each stage.
TEST(EcsGraphics, AvboitTopologyUsesSemanticTaskAnchors){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitPreTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitDepthWarpTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitExtinctionTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitIntegrationTask)"));
    EXPECT_TRUE(ContainsText(system, "taskIsCompiled(m_deferredAvboitAccumulationTask)"));
    EXPECT_TRUE(ContainsText(system, "tasksSharePacket(\n            m_deferredAvboitPreTask"));

    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitPrePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitDepthWarpPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitExtinctionPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitIntegrationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId avboitAccumulationPacket"));
}


// The exact terminal packet is retained solely for the swap-chain binary signal. Every other normal renderer
// readiness check uses a declared task anchor or a semantic task range.
TEST(EcsGraphics, OnlyTerminalPresentationRetainsAPacketIdentity){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString systemSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "system.cpp", systemSource));
    const AStringView system(systemSource.data(), systemSource.size());

    EXPECT_EQ(CountText(system, "packetForTask("), 1u);
    EXPECT_TRUE(ContainsText(system, "packetForTask(terminalPresentationTask)"));
    EXPECT_TRUE(ContainsText(system, "terminalPresentationPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLightingPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredCompositePacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredPresentPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredLaggedLightingHistoryPacket"));
    EXPECT_FALSE(ContainsText(system, "GpuSubmissionPacketId deferredFrameRecoveryPacket"));
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
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand.texture)"));

    const usize recordOffset = ui.find("bool UiSystem::recordTaskGraphPresentation");
    const usize completionOffset = ui.find("bool UiSystem::recordTaskGraphUploadCompletion");
    ASSERT_NE(recordOffset, AStringView::npos);
    ASSERT_NE(completionOffset, AStringView::npos);
    ASSERT_LT(recordOffset, completionOffset);
    const AStringView recordBody = ui.substr(recordOffset, completionOffset - recordOffset);
    EXPECT_TRUE(ContainsText(recordBody, "recordTaskGraphDrawSnapshot(commandList, framebuffer)"));
    EXPECT_FALSE(ContainsText(recordBody, "ImGui::GetDrawData()"));
    EXPECT_FALSE(ContainsText(recordBody, "renderDrawData(commandList, framebuffer"));
}


// The exceptional non-renderer/custom-callback UI route must not reopen the old preparation command list merely to
// upload requested ImGui textures. It may retain direct rasterization when no presentation graph exists or a callback
// is arbitrary, but its mutable texture bytes and status publication now belong to an isolated graph with one
// terminal acceptance task.
TEST(EcsGraphics, UiLegacyTextureFallbackUsesStandaloneGraphUpload){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsHeaderSource;
    AString graphicsSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.h", graphicsHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));
    const AStringView graphicsHeader(graphicsHeaderSource.data(), graphicsHeaderSource.size());
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    EXPECT_TRUE(ContainsText(graphicsHeader, "StandaloneTaskGraphDeclaration"));
    EXPECT_TRUE(ContainsText(graphicsHeader, "submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(graphics, "Graphics::submitStandaloneTaskGraph"));
    EXPECT_TRUE(ContainsText(ui, "StandaloneTextureUploadCompletionTask"));
    EXPECT_TRUE(ContainsText(ui, "declareStandaloneTextureUploadGraph"));
    EXPECT_TRUE(ContainsText(ui, "submitStandaloneTaskGraphPresentation"));
    EXPECT_TRUE(ContainsText(ui, "Standalone ImGui Presentation Back Buffer"));
    EXPECT_TRUE(ContainsText(ui, "if(prepareTaskGraphPresentation(framebuffer))"));

    const usize legacySubmitOffset = ui.find("bool UiSystem::submitLegacyTextureRequests");
    const usize renderOffset = ui.find("void UiSystem::render", legacySubmitOffset);
    ASSERT_NE(legacySubmitOffset, AStringView::npos);
    ASSERT_NE(renderOffset, AStringView::npos);
    const AStringView legacySubmit = ui.substr(legacySubmitOffset, renderOffset - legacySubmitOffset);
    EXPECT_TRUE(ContainsText(legacySubmit, "m_graphics.submitStandaloneTaskGraph"));
    EXPECT_FALSE(ContainsText(legacySubmit, "executeCommandLists"));
    EXPECT_FALSE(ContainsText(legacySubmit, "createCommandList"));
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

    const AStringView renderBody = ui.substr(renderOffset);
    const usize standalonePresentationOffset = renderBody.find("submitStandaloneTaskGraphPresentation(framebuffer)");
    const usize directTextureFallbackOffset = renderBody.find("submitLegacyTextureRequests(*drawData)");
    ASSERT_NE(standalonePresentationOffset, AStringView::npos);
    ASSERT_NE(directTextureFallbackOffset, AStringView::npos);
    EXPECT_LT(standalonePresentationOffset, directTextureFallbackOffset);
}


// Public setup uploads return only a resource handle, so a Transfer/Compute producer must still establish queue
// order for later legacy consumers. Keep those readiness packets inside the same graph transaction instead of
// issuing an opaque direct zero-command submission after graph acceptance.
TEST(EcsGraphics, SetupUploadReadinessBridgeRemainsGraphOwned){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString graphicsSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "module.cpp", graphicsSource));
    const AStringView graphics(graphicsSource.data(), graphicsSource.size());

    EXPECT_TRUE(ContainsText(graphics, "SetupUploadReadinessBridgeGraphTask"));
    EXPECT_TRUE(ContainsText(graphics, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_TRUE(ContainsText(graphics, "graphics.setup_upload.readiness_bridge"));
    EXPECT_FALSE(ContainsText(graphics, "BridgeSetupUploadToConsumerQueues"));

    const usize setupUploadOffset = graphics.find("static bool SubmitGraphOwnedSetupUpload");
    const usize timingResetOffset = graphics.find("struct FrameTimingResetGraphTask", setupUploadOffset);
    ASSERT_NE(setupUploadOffset, AStringView::npos);
    ASSERT_NE(timingResetOffset, AStringView::npos);
    const AStringView setupUpload = graphics.substr(setupUploadOffset, timingResetOffset - setupUploadOffset);
    EXPECT_TRUE(ContainsText(setupUpload, "DeclareSetupUploadReadinessBridgeTasks"));
    EXPECT_FALSE(ContainsText(setupUpload, "executeCommandLists"));
}


// The current renderer has exactly two runtime-selected sampled-image domains: material Texture2D assets (shared
// by raster and ray-trace surface dispatch) and ImGui textures.  A new domain must not silently rely on a global
// descriptor slot: keep the supported domain small and require each one to retain handles before graph declaration.
TEST(EcsGraphics, DynamicBindlessSampledImagesHaveFrozenGraphDeclarationOwners){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString materialAssetHeaderSource;
    AString materialSurfaceSource;
    AString taskGraphSource;
    AString uiHeaderSource;
    AString uiSource;
    AString uiTextureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "assets_material" / "asset.h", materialAssetHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.h", uiHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "system.cpp", uiSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_ui" / "texture_resources.cpp", uiTextureSource));

    const AStringView materialAssetHeader(materialAssetHeaderSource.data(), materialAssetHeaderSource.size());
    const AStringView materialSurface(materialSurfaceSource.data(), materialSurfaceSource.size());
    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView uiHeader(uiHeaderSource.data(), uiHeaderSource.size());
    const AStringView ui(uiSource.data(), uiSource.size());
    const AStringView uiTextures(uiTextureSource.data(), uiTextureSource.size());

    // Material resource validation supports only a Texture2D asset and a sampler. The prepared collector must
    // resolve the former to a retained texture handle, while samplers deliberately have no resource state to track.
    EXPECT_TRUE(ContainsText(materialAssetHeader, "SampledImage2D = 1"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "Sampler = 2"));
    EXPECT_TRUE(ContainsText(materialAssetHeader, "return resourceKind == MaterialResourceKind::SampledImage2D || resourceKind == MaterialResourceKind::Sampler"));
    EXPECT_TRUE(ContainsText(materialSurface, "appendPreparedMaterialSurfaceSampledTextures"));
    EXPECT_TRUE(ContainsText(materialSurface, "inOutTextures.push_back(textureResource.texture)"));
    EXPECT_TRUE(ContainsText(materialSurface, "default:\n            return false;"));

    // Raster and trace consumers share the frozen material collection. The named sets make a future dynamic
    // bindless consumer visible to the audit rather than allowing it to hide behind the descriptor heap.
    EXPECT_TRUE(ContainsText(taskGraph, "GatherPreparedMaterialSampledTextureResourceSet"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.gbuffer.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.graphics_prefix.csg_interval_sample.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.intervals.transparent_csg_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.occupancy.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.extinction.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.avboit.accumulation.material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "render.trace_material_sampled_textures"));
    EXPECT_TRUE(ContainsText(taskGraph, "traceMaterialSampledTextureSetUse"));

    // ImGui is the other dynamic domain. Its draw command retains the selected texture and heap slot, its upload
    // path imports the exact destination, and the terminal task declares that frozen texture rather than reading
    // the mutable ImGui command list.
    EXPECT_TRUE(ContainsText(uiHeader, "Core::TextureHandle texture;"));
    EXPECT_TRUE(ContainsText(uiHeader, "Core::GpuDescriptorHandle textureHeapHandle"));
    EXPECT_TRUE(ContainsText(uiTextures, "heap.allocate(Core::GpuDescriptorClass::SampledImage)"));
    EXPECT_TRUE(ContainsText(uiTextures, "importTaskGraphTexture(graph, *resource)"));
    EXPECT_TRUE(ContainsText(uiTextures, "graph.addUploadTextureTask("));
    EXPECT_TRUE(ContainsText(ui, "appendDrawTextureUse(drawCommand.texture)"));
    EXPECT_TRUE(ContainsText(ui, "m_taskGraphDrawCommands.push_back(TaskGraphDrawCommand{"));
    EXPECT_TRUE(ContainsText(ui, "graph-owned ImGui overlay cannot safely record a custom draw callback"));
}


// The hybrid HW-to-SW tail may fail after the software material context has replaced the opaque-HW context. Its
// successful frozen restore must use declaration-time graph blobs; only a stale snapshot may retain the existing
// direct re-gather/retry boundary, which disables later consumers before they can observe undeclared resources.
TEST(EcsGraphics, HybridHardwareFallbackRestoreUsesGraphOwnedBlobsWhenFrozen){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);

    AString taskGraphSource;
    AString rayTracingHeaderSource;
    AString rayTracingSource;
    AString swBvhSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "kernel" / "task_graph.cpp", taskGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", swBvhSource));

    const AStringView taskGraph(taskGraphSource.data(), taskGraphSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracing(rayTracingSource.data(), rayTracingSource.size());
    const AStringView swBvh(swBvhSource.data(), swBvhSource.size());

    EXPECT_TRUE(ContainsText(rayTracingHeader, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_TRUE(ContainsText(rayTracingHeader, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_TRUE(ContainsText(rayTracing, "retainPreparedHybridHardwareMaterialContextFallbackUploads"));
    EXPECT_TRUE(ContainsText(rayTracing, "graph.copyUploadData("));
    EXPECT_TRUE(ContainsText(taskGraph, "hybridHardwareFallbackInstanceMaterialBlob"));
    EXPECT_TRUE(ContainsText(taskGraph, "hybridHardwareFallbackUploadsGraphOwned"));
    EXPECT_TRUE(ContainsText(taskGraph, "context.taskGraph.uploadBlobData("));
    EXPECT_TRUE(ContainsText(taskGraph, "frozen hybrid hardware material fallback cannot use graph-owned upload blobs"));
    EXPECT_TRUE(ContainsText(swBvh, "const void* const instanceMaterialData"));
    EXPECT_TRUE(ContainsText(swBvh, "graph-owned hybrid hardware fallback bytes differ from preflight"));
    EXPECT_TRUE(ContainsText(swBvh, "tryWriteBuffer(instanceMaterialBuffer, instanceMaterialData"));

    // Keep the stale-snapshot direct retry explicitly narrow. It remains the compatibility boundary only after the
    // immutable graph bytes fail validation, and it disables material consumers for this compiled frame.
    EXPECT_TRUE(ContainsText(rayTracing, "!restoredFrozenHardwareContext\n                    && buildSceneTlas(commandList, scratchArena, false)"));
    EXPECT_TRUE(ContainsText(rayTracing, "disableHybridMaterialConsumers();"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
