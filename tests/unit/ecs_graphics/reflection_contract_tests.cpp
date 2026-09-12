// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_contract_test_helpers.h"

#include <impl/assets/graphics/mesh/runtime_constants.h>
#include <impl/assets/graphics/reflection/temporal_constants.h>
#include <impl/assets/graphics/reflection/spatial_constants.h>
#include <impl/ecs_render/reflection/reflection_system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_reflection_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;

TEST(EcsGraphics, ReflectionSurfaceContractUsesExplicitFieldsAndNeutralDefaults){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize structure = shader.find("struct NwbMeshSurface{");
    const usize constructor = shader.find("NwbMeshSurface nwbMakeMeshSurface(half3 baseColor, float3 normal, half param0, half param1)");
    ASSERT_NE(structure, AStringView::npos);
    ASSERT_NE(constructor, AStringView::npos);
    ASSERT_LT(structure, constructor);
    const AStringView fields = shader.substr(structure, constructor - structure);
    EXPECT_TRUE(ContainsText(fields, "half3  specularF0;"));
    EXPECT_TRUE(ContainsText(fields, "half   perceptualRoughness;"));

    const usize constructorEnd = shader.find("return surface;", constructor);
    ASSERT_NE(constructorEnd, AStringView::npos);
    const AStringView constructorBody = shader.substr(constructor, constructorEnd - constructor);
    EXPECT_TRUE(ContainsText(constructorBody, "surface.specularF0 = NWB_SURFACE_SPECULAR_F0_NONE;"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.perceptualRoughness = NWB_SURFACE_ROUGHNESS_MAX;"));
    EXPECT_TRUE(ContainsText(shader, "#define NWB_SURFACE_SPECULAR_F0_NONE half3(0.0h, 0.0h, 0.0h)"));
    EXPECT_TRUE(ContainsText(shader, "#define NWB_SURFACE_ROUGHNESS_MAX    half(1.0)"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.param0 = param0;"));
    EXPECT_TRUE(ContainsText(constructorBody, "surface.param1 = param1;"));
}

TEST(EcsGraphics, ReflectionAuthoringSanitizesOnlyTheDedicatedReflectionFields){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize begin = shader.find("half4 nwbPackMeshSurfaceReflection(");
    ASSERT_NE(begin, AStringView::npos);
    const usize end = shader.find("\n}", begin);
    ASSERT_NE(end, AStringView::npos);
    const AStringView authoring = shader.substr(begin, end - begin);
    EXPECT_TRUE(ContainsText(authoring, "all(isfinite(float3(specularF0))) ? saturate(specularF0) : NWB_SURFACE_SPECULAR_F0_NONE"));
    EXPECT_TRUE(ContainsText(authoring, "isfinite(float(perceptualRoughness))"));
    EXPECT_TRUE(ContainsText(authoring, "? saturate(perceptualRoughness) : NWB_SURFACE_ROUGHNESS_MAX"));
    EXPECT_FALSE(ContainsText(authoring, "param0"));
    EXPECT_FALSE(ContainsText(authoring, "param1"));
    EXPECT_FALSE(ContainsText(authoring, "refractionIor"));
    EXPECT_FALSE(ContainsText(authoring, "shadowAbsorptionTint"));
    EXPECT_FALSE(ContainsText(authoring, "renderCoverage"));
    EXPECT_TRUE(ContainsText(shader, "const half4 reflection = nwbPackMeshSurfaceReflection(specularF0, perceptualRoughness);"));
    EXPECT_TRUE(ContainsText(shader, "surface.specularF0 = reflection.rgb;"));
    EXPECT_TRUE(ContainsText(shader, "surface.perceptualRoughness = reflection.a;"));
}

TEST(EcsGraphics, GlassReflectionUsesFiniteDielectricF0WithoutCoverageScaling){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "surface.slangi", source));
    const AStringView shader(source.data(), source.size());
    const usize begin = shader.find("NwbMeshSurface nwbMakeGlassSurface(");
    ASSERT_NE(begin, AStringView::npos);
    const usize end = shader.find("return surface;", begin);
    ASSERT_NE(end, AStringView::npos);
    const AStringView glass = shader.substr(begin, end - begin);
    EXPECT_TRUE(ContainsText(glass, "isfinite(float(refractionIor)) ? max(float(refractionIor), 1.0) : 1.0"));
    EXPECT_TRUE(ContainsText(glass, "(validIor - 1.0) / (validIor + 1.0)"));
    EXPECT_TRUE(ContainsText(glass, "surface.refractionIor = half(validIor);"));
    EXPECT_TRUE(ContainsText(glass, "nwbSetMeshSurfaceReflection(surface, half3(dielectricRatio * dielectricRatio), half(0.0));"));
    const usize reflectionWrite = glass.find("nwbSetMeshSurfaceReflection(");
    const usize coverageWrite = glass.find("surface.renderCoverage =");
    ASSERT_NE(reflectionWrite, AStringView::npos);
    ASSERT_NE(coverageWrite, AStringView::npos);
    EXPECT_LT(reflectionWrite, coverageWrite);
}

TEST(EcsGraphics, SmokeReflectionInputsHaveIndependentTypedDefaults){
    TestArena testArena;
    const TestPath shaders = RepoRoot(testArena) / "tests" / "smoke" / "assets" / "shaders";
    AString bindingSource;
    AString hookSource;
    ASSERT_TRUE(ReadTextFile(shaders / "smoke_surface.bind", bindingSource));
    ASSERT_TRUE(ReadTextFile(shaders / "smoke_surface.surface", hookSource));
    const AStringView bindings(bindingSource.data(), bindingSource.size());
    const AStringView hook(hookSource.data(), hookSource.size());
    EXPECT_TRUE(ContainsText(bindings, "[default(\"half3(0.0h, 0.0h, 0.0h)\")]\n    half3 specular_f0;"));
    EXPECT_TRUE(ContainsText(bindings, "[default(\"half(1.0h)\")]\n    half perceptual_roughness;"));
    EXPECT_TRUE(ContainsText(hook, "nwbSetMeshSurfaceReflection(result, runtime.specular_f0, runtime.perceptual_roughness);"));
    EXPECT_FALSE(ContainsText(hook, "result.param0"));
    EXPECT_FALSE(ContainsText(hook, "result.param1"));
}

TEST(EcsGraphics, ReflectionGBufferAppendsItsOwnAttachmentWithoutRepackingBxdfParameters){
    EXPECT_EQ(NWB_MESH_GBUFFER_BASE_COLOR_LOCATION, 0);
    EXPECT_EQ(NWB_MESH_GBUFFER_NORMAL_LOCATION, 1);
    EXPECT_EQ(NWB_MESH_GBUFFER_WORLD_POSITION_LOCATION, 2);
    EXPECT_EQ(NWB_MESH_GBUFFER_SPECULAR_ROUGHNESS_LOCATION, 3);
    EXPECT_EQ(NWB_MESH_GBUFFER_TARGET_COUNT, 4);

    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "assets" / "graphics" / "mesh" / "gbuffer_io.slangi", source));
    const AStringView shader(source.data(), source.size());
    EXPECT_TRUE(ContainsText(shader, "float4 specularRoughness : SV_Target3;"));
    EXPECT_TRUE(ContainsText(shader, "output.specularRoughness = outGBufferSpecularRoughness;"));
    EXPECT_TRUE(ContainsText(shader, "outGBufferSpecularRoughness = specularRoughness;"));
    EXPECT_TRUE(ContainsText(shader, "outGBufferNormal = half4(nwbGBufferEncodeNormal(normal), half(param0));"));
    EXPECT_TRUE(ContainsText(shader, "outGBufferWorldPosition = half4(half3(worldPosition), param1);"));
}

