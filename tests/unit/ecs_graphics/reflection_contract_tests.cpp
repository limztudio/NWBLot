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


static constexpr AStringView s_IMPL = "impl";
static constexpr AStringView s_ECS_RENDER = "ecs_render";
static constexpr AStringView s_REFLECTION = "reflection";
static constexpr AStringView s_MESH = "mesh";
static constexpr AStringView s_ASSETS = "assets";
static constexpr AStringView s_GRAPHICS = "graphics";
static constexpr AStringView s_SURFACE_SLANGI = "surface.slangi";


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace EcsGraphicsTaskGraphContractTestDetail;
using EcsGraphicsTaskGraphContractTestDetail::AString;

TEST(EcsGraphics, ReflectionAuthoringSanitizesOnlyTheDedicatedReflectionFields){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ASSETS / s_GRAPHICS / s_MESH / s_SURFACE_SLANGI, source));
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
}

TEST(EcsGraphics, GlassReflectionUsesFiniteDielectricF0WithoutCoverageScaling){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ASSETS / s_GRAPHICS / s_MESH / s_SURFACE_SLANGI, source));
    const AStringView shader(source.data(), source.size());
    const usize begin = shader.find("NwbMeshSurface nwbMakeGlassSurface(");
    ASSERT_NE(begin, AStringView::npos);
    const usize end = shader.find("return surface;", begin);
    ASSERT_NE(end, AStringView::npos);
    const AStringView glass = shader.substr(begin, end - begin);
    EXPECT_TRUE(ContainsText(glass, "isfinite(float(refractionIor)) ? max(float(refractionIor), 1.0) : 1.0"));
    const usize reflectionWrite = glass.find("nwbSetMeshSurfaceReflection(");
    const usize coverageWrite = glass.find("surface.renderCoverage =");
    ASSERT_NE(reflectionWrite, AStringView::npos);
    ASSERT_NE(coverageWrite, AStringView::npos);
    EXPECT_LT(reflectionWrite, coverageWrite);
}


TEST(EcsGraphics, GlassReflectionAttachmentSurvivesAvboitClearAndIsNeutralWhenCaptureIsDisabled){
    TestArena testArena;
    const TestPath avboit = RepoRoot(testArena) / "impl" / s_ECS_RENDER / "avboit";
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
    snapshot.parameters.hardwareEnabled = 0u;
    EXPECT_FALSE(snapshot.hasHardwareWork());
}


TEST(EcsGraphics, ReflectionTemporalNoUpdatePreservesTheTaskAndSkipsNativeRecording){
    TestArena testArena;
    AString source;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ECS_RENDER / s_REFLECTION / "task_graph_postprocess.cpp", source));
    const AStringView graph(source.data(), source.size());
    const usize taskBegin = graph.find("struct TemporalTask{");
    ASSERT_NE(taskBegin, AStringView::npos);
    const usize taskEnd = graph.find("\nstruct SpatialTask{", taskBegin);
    ASSERT_NE(taskEnd, AStringView::npos);
    const AStringView task = graph.substr(taskBegin, taskEnd - taskBegin);
    const usize recordBegin = task.find("static bool Record(");
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
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / s_IMPL / s_ECS_RENDER / s_REFLECTION / "task_graph_reflection.cpp", source));
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
    const usize acceptedBegin = graph.find("static void Accepted(", dispatchBegin);
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

