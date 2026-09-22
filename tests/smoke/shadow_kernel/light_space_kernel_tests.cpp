// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "light_space_kernel_fixture.h"

#include <impl/assets_csg/cook.h>
#include <impl/assets_shader/cook.h>

#include <global/filesystem.h>
#include <global/scope_exit.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void LightSpaceKernelTest::SetUpTestSuite(){
    s_logger.emplace();
    s_loggerGuard.emplace(*s_logger);
    s_scope = MakeUnique<HeadlessGraphicsScope>();
    ASSERT_TRUE(s_scope->graphics().setHardwareRayTracingPolicy(HardwareRayTracingPolicy::Disabled));
    if(!s_scope->initialize()){
        GTEST_SKIP() << "Light-space regression requires a validation-backed descriptor-buffer device.";
        return;
    }
    s_validationBackedDeviceInitialized = true;
    EXPECT_FALSE(device().queryFeatureSupport(Feature::RayQuery));
    EXPECT_FALSE(device().queryFeatureSupport(Feature::RayTracingPipeline));
    EXPECT_FALSE(device().queryFeatureSupport(Feature::RayTracingAccelStruct));
    EXPECT_EQ(device().getDescriptorHeap().lifecycleStatistics().accelStructCapacity, 0u);
    EXPECT_FALSE(device().getDescriptorHeap().hasAccelStructLayout());
}

