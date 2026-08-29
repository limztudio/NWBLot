// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/operations.h>
#include <global/filesystem/path.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_renderer_subsystem_boundary_contract_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AString = NWB::Tests::TestAString;
using TestPath = ::Path<NWB::Core::Alloc::GlobalArena>;

struct RendererSubsystemBoundaryContractTestArenaTag{};
using TestArena = NWB::Tests::TestArena<RendererSubsystemBoundaryContractTestArenaTag>;


static bool ContainsText(const AStringView text, const AStringView expected){
    return text.find(expected) != AStringView::npos;
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


static AString CompactSource(const AStringView source){
    AString compact;
    compact.reserve(source.size());
    for(const char ch : source){
        if(ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' && ch != '\f' && ch != '\v')
            compact += ch;
    }
    return compact;
}


static bool IsOptionalParameterName(const AStringView text){
    if(text.empty())
        return true;

    const auto isLetterOrUnderscore = [](const char ch){
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    };
    if(!isLetterOrUnderscore(text.front()))
        return false;
    for(const char ch : text){
        if(!isLetterOrUnderscore(ch) && !(ch >= '0' && ch <= '9'))
            return false;
    }
    return true;
}


static bool ConstructorParameterTypesMatch(
    const AStringView compactHeader,
    const AStringView constructorOpen,
    const InitializerList<StringView> expectedTypes
){
    if(CountText(compactHeader, constructorOpen) != 1u)
        return false;

    const usize parameterListBegin = compactHeader.find(constructorOpen) + constructorOpen.size();
    const usize parameterListEnd = compactHeader.find(");", parameterListBegin);
    if(parameterListEnd == AStringView::npos)
        return false;

    usize parameterBegin = parameterListBegin;
    usize expectedTypeIndex = 0u;
    for(const StringView expectedTypeStorage : expectedTypes){
        const bool lastParameter = expectedTypeIndex + 1u == expectedTypes.size();
        const usize comma = compactHeader.find(',', parameterBegin);
        if((lastParameter && comma < parameterListEnd) || (!lastParameter && (comma == AStringView::npos || comma >= parameterListEnd)))
            return false;

        const usize parameterEnd = lastParameter ? parameterListEnd : comma;
        const AStringView parameter = compactHeader.substr(parameterBegin, parameterEnd - parameterBegin);
        const AStringView expectedType(expectedTypeStorage.data(), expectedTypeStorage.size());
        if(parameter.size() < expectedType.size() || parameter.substr(0u, expectedType.size()) != expectedType)
            return false;
        if(!IsOptionalParameterName(parameter.substr(expectedType.size())))
            return false;

        parameterBegin = parameterEnd + 1u;
        ++expectedTypeIndex;
    }
    return parameterListBegin != parameterListEnd && expectedTypeIndex == expectedTypes.size();
}


static TestPath RepoRoot(TestArena& testArena){
    return TestPath(testArena.arena, __FILE__).parent_path().parent_path().parent_path().parent_path().lexically_normal();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(EcsGraphics, ShaderSystemOwnsOnlyItsNarrowConstructionBoundary){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "shader" / "shader_system.h", headerSource));
    const AStringView header(headerSource.data(), headerSource.size());
    const AString compactHeaderStorage = CompactSource(header);
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(header, "RendererFramePipeline"));
    EXPECT_FALSE(ContainsText(header, "RendererFramePipelineSubsystemBase"));
    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactHeader,
        "RendererShaderSystem(",
        {
            "Core::Graphics&",
            "Core::Assets::AssetManager&",
            "RendererShaderPathResolveCallback&",
        }
    ));
}


TEST(EcsGraphics, MeshSystemOwnsOnlyItsNarrowConstructionBoundary){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "mesh" / "mesh_system.h", headerSource));
    const AStringView header(headerSource.data(), headerSource.size());
    const AString compactHeaderStorage = CompactSource(header);
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(header, "RendererFramePipeline"));
    EXPECT_FALSE(ContainsText(header, "RendererFramePipelineSubsystemBase"));
    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactHeader,
        "RendererMeshSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::ECS::World&",
            "Core::Graphics&",
            "Core::Assets::AssetManager&",
            "RendererMeshState&",
            "RendererDrawState&",
        }
    ));
}