TEST(EcsGraphics, OpaqueCsgAndGlassWritersUseOneSanitizedReflectionPacking){
    TestArena testArena;
    const TestPath graphics = RepoRoot(testArena) / "impl" / "assets" / "graphics";
    AString meshSource;
    AString capSource;
    AString captureSource;
    ASSERT_TRUE(ReadTextFile(graphics / "mesh" / "gbuffer_ps.slangi", meshSource));
    ASSERT_TRUE(ReadTextFile(graphics / "csg" / "interval_cap_fill_ps.slang", capSource));
    ASSERT_TRUE(ReadTextFile(graphics / "avboit" / "accumulate_ps_authoring.slangi", captureSource));
    constexpr AStringView pack = "nwbPackMeshSurfaceReflection(surface.specularF0, surface.perceptualRoughness)";
    EXPECT_TRUE(ContainsText(AStringView(meshSource.data(), meshSource.size()), pack));
    EXPECT_TRUE(ContainsText(AStringView(capSource.data(), capSource.size()), pack));
    const AStringView capture(captureSource.data(), captureSource.size());
    const usize captureBegin = capture.find("if(nwbAvboitRefractionCapture())");
    ASSERT_NE(captureBegin, AStringView::npos);
    const usize captureEnd = capture.find("return capture;", captureBegin);
    ASSERT_NE(captureEnd, AStringView::npos);
    const AStringView captureBody = capture.substr(captureBegin, captureEnd - captureBegin);
    EXPECT_TRUE(ContainsText(captureBody, pack));
    EXPECT_TRUE(ContainsText(captureBody, "capture.foregroundExtinction = nwbPackMeshSurfaceReflection"));
    EXPECT_TRUE(ContainsText(captureBody, "capture.accumExtinction.w = -1.0;"));
    EXPECT_TRUE(ContainsText(captureBody, "float(nwbMeshInstanceIndex()) + 1.0"));
}