bool LightSpaceKernelTest::loadPrograms(Alloc::ScratchArena& scratchArena, LightSpaceKernel::Programs& programs){
    auto& graphicsDevice = device();
    auto& heap = graphicsDevice.getDescriptorHeap();
    BindingLayoutDesc layoutDesc(arena());
    layoutDesc.setVisibility(ShaderType::All).addItem(BindingLayoutItem::PushConstants(0u, sizeof(LightSpaceKernel::Push)));
    programs.layout = graphicsDevice.createBindingLayout(layoutDesc);
    if(!programs.layout)
        return false;
    const Path sourceRoot(arena(), NWB_SHADOW_KERNEL_SOURCE_ROOT);
    const Path outputRoot(arena(), NWB_SHADOW_KERNEL_OUTPUT_ROOT);
    const Path testRoot = sourceRoot / "tests/smoke/shadow_kernel/assets";
    const Path engineRoot = sourceRoot / "impl/assets/graphics";
    const Path shadowRoot = engineRoot / "shadow";
    ErrorCode error;
    if(!CreateDirectories(outputRoot, error) && error)
        return false;
    const Impl::AssetsCsgCook::CsgShapeCookEntryVector noShapes(arena());
    Path generatedRoot(arena());
    if(!Impl::AssetsCsgCook::EmitCsgShapeModuleIncludes(outputRoot, "light_space", noShapes, generatedRoot, scratchArena))
        return false;
    Impl::ShaderCook cook(arena());
    Impl::ShaderCook::CookVector<Path> includes(arena());
    includes.push_back(testRoot);
    includes.push_back(engineRoot);
    includes.push_back(generatedRoot);
    const Path sources[] = { shadowRoot / "light_space_view_cs.slang", shadowRoot / "light_space_capture_vs.slang",
        shadowRoot / "light_space_capture_ps.slang", shadowRoot / "light_space_resolve_cs.slang",
        shadowRoot / "light_space_resolve_cs.slang", testRoot / "light_space_observe_cs.slang",
        testRoot / "light_space_observe_cs.slang", shadowRoot / "light_space_shade_cs.slang",
        shadowRoot / "light_space_fallback_cs.slang", shadowRoot / "light_space_fallback_cs.slang",
        testRoot / "light_space_poison_neighbor_cs.slang", testRoot / "light_space_cull_cs.slang" };
    const Path metadata[] = { shadowRoot / "light_space_view_cs.nwb", shadowRoot / "light_space_capture_vs.nwb",
        shadowRoot / "light_space_capture_ps.nwb", shadowRoot / "light_space_resolve_cs.nwb",
        shadowRoot / "light_space_resolve_cs.nwb", testRoot / "light_space_observe_cs.nwb",
        testRoot / "light_space_observe_cs.nwb", shadowRoot / "light_space_shade_cs.nwb",
        shadowRoot / "light_space_fallback_cs.nwb", shadowRoot / "light_space_fallback_cs.nwb",
        testRoot / "light_space_poison_neighbor_cs.nwb", testRoot / "light_space_cull_cs.nwb" };
    const AStringView outputNames[] = { "light_space_view.spv", "light_space_capture_vertex.spv", "light_space_capture_pixel.spv",
        "light_space_resolve_opaque.spv", "light_space_resolve_transparent.spv", "light_space_observe_opaque.spv",
        "light_space_observe_transparent.spv", "light_space_shade.spv", "light_space_fallback_opaque.spv",
        "light_space_fallback_transparent.spv", "light_space_poison_neighbor.spv", "light_space_cull.spv" };
    for(u32 index = 0u; index < LengthOf(sources); ++index){
        Impl::ShaderCook::ShaderEntry entry(arena());
        Impl::ShaderCook::CookVector<u8> bytes(arena());
        Impl::ShaderCook::CookVector<Path> dependencies(arena());
        const bool variant = index >= 3u && index <= 9u && index != 7u;
        const bool transparent = index == 4u || index == 6u || index == 9u;
        const Impl::ShaderCook::ShaderMacroDefinition definition{ "NWB_LIGHT_SPACE_OCCLUDER", transparent ? "1" : "0" };
        const bool cooked = [&]{
            if(!cook.parseShaderMeta(metadata[index], entry, scratchArena)
                || !cook.gatherShaderDependencies(sources[index], includes, dependencies, scratchArena))
                return false;
            const Impl::ShaderCook::ShaderCompilerRequest request{
                .shaderName = "tests/shadow_kernel/light_space",
                .stage = entry.stage.view(),
                .targetProfile = entry.targetProfile.view(),
                .entryPoint = AStringView(entry.entryPoint.data(), entry.entryPoint.size()),
                .variantName = variant ? (transparent ? "NWB_LIGHT_SPACE_OCCLUDER=1" : "NWB_LIGHT_SPACE_OCCLUDER=0") : "default",
                .defines = variant ? &definition : nullptr,
                .includeDirectories = includes,
                .dependencies = dependencies,
                .sourcePath = sources[index],
                .outputPath = outputRoot / outputNames[index],
                .defineCount = variant ? 1u : 0u,
                .optimizationLevel = entry.optimizationLevel,
            };
            return cook.compileVariant(request, bytes) && !bytes.empty();
        }();
        if(!cooked){
            s_logger->emitErrorsToStderr();
            return false;
        }
        ShaderDesc desc(arena());
        desc
            .setShaderType(index == 1u ? ShaderType::Vertex : index == 2u ? ShaderType::Pixel : ShaderType::Compute)
            .setEntryName(AStringView(entry.entryPoint.data(), entry.entryPoint.size()))
        ;
        const ShaderHandle shader = graphicsDevice.createShader(desc, bytes.data(), bytes.size());
        if(!shader)
            return false;
        if(index == 1u){ programs.vertex = shader; continue; }
        if(index == 2u){ programs.pixel = shader; continue; }
        ComputePipelineDesc pipelineDesc;
        pipelineDesc
            .setComputeShader(shader)
            .addBindingLayout(programs.layout)
            .addBindingLayout(heap.getResourceLayout())
            .addBindingLayout(heap.getSamplerLayout())
        ;
        ComputePipelineHandle pipeline = graphicsDevice.createComputePipeline(pipelineDesc);
        if(!pipeline)
            return false;
        if(index == 0u)
            programs.view = pipeline;
        else if(index < 5u)
            programs.resolve[index - 3u] = pipeline;
        else if(index < 7u)
            programs.observe[index - 5u] = pipeline;
        else if(index == 7u)
            programs.shade = pipeline;
        else if(index < 10u)
            programs.fallback[index - 8u] = pipeline;
        else if(index == 10u)
            programs.poison = pipeline;
        else
            programs.cull = pipeline;
    }
    return true;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LightSpaceKernelTest, CapturesAndResolvesOpaqueAndOverlappingVolumesWithSoftwareFallback){
    using namespace LightSpaceKernel;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    ScopeExit report([&]()noexcept{
        if(::testing::Test::HasFailure()){
            s_logger->emitErrorsToStderr();
            s_logger->emitMessagesContainingToStderr(NWB_TEXT("Vulkan debug: [severity=error"));
        }
    });
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_cook"));
    Programs programs;
    ASSERT_TRUE(loadPrograms(scratchArena, programs));
    const Case cases[] = {
        { .label = "directional empty", .count = 0u },
        { .label = "directional opaque", .opaque = true },
        { .label = "directional one volume" },
        { .label = "directional off-axis Y orientation", .asymmetric = true },
        { .label = "point off-axis Y orientation", .point = true, .asymmetric = true },
        { .label = "directional two tinted overlaps", .count = 2u },
        { .label = "directional four tinted overlaps", .count = 4u },
        { .label = "directional per-object thickness", .count = 4u, .varyingThickness = true },
        { .label = "directional thin interface", .count = 2u, .thickness = 0.001f },
        { .label = "directional overflow", .count = NWB_LIGHT_SPACE_EVENTS_PER_TEXEL / 2u + 1u },
        { .label = "directional invalid view", .count = 2u, .invalidView = true },
        { .label = "overflowing directional fit", .count = 2u, .invalidFit = true },
        { .label = "closed receiver inside directional volume", .closed = true, .inside = true },
        { .label = "unspecified receiver inside keeps singleton policy", .inside = true },
        { .label = "closed receiver inside point volume", .point = true, .closed = true, .inside = true },
        { .label = "point near-plane caster falls back", .point = true, .nearClip = true },
        { .label = "directional invalid event", .count = 2u, .corruptEvent = true },
        { .label = "background preserves lit output", .count = 2u, .background = true },
        { .label = "single sample", .count = 2u, .sampleCount = 1u },
        { .label = "point positive X", .count = 2u, .face = 0u, .point = true },
        { .label = "point negative X", .count = 2u, .face = 1u, .point = true },
        { .label = "point positive Y", .count = 2u, .face = 2u, .point = true },
        { .label = "point negative Y", .count = 2u, .face = 3u, .point = true },
        { .label = "point positive Z", .count = 2u, .face = 4u, .point = true },
        { .label = "point negative Z", .count = 2u, .face = 5u, .point = true },
        { .label = "point opaque", .point = true, .opaque = true },
        { .label = "point four volumes", .count = 4u, .point = true },
        { .label = "point overflow", .count = NWB_LIGHT_SPACE_EVENTS_PER_TEXEL / 2u + 1u, .point = true },
        { .label = "point missing selected face", .count = 2u, .point = true, .missingFace = true },
        { .label = "point near X-Z seam X side", .count = 2u, .seam = -1, .point = true },
        { .label = "point near X-Z seam Z side", .count = 2u, .seam = 1, .point = true },
    };
    for(const Case& testCase : cases){
        runCase(testCase, programs);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
    }
    EXPECT_EQ(s_logger->errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LightSpaceKernelTest, FiniteSourcesPreserveInteriorVisibilityAndReprojectAcrossPointFaces){
    using namespace LightSpaceKernel;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    ScopeExit report([&]()noexcept{
        if(::testing::Test::HasFailure()){
            s_logger->emitErrorsToStderr();
            s_logger->emitMessagesContainingToStderr(NWB_TEXT("Vulkan debug: [severity=error"));
        }
    });
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_soft_cook"));
    Programs programs;
    ASSERT_TRUE(loadPrograms(scratchArena, programs));
    const Case interiors[] = {
        { .label = "finite directional all lit", .count = 0u, .sourceSize = 0.1f, .softExpectation = SoftExpectation::Lit },
        { .label = "finite point all lit", .count = 0u, .sourceSize = 0.25f, .point = true, .softExpectation = SoftExpectation::Lit },
        { .label = "finite directional fully blocked", .sourceSize = 0.1f, .opaque = true, .softExpectation = SoftExpectation::Blocked },
        { .label = "finite point fully blocked", .sourceSize = 0.25f, .point = true, .opaque = true, .softExpectation = SoftExpectation::Blocked },
        { .label = "finite directional tinted interior", .count = 2u, .sourceSize = 0.1f, .softExpectation = SoftExpectation::Interior },
        { .label = "finite point tinted interior", .count = 2u, .sourceSize = 0.2f, .point = true, .softExpectation = SoftExpectation::Interior },
    };
    for(const Case& testCase : interiors){
        runCase(testCase, programs);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
    }
    Quality directionalEdge;
    Quality pointEdge;
    Quality seam;
    Quality missingNeighbor;
    for(u32 frame = 0u; frame < 8u; ++frame){
        const Case cases[] = {
            { .label = "finite directional edge", .sourceSize = 0.15f, .receiverX = 1.0f, .frameIndex = frame, .opaque = true, .softExpectation = SoftExpectation::Edge },
            { .label = "finite point edge", .sourceSize = 0.3f, .receiverX = 4.0f, .frameIndex = frame, .point = true, .opaque = true, .softExpectation = SoftExpectation::Edge },
            { .label = "finite point seam reprojects", .count = 2u, .seam = 1, .sourceSize = 0.5f, .frameIndex = frame, .point = true, .softExpectation = SoftExpectation::Seam },
            { .label = "finite point seam missing neighbor falls back", .count = 2u, .seam = 1, .sourceSize = 0.5f, .frameIndex = frame, .point = true, .softExpectation = SoftExpectation::MissingNeighbor },
        };
        Quality* const outputs[] = { &directionalEdge, &pointEdge, &seam, &missingNeighbor };
        for(u32 index = 0u; index < LengthOf(cases); ++index){
            runCase(cases[index], programs, outputs[index]);
            ASSERT_FALSE(::testing::Test::HasFatalFailure());
        }
    }
    EXPECT_TRUE(directionalEdge.penumbra);
    EXPECT_TRUE(directionalEdge.geometricPenumbra);
    EXPECT_TRUE(pointEdge.penumbra);
    EXPECT_TRUE(pointEdge.geometricPenumbra);
    EXPECT_EQ(seam.faceMask & ((1u << 0u) | (1u << 4u)), (1u << 0u) | (1u << 4u));
    EXPECT_EQ(seam.faceMask & ~((1u << 0u) | (1u << 4u)), 0u);
    EXPECT_EQ(seam.fallbackMask, 0u);
    EXPECT_EQ(missingNeighbor.fallbackMask, 7u);
    EXPECT_EQ(s_logger->errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST_F(LightSpaceKernelTest, ObliqueReceiversStayLitAndPreserveNearbyContacts){
    using namespace LightSpaceKernel;
    const Common::LoggerRegistrationGuard diagnostics(*s_logger, Common::LoggerBreakPolicy::BreakOnFatal);
    ScopeExit report([&]()noexcept{
        if(::testing::Test::HasFailure()){
            s_logger->emitErrorsToStderr();
            s_logger->emitMessagesContainingToStderr(NWB_TEXT("Vulkan debug: [severity=error"));
        }
    });
    Alloc::ScratchArena scratchArena(Name("tests/shadow_kernel/light_space_receiver_cook"));
    Programs programs;
    ASSERT_TRUE(loadPrograms(scratchArena, programs));
    const Case cases[] = {
        { .label = "half directional oblique self receiver", .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "half directional reverse slope self receiver", .receiverSlopeX = -0.7f, .receiverSlopeY = 0.45f, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "half point oblique self receiver", .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "half point reverse slope self receiver", .receiverSlopeX = -0.7f, .receiverSlopeY = 0.45f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "directional contact gap 0.03", .count = 2u, .receiverGap = 0.03f, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "directional contact gap 0.006", .count = 2u, .receiverGap = 0.006f, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "point contact gap 0.03", .count = 2u, .receiverGap = 0.03f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "point contact gap 0.006", .count = 2u, .receiverGap = 0.006f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "directional plane below receiver stays lit", .count = 2u, .receiverGap = -0.006f, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "point plane below receiver stays lit", .count = 2u, .receiverGap = -0.006f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "finite directional oblique self receiver", .sourceSize = 0.04f, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "finite point oblique self receiver", .sourceSize = 0.05f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "finite directional close contact", .count = 2u, .sourceSize = 0.04f, .receiverGap = 0.006f, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "finite point close contact", .count = 2u, .sourceSize = 0.05f, .receiverGap = 0.006f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Blocked },
        { .label = "finite point lower plane stays lit", .count = 2u, .sourceSize = 0.05f, .receiverGap = -0.006f, .point = true, .opaque = true, .receiverExpectation = ReceiverExpectation::Lit },
        { .label = "nonfinite neighboring transparent blocker invokes software", .count = 2u, .sourceSize = 0.12f, .corruptNeighbor = true },
        { .label = "singular directional receiver invokes software", .receiverSlopeX = 4096.0f, .opaque = true, .receiverExpectation = ReceiverExpectation::Singular, .receiverHalf = false },
    };
    for(const Case& testCase : cases){
        runCase(testCase, programs);
        ASSERT_FALSE(::testing::Test::HasFatalFailure());
    }
    EXPECT_EQ(s_logger->errorCount(), 0u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