TEST(EcsGraphics, MaterialSystemUsesMeshDomainLookupWithoutMeshStatePrivilege){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString materialHeaderSource;
    AString materialResourcesSource;
    AString rendererStateSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateSource));

    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AString compactRendererStateStorage = CompactSource(AStringView(rendererStateSource.data(), rendererStateSource.size()));
    const AStringView compactRendererState(compactRendererStateStorage.data(), compactRendererStateStorage.size());

    EXPECT_FALSE(ContainsText(materialHeader, "RendererMeshState"));
    EXPECT_FALSE(ContainsText(materialResources, "m_meshState"));
    EXPECT_TRUE(ContainsText(materialResources, "m_meshSystem.findMeshResources(drawItem.meshKey, mesh)"));
    EXPECT_FALSE(ContainsText(compactRendererState, "friendclassRendererMaterialSystem;friendclassRendererRayTracingSystem;"));
}


TEST(EcsGraphics, MaterialDomainOwnsTheSharedMaterialPassLayout){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString materialHeaderSource;
    AString materialPipelineSource;
    AString materialResourcesSource;
    AString avboitResourcesSource;
    AString rendererStateSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pipeline.cpp", materialPipelineSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_resources.cpp", avboitResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateSource));

    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialPipeline(materialPipelineSource.data(), materialPipelineSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AStringView avboitResources(avboitResourcesSource.data(), avboitResourcesSource.size());
    const AString compactRendererStateStorage = CompactSource(AStringView(rendererStateSource.data(), rendererStateSource.size()));
    const AStringView compactRendererState(compactRendererStateStorage.data(), compactRendererStateStorage.size());
    const usize avboitStateBegin = compactRendererState.find("classRendererAvboitStatefinal:NoCopy{");
    ASSERT_NE(avboitStateBegin, AStringView::npos);
    const usize avboitStateEnd = compactRendererState.find("};", avboitStateBegin);
    ASSERT_NE(avboitStateEnd, AStringView::npos);
    const AStringView avboitState = compactRendererState.substr(avboitStateBegin, avboitStateEnd - avboitStateBegin);

    EXPECT_FALSE(ContainsText(materialHeader, "RendererAvboitState"));
    EXPECT_FALSE(ContainsText(materialPipeline, "m_avboitState"));
    EXPECT_TRUE(ContainsText(materialResources, "m_materialState.m_materialPassBindingLayout"));
    EXPECT_TRUE(ContainsText(avboitResources, "m_materialSystem.prepareMaterialPassBindingLayout(materialPassBindingLayout)"));
    EXPECT_FALSE(ContainsText(avboitResources, "m_avboitState.m_emptyBindingLayout"));
    EXPECT_FALSE(ContainsText(avboitState, "friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(avboitState, "m_emptyBindingLayout"));
}


TEST(EcsGraphics, AvboitDoesNotDependOnDeferredPrivateState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString avboitHeaderSource;
    AString avboitSystemSource;
    AString avboitResourcesSource;
    AString rendererStateSource;
    AString rootResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.cpp", avboitSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_resources.cpp", avboitResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rootResourcesSource));

    const AStringView avboitHeader(avboitHeaderSource.data(), avboitHeaderSource.size());
    const AStringView avboitSystem(avboitSystemSource.data(), avboitSystemSource.size());
    const AStringView avboitResources(avboitResourcesSource.data(), avboitResourcesSource.size());
    const AString compactAvboitHeaderStorage = CompactSource(avboitHeader);
    const AStringView compactAvboitHeader(compactAvboitHeaderStorage.data(), compactAvboitHeaderStorage.size());
    const AString compactRendererStateStorage = CompactSource(AStringView(rendererStateSource.data(), rendererStateSource.size()));
    const AStringView compactRendererState(compactRendererStateStorage.data(), compactRendererStateStorage.size());
    const AStringView rootResources(rootResourcesSource.data(), rootResourcesSource.size());

    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactAvboitHeader,
        "RendererAvboitSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::Graphics&",
            "RendererAvboitState&",
            "RendererShaderSystem&",
            "RendererMaterialSystem&",
            "RendererCsgSystem&",
        }
    ));
    EXPECT_FALSE(ContainsText(avboitHeader, "RendererDeferredState"));
    EXPECT_FALSE(ContainsText(avboitHeader, "RendererDeferredSystem"));
    EXPECT_FALSE(ContainsText(avboitHeader, "DeferredLightingGraphResources"));
    EXPECT_FALSE(ContainsText(avboitSystem, "m_deferredState"));
    EXPECT_FALSE(ContainsText(avboitResources, "m_deferredState"));
    EXPECT_TRUE(ContainsText(avboitResources, "m_avboitState.m_linearSampler"));

    const usize deferredStateBegin = compactRendererState.find("classRendererDeferredStatefinal:NoCopy{");
    ASSERT_NE(deferredStateBegin, AStringView::npos);
    const usize deferredStateEnd = compactRendererState.find("classRendererAvboitStatefinal:NoCopy{", deferredStateBegin);
    ASSERT_NE(deferredStateEnd, AStringView::npos);
    const AStringView deferredState = compactRendererState.substr(deferredStateBegin, deferredStateEnd - deferredStateBegin);
    EXPECT_FALSE(ContainsText(deferredState, "friendclassRendererAvboitSystem;"));

    const usize createDeferredTargets = rootResources.find("m_deferredSystem.createDeferredFrameTargets");
    const usize createAvboitResources = rootResources.find("m_avboitSystem.createAvboitResources");
    const usize createDeferredTargetResources = rootResources.find("m_deferredSystem.createDeferredFrameTargetResources");
    ASSERT_NE(createDeferredTargets, AStringView::npos);
    ASSERT_NE(createAvboitResources, AStringView::npos);
    ASSERT_NE(createDeferredTargetResources, AStringView::npos);
    EXPECT_LT(createDeferredTargets, createAvboitResources);
    EXPECT_LT(createAvboitResources, createDeferredTargetResources);
}