TEST(EcsGraphics, GlassReflectionAttachmentSurvivesAvboitClearAndIsNeutralWhenCaptureIsDisabled){
    TestArena testArena;
    const TestPath avboit = RepoRoot(testArena) / "impl" / "ecs_render" / "avboit";
    AString targetSource;
    AString graphSource;
    ASSERT_TRUE(ReadTextFile(avboit / "avboit_targets.cpp", targetSource));
    ASSERT_TRUE(ReadTextFile(avboit / "task_graph_refraction_capture.cpp", graphSource));
    const AStringView targets(targetSource.data(), targetSource.size());
    const AStringView graph(graphSource.data(), graphSource.size());
    const usize framebufferBegin = targets.find("Core::FramebufferDesc refractionFramebufferDesc;");
    ASSERT_NE(framebufferBegin, AStringView::npos);
    const usize framebufferEnd = targets.find("device.createFramebuffer(refractionFramebufferDesc)", framebufferBegin);
    ASSERT_NE(framebufferEnd, AStringView::npos);
    const AStringView framebuffer = targets.substr(framebufferBegin, framebufferEnd - framebufferBegin);
    EXPECT_TRUE(ContainsText(framebuffer, ".addColorAttachment(avboitTargets.refractionSpecularRoughness.get(),"));
    EXPECT_FALSE(ContainsText(framebuffer, "foregroundAccumExtinction"));

    const usize clear = graph.find("render.refraction.clear.specular_roughness");
    const usize disabled = graph.find("if(!enabled)");
    ASSERT_NE(clear, AStringView::npos);
    ASSERT_NE(disabled, AStringView::npos);
    EXPECT_LT(clear, disabled);
    EXPECT_TRUE(ContainsText(graph, "specularRoughness, false, Core::Color(0.f, 0.f, 0.f, 1.f)"));
    EXPECT_TRUE(ContainsText(graph, "ReadUse(specularRoughness)"));
    EXPECT_TRUE(ContainsText(graph, "ReadWriteUse(specularRoughness, Core::ResourceStates::RenderTarget)"));
    EXPECT_FALSE(ContainsText(graph, "foregroundAccumExtinction"));
}

TEST(EcsGraphics, ReflectionHardwareWorkRequiresAnEnabledRouteAndPositiveRayBudget){
    NWB::Impl::ReflectionFrameSnapshot snapshot;
    snapshot.parameters.hardwareEnabled = 1u;
    snapshot.parameters.maxHardwareRays = 0u;
    EXPECT_FALSE(snapshot.hasHardwareWork());
    snapshot.parameters.maxHardwareRays = 1u;
    EXPECT_TRUE(snapshot.hasHardwareWork());
    snapshot.parameters.maxHardwareRays = 64u;
    EXPECT_TRUE(snapshot.hasHardwareWork());
    snapshot.parameters.hardwareEnabled = 0u;
    EXPECT_FALSE(snapshot.hasHardwareWork());
}

TEST(EcsGraphics, ReflectionFrameSelectorsMatchTwelveStd140LanesAndSeparateCounterBytes){
    using Parameters = NWB::Impl::ReflectionFrameParameters;
    EXPECT_EQ(sizeof(Parameters), 192u);
    EXPECT_EQ(offsetof(Parameters, width), 0u);
    EXPECT_EQ(offsetof(Parameters, height), 4u);
    EXPECT_EQ(offsetof(Parameters, traceMode), 8u);
    EXPECT_EQ(offsetof(Parameters, hardwareEnabled), 12u);
    EXPECT_EQ(offsetof(Parameters, opaqueSpecularSlot), 16u);
    EXPECT_EQ(offsetof(Parameters, opaqueRadianceSlot), 32u);
    EXPECT_EQ(offsetof(Parameters, argsSlot), 48u);
    EXPECT_EQ(offsetof(Parameters, deferredResourcesSlot), 64u);
    EXPECT_EQ(offsetof(Parameters, depthPyramidSlot), 80u);
    EXPECT_EQ(offsetof(Parameters, sampleIndex), 60u);
    EXPECT_EQ(offsetof(Parameters, samplingSeed), 96u);
    EXPECT_EQ(offsetof(Parameters, sampleBaseX), 100u);
    EXPECT_EQ(offsetof(Parameters, sampleBaseY), 104u);
    EXPECT_EQ(offsetof(Parameters, maxOpticalQueries), 108u);
    EXPECT_EQ(offsetof(Parameters, feedbackReadSlot), 112u);
    EXPECT_EQ(offsetof(Parameters, feedbackWriteSlot), 116u);
    EXPECT_EQ(offsetof(Parameters, feedbackFlags), 120u);
    EXPECT_EQ(offsetof(Parameters, feedbackProbeIndex), 124u);
    EXPECT_EQ(offsetof(Parameters, maxRayDistance), 128u);
    EXPECT_EQ(offsetof(Parameters, environmentTopR), 144u);
    EXPECT_EQ(offsetof(Parameters, environmentBottomR), 160u);
    EXPECT_EQ(offsetof(Parameters, screenThickness), 176u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_MEDIUM_OVERFLOW_PATHS + sizeof(u32), 64u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_POTENTIAL_RECEIVERS, 64u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_SCREEN_RETURNS, 68u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_FEEDBACK_BYPASSED_PIXELS, 72u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_FEEDBACK_PROBE_TILES, 76u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_LOW, 80u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_SCREEN_ITERATIONS_HIGH, 84u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_SCREEN_LIMIT_MISSES, 88u);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_RESERVED + sizeof(u32), NWB_REFLECTION_COUNTER_SIZE);
    EXPECT_EQ(NWB_REFLECTION_COUNTER_SIZE, 96u);
    EXPECT_EQ(static_cast<u32>(NWB::Impl::ReflectionTraceMode::Disabled), NWB_REFLECTION_MODE_DISABLED);
    EXPECT_EQ(static_cast<u32>(NWB::Impl::ReflectionTraceMode::ScreenSpace), NWB_REFLECTION_MODE_SCREEN);
    EXPECT_EQ(static_cast<u32>(NWB::Impl::ReflectionTraceMode::Hardware), NWB_REFLECTION_MODE_HARDWARE);
    EXPECT_EQ(static_cast<u32>(NWB::Impl::ReflectionTraceMode::Hybrid), NWB_REFLECTION_MODE_HYBRID);
}

TEST(EcsGraphics, ReflectionPostprocessSelectorsKeepAcceptedSamplingSeparateFromHistoryCount){
    struct TemporalParameters{
#define NWB_REFLECTION_TEST_POST_FIELD(name) u32 name = 0u;
        NWB_REFLECTION_TEMPORAL_UINT_FIELDS(NWB_REFLECTION_TEST_POST_FIELD)
#undef NWB_REFLECTION_TEST_POST_FIELD
    };
    EXPECT_EQ(sizeof(TemporalParameters), NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES);
    EXPECT_EQ(sizeof(TemporalParameters), 48u);
    EXPECT_EQ(offsetof(TemporalParameters, previousSampleCount), 20u);
    EXPECT_EQ(offsetof(TemporalParameters, historyValid), 28u);
    EXPECT_EQ(offsetof(TemporalParameters, sampleIndex), 32u);
    EXPECT_EQ(offsetof(TemporalParameters, samplingSeed), 36u);
    EXPECT_LE(NWB_REFLECTION_SPATIAL_PUSH_CONSTANT_BYTES, NWB_REFLECTION_TEMPORAL_PUSH_CONSTANT_BYTES);
}