TEST(EcsGraphics, CsgConsumesTheActiveDeferredTargetContractWithoutDeferredStatePrivilege){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString csgHeaderSource;
    AString csgSystemSource;
    AString csgResourcesSource;
    AString rendererStateSource;
    AString frameTypesSource;
    AString materialDrawSource;
    AString rootPrefixSource;
    AString rootGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.cpp", csgSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_types.h", frameTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_draw.cpp", materialDrawSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", rootPrefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));

    const AStringView csgHeader(csgHeaderSource.data(), csgHeaderSource.size());
    const AStringView csgSystem(csgSystemSource.data(), csgSystemSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());
    const AString compactCsgHeaderStorage = CompactSource(csgHeader);
    const AStringView compactCsgHeader(compactCsgHeaderStorage.data(), compactCsgHeaderStorage.size());
    const AString compactCsgResourcesStorage = CompactSource(csgResources);
    const AStringView compactCsgResources(compactCsgResourcesStorage.data(), compactCsgResourcesStorage.size());
    const AString compactRendererStateStorage = CompactSource(AStringView(rendererStateSource.data(), rendererStateSource.size()));
    const AStringView compactRendererState(compactRendererStateStorage.data(), compactRendererStateStorage.size());
    const AString compactFrameTypesStorage = CompactSource(AStringView(frameTypesSource.data(), frameTypesSource.size()));
    const AStringView compactFrameTypes(compactFrameTypesStorage.data(), compactFrameTypesStorage.size());
    const AString compactMaterialDrawStorage = CompactSource(AStringView(materialDrawSource.data(), materialDrawSource.size()));
    const AStringView compactMaterialDraw(compactMaterialDrawStorage.data(), compactMaterialDrawStorage.size());
    const AString compactRootPrefixStorage = CompactSource(AStringView(rootPrefixSource.data(), rootPrefixSource.size()));
    const AStringView compactRootPrefix(compactRootPrefixStorage.data(), compactRootPrefixStorage.size());
    const AString compactRootGraphStorage = CompactSource(AStringView(rootGraphSource.data(), rootGraphSource.size()));
    const AStringView compactRootGraph(compactRootGraphStorage.data(), compactRootGraphStorage.size());

    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactCsgHeader,
        "RendererCsgSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::ECS::World&",
            "Core::Graphics&",
            "CsgShapeRegistry&",
            "RendererDrawState&",
            "RendererCsgState&",
            "RendererShaderSystem&",
            "RendererMeshSystem&",
        }
    ));
    EXPECT_FALSE(ContainsText(csgHeader, "RendererDeferredState"));
    EXPECT_FALSE(ContainsText(csgHeader, "m_deferredState"));
    EXPECT_FALSE(ContainsText(csgSystem, "m_deferredState"));
    EXPECT_FALSE(ContainsText(csgResources, "m_deferredState"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "prepareCsgClipContextSlotData(constDeferredFrameTargets&targets,"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "setCsgReceiverSurfaceImageStates(Core::CommandList&commandList,constDeferredFrameTargets&targets);"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "setCsgIntervalSampleImageStates(Core::CommandList&commandList,constDeferredFrameTargets&targets);"));
    EXPECT_TRUE(ContainsText(compactCsgResources, "targets.bindless.slotsBufferDescriptor.slot()"));

    const usize deferredStateBegin = compactRendererState.find("classRendererDeferredStatefinal:NoCopy{");
    ASSERT_NE(deferredStateBegin, AStringView::npos);
    const usize deferredStateEnd = compactRendererState.find("classRendererAvboitStatefinal:NoCopy{", deferredStateBegin);
    ASSERT_NE(deferredStateEnd, AStringView::npos);
    const AStringView deferredState = compactRendererState.substr(deferredStateBegin, deferredStateEnd - deferredStateBegin);
    EXPECT_FALSE(ContainsText(deferredState, "friendclassRendererCsgSystem;"));

    EXPECT_TRUE(ContainsText(compactFrameTypes, "structMaterialPassDrawContext{Core::CommandList&commandList;constDeferredFrameTargets&deferredTargets;"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "context.deferredTargets"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "setCsgReceiverSurfaceImageStates(commandList,deferredTargets)"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "setCsgIntervalSampleImageStates(commandList,deferredTargets)"));
    EXPECT_EQ(CountText(compactRootPrefix, "prepareCsgClipContextSlotData(deferredTargets,"), 1u);
    EXPECT_EQ(CountText(compactRootGraph, "prepareCsgClipContextSlotData(deferredTargets,"), 4u);
}