TEST(EcsGraphics, ReflectionTemporalNoUpdatePreservesTheTaskAndSkipsNativeRecording){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "reflection" / "task_graph_postprocess.cpp", source));
    const AStringView graph(source.data(), source.size());
    const usize taskBegin = graph.find("struct TemporalTask{");
    ASSERT_NE(taskBegin, AStringView::npos);
    const usize taskEnd = graph.find("\nstruct SpatialTask{", taskBegin);
    ASSERT_NE(taskEnd, AStringView::npos);
    const AStringView task = graph.substr(taskBegin, taskEnd - taskBegin);
    const usize recordBegin = task.find("static bool record(");
    ASSERT_NE(recordBegin, AStringView::npos);
    const usize recordEnd = task.find("\n    }", recordBegin);
    ASSERT_NE(recordEnd, AStringView::npos);
    const AStringView record = task.substr(recordBegin, recordEnd - recordBegin);
    const usize resolve = record.find("ResolveReflectionHistoryOutcome(payload.snapshot.history, ready)");
    const usize skip = record.find("if(!outcome.reused || payload.snapshot.history.settings.temporalMaxSamples <= 1u)");
    ASSERT_NE(resolve, AStringView::npos);
    ASSERT_NE(skip, AStringView::npos);
    ASSERT_LT(resolve, skip);
    const usize skipReturn = record.find("return true;", skip);
    ASSERT_NE(skipReturn, AStringView::npos);
    const AStringView commands[] = {
        "commandList.endRenderPass();", "commandList.setComputeState(", ".bindCompute(",
        "commandList.setPushConstants(", "Core::GpuTimingMeasure timing(", "commandList.dispatch(",
    };
    for(const AStringView command : commands){
        const usize commandOffset = record.find(command);
        ASSERT_NE(commandOffset, AStringView::npos);
        EXPECT_LT(skipReturn, commandOffset);
    }
    EXPECT_TRUE(ContainsText(task, "payload.reservation.accept(token, payload.hardwareEnabled && payload.hardwarePreparationReady && *payload.hardwarePreparationReady);"));
    EXPECT_TRUE(ContainsText(task, "payload.reservation.discard();"));
    EXPECT_FALSE(ContainsText(record, "payload.reservation.accept("));
    EXPECT_FALSE(ContainsText(record, "payload.reservation.discard("));

    const usize declarationBegin = graph.find("Core::GpuTaskId DeclareReflectionPostprocessTasks(");
    ASSERT_NE(declarationBegin, AStringView::npos);
    const usize declarationEnd = graph.find("\n}", declarationBegin);
    ASSERT_NE(declarationEnd, AStringView::npos);
    const AStringView declaration = graph.substr(declarationBegin, declarationEnd - declarationBegin);
    EXPECT_TRUE(ContainsText(declaration, "ReflectionHistoryReservation reservation(snapshot.control, snapshot.history);"));
    EXPECT_TRUE(ContainsText(declaration, "ReadWriteUse(result.opaqueRadiance, Core::ResourceStates::UnorderedAccess)"));
    EXPECT_TRUE(ContainsText(declaration, "dependency = graph.addTask<TemporalTask>("));
    EXPECT_TRUE(ContainsText(declaration, "graphics, snapshot, Move(reservation),"));
}

TEST(EcsGraphics, ReflectionUnavailableHardwareDisablesQueueingAndSkipsTlasDispatch){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "reflection" / "task_graph_reflection.cpp", source));
    const AStringView graph(source.data(), source.size());
    const usize uploadBegin = graph.find("struct UploadParametersTask{");
    const usize uploadEnd = graph.find("namespace DispatchStage{", uploadBegin);
    ASSERT_NE(uploadBegin, AStringView::npos);
    ASSERT_NE(uploadEnd, AStringView::npos);
    const AStringView upload = graph.substr(uploadBegin, uploadEnd - uploadBegin);
    EXPECT_TRUE(ContainsText(upload, "if(!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady)"));
    EXPECT_TRUE(ContainsText(upload, "parameters.hardwareEnabled = 0u;"));
    EXPECT_LT(upload.find("parameters.hardwareEnabled = 0u;"), upload.find("commandList.writeBuffer("));

    const usize dispatchBegin = graph.find("struct DispatchTask{");
    const usize acceptedBegin = graph.find("static void accepted(", dispatchBegin);
    ASSERT_NE(dispatchBegin, AStringView::npos);
    ASSERT_NE(acceptedBegin, AStringView::npos);
    const AStringView dispatch = graph.substr(dispatchBegin, acceptedBegin - dispatchBegin);
    EXPECT_TRUE(ContainsText(dispatch, "if(hardware && (!payload.hardwarePreparationReady || !*payload.hardwarePreparationReady))\n            return true;"));
    EXPECT_LT(dispatch.find("return true;"), dispatch.find(".bindCompute("));
    EXPECT_FALSE(ContainsText(dispatch, "createComputePipeline"));
    EXPECT_FALSE(ContainsText(dispatch, "heap.allocate"));
    EXPECT_TRUE(ContainsText(graph, "ReadUse(result.opaqueRadiance), ReadUse(result.glassRadiance)"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