TEST(EcsGraphics, RootInvalidatesFeatureResourcesThroughDomainSystems){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString meshHeaderSource;
    AString meshSystemSource;
    AString materialHeaderSource;
    AString materialSystemSource;
    AString csgHeaderSource;
    AString csgSystemSource;
    AString rendererStateSource;
    AString rootResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.cpp", meshSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.cpp", materialSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.cpp", csgSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_state.h", rendererStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rootResourcesSource));

    const AString compactMeshHeaderStorage = CompactSource(AStringView(meshHeaderSource.data(), meshHeaderSource.size()));
    const AStringView compactMeshHeader(compactMeshHeaderStorage.data(), compactMeshHeaderStorage.size());
    const AString compactMeshSystemStorage = CompactSource(AStringView(meshSystemSource.data(), meshSystemSource.size()));
    const AStringView compactMeshSystem(compactMeshSystemStorage.data(), compactMeshSystemStorage.size());
    const AString compactMaterialHeaderStorage = CompactSource(AStringView(materialHeaderSource.data(), materialHeaderSource.size()));
    const AStringView compactMaterialHeader(compactMaterialHeaderStorage.data(), compactMaterialHeaderStorage.size());
    const AString compactMaterialSystemStorage = CompactSource(AStringView(materialSystemSource.data(), materialSystemSource.size()));
    const AStringView compactMaterialSystem(compactMaterialSystemStorage.data(), compactMaterialSystemStorage.size());
    const AString compactCsgHeaderStorage = CompactSource(AStringView(csgHeaderSource.data(), csgHeaderSource.size()));
    const AStringView compactCsgHeader(compactCsgHeaderStorage.data(), compactCsgHeaderStorage.size());
    const AString compactCsgSystemStorage = CompactSource(AStringView(csgSystemSource.data(), csgSystemSource.size()));
    const AStringView compactCsgSystem(compactCsgSystemStorage.data(), compactCsgSystemStorage.size());
    const AString compactRendererStateStorage = CompactSource(AStringView(rendererStateSource.data(), rendererStateSource.size()));
    const AStringView compactRendererState(compactRendererStateStorage.data(), compactRendererStateStorage.size());
    const AString compactRootResourcesStorage = CompactSource(AStringView(rootResourcesSource.data(), rootResourcesSource.size()));
    const AStringView compactRootResources(compactRootResourcesStorage.data(), compactRootResourcesStorage.size());

    EXPECT_TRUE(ContainsText(compactMeshHeader, "public:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactMaterialHeader, "public:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "public:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactMeshHeader, "private:voidreleaseAllMeshGeometryHeapHandles();private:"));
    EXPECT_TRUE(ContainsText(compactMaterialHeader, "private:voidreleaseMaterialResourceReferences();private:"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "private:voidreleaseCsgClipContextHeapHandles();private:"));
    EXPECT_FALSE(ContainsText(compactMaterialHeader, "public:voidreleaseMaterialResourceReferences();"));
    EXPECT_FALSE(ContainsText(compactCsgHeader, "public:voidreleaseCsgClipContextHeapHandles();"));

    const usize meshStateBegin = compactRendererState.find("classRendererMeshStatefinal:NoCopy{");
    const usize meshStateEnd = compactRendererState.find("classRendererMaterialStatefinal:NoCopy{", meshStateBegin);
    const usize materialStateBegin = meshStateEnd;
    const usize materialStateEnd = compactRendererState.find("classRendererDrawStatefinal:NoCopy{", materialStateBegin);
    const usize csgStateBegin = compactRendererState.find("classRendererCsgStatefinal:NoCopy{", materialStateEnd);
    const usize csgStateEnd = compactRendererState.find("classRendererDeferredStatefinal:NoCopy{", csgStateBegin);
    ASSERT_NE(meshStateBegin, AStringView::npos);
    ASSERT_NE(meshStateEnd, AStringView::npos);
    ASSERT_NE(materialStateEnd, AStringView::npos);
    ASSERT_NE(csgStateBegin, AStringView::npos);
    ASSERT_NE(csgStateEnd, AStringView::npos);
    const AStringView meshState = compactRendererState.substr(meshStateBegin, meshStateEnd - meshStateBegin);
    const AStringView materialState = compactRendererState.substr(materialStateBegin, materialStateEnd - materialStateBegin);
    const AStringView csgState = compactRendererState.substr(csgStateBegin, csgStateEnd - csgStateBegin);
    EXPECT_TRUE(ContainsText(meshState, "private:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(materialState, "private:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(csgState, "private:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(meshState, "public:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(materialState, "public:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(csgState, "public:voidinvalidateResources();"));

    const usize meshDomainInvalidationBegin = compactMeshSystem.find("RendererMeshSystem::invalidateResources(){");
    const usize meshDomainInvalidationEnd = compactMeshSystem.find('}', meshDomainInvalidationBegin);
    const usize materialDomainInvalidationBegin = compactMaterialSystem.find("RendererMaterialSystem::invalidateResources(){");
    const usize materialDomainInvalidationEnd = compactMaterialSystem.find('}', materialDomainInvalidationBegin);
    const usize csgDomainInvalidationBegin = compactCsgSystem.find("RendererCsgSystem::invalidateResources(){");
    const usize csgDomainInvalidationEnd = compactCsgSystem.find('}', csgDomainInvalidationBegin);
    ASSERT_NE(meshDomainInvalidationBegin, AStringView::npos);
    ASSERT_NE(meshDomainInvalidationEnd, AStringView::npos);
    ASSERT_NE(materialDomainInvalidationBegin, AStringView::npos);
    ASSERT_NE(materialDomainInvalidationEnd, AStringView::npos);
    ASSERT_NE(csgDomainInvalidationBegin, AStringView::npos);
    ASSERT_NE(csgDomainInvalidationEnd, AStringView::npos);
    const AStringView meshDomainInvalidation = compactMeshSystem.substr(
        meshDomainInvalidationBegin,
        meshDomainInvalidationEnd - meshDomainInvalidationBegin
    );
    const AStringView materialDomainInvalidation = compactMaterialSystem.substr(
        materialDomainInvalidationBegin,
        materialDomainInvalidationEnd - materialDomainInvalidationBegin
    );
    const AStringView csgDomainInvalidation = compactCsgSystem.substr(
        csgDomainInvalidationBegin,
        csgDomainInvalidationEnd - csgDomainInvalidationBegin
    );
    const usize meshGeometryRelease = meshDomainInvalidation.find("releaseAllMeshGeometryHeapHandles()");
    const usize meshFrameRelease = meshDomainInvalidation.find("releaseMeshFrameHeapHandles()");
    const usize meshStateInvalidation = meshDomainInvalidation.find("m_meshState.invalidateResources()");
    const usize materialReferenceRelease = materialDomainInvalidation.find("releaseMaterialResourceReferences()");
    const usize materialStateInvalidation = materialDomainInvalidation.find("m_materialState.invalidateResources()");
    const usize csgHeapRelease = csgDomainInvalidation.find("releaseCsgClipContextHeapHandles()");
    const usize csgStateInvalidation = csgDomainInvalidation.find("m_csgState.invalidateResources()");
    ASSERT_NE(meshGeometryRelease, AStringView::npos);
    ASSERT_NE(meshFrameRelease, AStringView::npos);
    ASSERT_NE(meshStateInvalidation, AStringView::npos);
    ASSERT_NE(materialReferenceRelease, AStringView::npos);
    ASSERT_NE(materialStateInvalidation, AStringView::npos);
    ASSERT_NE(csgHeapRelease, AStringView::npos);
    ASSERT_NE(csgStateInvalidation, AStringView::npos);
    EXPECT_LT(meshGeometryRelease, meshFrameRelease);
    EXPECT_LT(meshFrameRelease, meshStateInvalidation);
    EXPECT_LT(materialReferenceRelease, materialStateInvalidation);
    EXPECT_LT(csgHeapRelease, csgStateInvalidation);
    EXPECT_FALSE(ContainsText(compactRootResources, "m_meshState.invalidateResources()"));
    EXPECT_FALSE(ContainsText(compactRootResources, "m_materialState.invalidateResources()"));
    EXPECT_FALSE(ContainsText(compactRootResources, "m_csgState.invalidateResources()"));

    const usize rayTracingInvalidation = compactRootResources.find("m_raytracingSystem.invalidateResources()");
    const usize avboitInvalidation = compactRootResources.find("m_avboitSystem.invalidateResources()");
    const usize shaderInvalidation = compactRootResources.find("m_shaderSystem.invalidateResources()");
    const usize meshInvalidation = compactRootResources.find("m_meshSystem.invalidateResources()");
    const usize materialInvalidation = compactRootResources.find("m_materialSystem.invalidateResources()");
    const usize drawInvalidation = compactRootResources.find("m_drawState.invalidateResources()");
    const usize csgInvalidation = compactRootResources.find("m_csgSystem.invalidateResources()");
    const usize deferredInvalidation = compactRootResources.find("m_deferredSystem.invalidateResources()");
    ASSERT_NE(rayTracingInvalidation, AStringView::npos);
    ASSERT_NE(avboitInvalidation, AStringView::npos);
    ASSERT_NE(shaderInvalidation, AStringView::npos);
    ASSERT_NE(meshInvalidation, AStringView::npos);
    ASSERT_NE(materialInvalidation, AStringView::npos);
    ASSERT_NE(drawInvalidation, AStringView::npos);
    ASSERT_NE(csgInvalidation, AStringView::npos);
    ASSERT_NE(deferredInvalidation, AStringView::npos);
    EXPECT_LT(rayTracingInvalidation, meshInvalidation);
    EXPECT_LT(avboitInvalidation, materialInvalidation);
    EXPECT_LT(meshInvalidation, drawInvalidation);
    EXPECT_LT(materialInvalidation, drawInvalidation);
}


TEST(EcsGraphics, FramePipelineDoesNotPrivilegeNarrowShaderOrMeshSystems){
    TestArena testArena;
    AString headerSource;
    ASSERT_TRUE(ReadTextFile(RepoRoot(testArena) / "impl" / "ecs_render" / "renderer_frame_pipeline.h", headerSource));
    const AString compactHeaderStorage = CompactSource(AStringView(headerSource.data(), headerSource.size()));
    const AStringView compactHeader(compactHeaderStorage.data(), compactHeaderStorage.size());

    EXPECT_FALSE(ContainsText(compactHeader, "friendclassRendererShaderSystem;"));
    EXPECT_FALSE(ContainsText(compactHeader, "friendclassRendererMeshSystem;"));
}


TEST(EcsGraphics, RootFrameGraphUsesRayTracingContractsInsteadOfDomainState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString shadowPrepareSource;
    AString shadowVisibilitySource;
    AString causticsSource;
    AString surfelGiSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_prepare.cpp", shadowPrepareSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", shadowVisibilitySource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", causticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", surfelGiSource));

    EXPECT_FALSE(ContainsText(AStringView(shadowPrepareSource.data(), shadowPrepareSource.size()), "m_rayTracingState"));
    EXPECT_FALSE(ContainsText(AStringView(shadowVisibilitySource.data(), shadowVisibilitySource.size()), "m_rayTracingState"));
    EXPECT_FALSE(ContainsText(AStringView(causticsSource.data(), causticsSource.size()), "m_rayTracingState"));
    EXPECT_FALSE(ContainsText(AStringView(surfelGiSource.data(), surfelGiSource.size()), "m_rayTracingState"));
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

