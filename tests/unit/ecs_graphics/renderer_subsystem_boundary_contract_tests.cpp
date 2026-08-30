// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <tests/common/test_context.h>
#include <gtest/gtest.h>

#include <global/filesystem/directory_iterator.h>
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
        }
    ));
}


TEST(EcsGraphics, MeshOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString stateHeaderSource;
    AString stateSystemSource;
    AString meshHeaderSource;
    AString meshSystemSource;
    AString meshResourcesSource;
    AString meshBindingsSource;
    AString meshViewSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_state.h", stateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_state.cpp", stateSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.cpp", meshSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_bindings.cpp", meshBindingsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_view.cpp", meshViewSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactStateHeaderStorage = CompactSource(AStringView(stateHeaderSource.data(), stateHeaderSource.size()));
    const AStringView compactStateHeader(compactStateHeaderStorage.data(), compactStateHeaderStorage.size());
    const AString compactStateSystemStorage = CompactSource(AStringView(stateSystemSource.data(), stateSystemSource.size()));
    const AStringView compactStateSystem(compactStateSystemStorage.data(), compactStateSystemStorage.size());
    const AString compactMeshHeaderStorage = CompactSource(
        AStringView(meshHeaderSource.data(), meshHeaderSource.size())
    );
    const AStringView compactMeshHeader(compactMeshHeaderStorage.data(), compactMeshHeaderStorage.size());

    EXPECT_TRUE(ContainsText(compactStateHeader, "classRendererMeshStatefinal:NoCopy{friendclassRendererMeshSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererFramePipeline;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererAvboitSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererCsgSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererDeferredSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererRayTracingSystem;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "explicitRendererMeshState(Core::Alloc::GlobalArena&arena);"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "private:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "public:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "HashMap<Name,MeshResources,Hasher<Name>,EqualTo<Name>,Core::Alloc::GlobalArena>m_meshes;"
    ));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_meshViewBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "ECSRenderDetail::MeshFrameBindingSnapshotm_frameBindings;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_meshViewGpuDataValid=false;"));
    EXPECT_TRUE(ContainsText(
        compactStateSystem,
        "RendererMeshState::RendererMeshState(Core::Alloc::GlobalArena&arena):m_meshes(0,Hasher<Name>(),EqualTo<Name>(),arena){}"
    ));
    EXPECT_TRUE(ContainsText(compactStateSystem, "voidRendererMeshState::invalidateResources(){m_meshes.clear();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_meshViewBuffer.reset();m_frameBindings={};m_meshViewGpuDataValid=false;"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <impl/ecs_render/mesh/renderer_mesh_types.h>"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <impl/ecs_render/shared/renderer_frame_bindings.h>"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "shared/renderer_state.h"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "mesh/mesh_system.h"));
    EXPECT_TRUE(ContainsText(meshHeaderSource, "class RendererMeshState;"));
    EXPECT_FALSE(ContainsText(meshHeaderSource, "renderer_mesh_state.h"));
    EXPECT_FALSE(ContainsText(compactMeshHeader, "structMeshFrameHeapSlots;"));
    EXPECT_EQ(CountText(compactMeshHeader, "releaseMeshFrameHeapHandles();"), 1u);
    EXPECT_TRUE(ContainsText(compactMeshHeader, "private:voidreleaseMeshFrameHeapHandles();"));
    EXPECT_TRUE(ContainsText(meshSystemSource, "#include <impl/ecs_render/mesh/renderer_mesh_state.h>"));
    EXPECT_FALSE(ContainsText(meshSystemSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(meshResourcesSource, "#include <impl/ecs_render/mesh/renderer_mesh_state.h>"));
    EXPECT_FALSE(ContainsText(meshResourcesSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(meshBindingsSource, "#include <impl/ecs_render/mesh/renderer_mesh_state.h>"));
    EXPECT_FALSE(ContainsText(meshBindingsSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(meshViewSource, "#include <impl/ecs_render/mesh/renderer_mesh_state.h>"));
    EXPECT_FALSE(ContainsText(meshViewSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "#include <impl/ecs_render/mesh/renderer_mesh_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "RendererMeshState m_meshState;"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/mesh/renderer_mesh_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/mesh/renderer_mesh_state.h"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/shared/renderer_frame_bindings.h"));
    EXPECT_FALSE(ContainsText(rendererCmakeSource, "shared/renderer_state"));
}


TEST(EcsGraphics, RayTracingUsesMeshDomainContractsWithoutSharedStatePrivilege){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString meshHeaderSource;
    AString meshResourcesSource;
    AString meshViewSource;
    AString rayTracingHeaderSource;
    AString rayTracingSystemSource;
    AString rayTracingPrivateSource;
    AString rayTracingCausticsSource;
    AString rayTracingDetailSource;
    AString rayTracingSoftShadowSource;
    AString rayTracingSwBvhSource;
    AString meshStateSource;
    AString frameBindingsSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_resources.cpp", meshResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_view.cpp", meshViewSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_private.h", rayTracingPrivateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", rayTracingCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_detail.cpp", rayTracingDetailSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", rayTracingSoftShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", rayTracingSwBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_state.h", meshStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_bindings.h", frameBindingsSource));

    const AStringView meshHeader(meshHeaderSource.data(), meshHeaderSource.size());
    const AStringView meshResources(meshResourcesSource.data(), meshResourcesSource.size());
    const AStringView meshView(meshViewSource.data(), meshViewSource.size());
    const AStringView rayTracingHeader(rayTracingHeaderSource.data(), rayTracingHeaderSource.size());
    const AStringView rayTracingSystem(rayTracingSystemSource.data(), rayTracingSystemSource.size());
    const AStringView rayTracingPrivate(rayTracingPrivateSource.data(), rayTracingPrivateSource.size());
    const AStringView rayTracingCaustics(rayTracingCausticsSource.data(), rayTracingCausticsSource.size());
    const AStringView rayTracingDetail(rayTracingDetailSource.data(), rayTracingDetailSource.size());
    const AStringView rayTracingSoftShadow(rayTracingSoftShadowSource.data(), rayTracingSoftShadowSource.size());
    const AStringView rayTracingSwBvh(rayTracingSwBvhSource.data(), rayTracingSwBvhSource.size());
    const AString compactMeshStateStorage = CompactSource(AStringView(meshStateSource.data(), meshStateSource.size()));
    const AStringView compactMeshState(compactMeshStateStorage.data(), compactMeshStateStorage.size());
    const AString compactFrameBindingsStorage = CompactSource(
        AStringView(frameBindingsSource.data(), frameBindingsSource.size())
    );
    const AStringView compactFrameBindings(compactFrameBindingsStorage.data(), compactFrameBindingsStorage.size());

    EXPECT_FALSE(ContainsText(rayTracingHeader, "RendererMeshState"));
    EXPECT_FALSE(ContainsText(rayTracingHeader, "RendererDrawState"));
    EXPECT_FALSE(ContainsText(rayTracingSystem, "m_meshState"));
    EXPECT_FALSE(ContainsText(rayTracingSystem, "m_drawState"));
    EXPECT_FALSE(ContainsText(rayTracingCaustics, "m_drawState"));
    EXPECT_FALSE(ContainsText(rayTracingSoftShadow, "m_drawState"));
    EXPECT_FALSE(ContainsText(rayTracingSwBvh, "m_meshState"));
    EXPECT_FALSE(ContainsText(rayTracingPrivate, "MeshResources*&"));
    EXPECT_FALSE(ContainsText(rayTracingDetail, "MeshResources*&"));
    EXPECT_TRUE(ContainsText(meshHeader, "struct MeshRayTracingResourceSnapshot{"));
    EXPECT_TRUE(ContainsText(meshHeader, "collectRayTracingResourceSnapshots("));
    EXPECT_TRUE(ContainsText(meshHeader, "findRenderableRayTracingResourceSnapshot("));
    EXPECT_TRUE(ContainsText(meshHeader, "commitRayTracingResourceSnapshot("));
    EXPECT_TRUE(ContainsText(meshHeader, "confirmAcceptedRayTracingStateHandoffs()noexcept;"));
    EXPECT_TRUE(ContainsText(meshHeader, "discardRayTracingBuildState()noexcept;"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "Core::GpuDescriptorHandleheapHandle"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "boolbindingValid()constnoexcept"));
    EXPECT_TRUE(ContainsText(meshHeader, "snapshotAcceptedMeshViewWorldToClip(Float44& outWorldToClip)const noexcept;"));
    EXPECT_TRUE(ContainsText(meshView, "snapshot.heapHandle = m_meshState.m_frameBindings.meshView.heapHandle;"));
    EXPECT_TRUE(ContainsText(meshView, "if(!m_meshState.m_meshViewGpuDataValid)"));
    EXPECT_TRUE(ContainsText(meshView, "ECSRenderDetail::MeshViewGpuData acceptedView;"));
    EXPECT_TRUE(ContainsText(meshView, "outWorldToClip = acceptedView.worldToClip;"));
    EXPECT_TRUE(ContainsText(meshResources, "RayTracingResourceSnapshotMatches(found.value(), expected)"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "m_meshSystem.confirmAcceptedRayTracingStateHandoffs();"));
    EXPECT_TRUE(ContainsText(rayTracingSystem, "m_meshSystem.discardRayTracingBuildState();"));
    EXPECT_TRUE(ContainsText(rayTracingCaustics, "m_meshSystem.meshViewBufferSnapshot()"));
    EXPECT_TRUE(ContainsText(rayTracingCaustics, "ECSRenderDetail::MeshViewBufferSnapshot meshView;"));
    EXPECT_TRUE(ContainsText(rayTracingCaustics, ".meshView = meshView,"));
    EXPECT_TRUE(ContainsText(rayTracingCaustics, "payload.raytracingSystem->hasCausticWork(payload.meshView)"));
    EXPECT_TRUE(ContainsText(rayTracingCaustics, "payload.raytracingSystem->hasHwCausticWork(payload.meshView)"));
    EXPECT_TRUE(ContainsText(rayTracingSoftShadow, "m_meshSystem.snapshotAcceptedMeshViewWorldToClip(acceptedWorldToClip)"));
    EXPECT_FALSE(ContainsText(rayTracingPrivate, "mesh_view_private.h"));
    EXPECT_FALSE(ContainsText(rayTracingSoftShadow, "reinterpret_cast<const ECSRenderDetail::MeshViewGpuData*>"));

    EXPECT_FALSE(ContainsText(compactMeshState, "friendclassRendererRayTracingSystem;"));
}


TEST(EcsGraphics, RayTracingOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString stateHeaderSource;
    AString stateSystemSource;
    AString rayTracingHeaderSource;
    AString rayTracingSystemSource;
    AString rayTracingDetailSource;
    AString rayTracingSwBvhSource;
    AString rayTracingShadowSource;
    AString rayTracingSoftShadowSource;
    AString rayTracingCausticsSource;
    AString rayTracingSurfelSource;
    AString rayTracingPrivateSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.h", stateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.cpp", stateSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_detail.cpp", rayTracingDetailSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_swbvh.cpp", rayTracingSwBvhSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", rayTracingShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", rayTracingSoftShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", rayTracingCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", rayTracingSurfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_private.h", rayTracingPrivateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactStateHeaderStorage = CompactSource(AStringView(stateHeaderSource.data(), stateHeaderSource.size()));
    const AStringView compactStateHeader(compactStateHeaderStorage.data(), compactStateHeaderStorage.size());
    const AString compactStateSystemStorage = CompactSource(AStringView(stateSystemSource.data(), stateSystemSource.size()));
    const AStringView compactStateSystem(compactStateSystemStorage.data(), compactStateSystemStorage.size());

    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtMeshHeapHandleCacheEntry{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "usingRtMeshHeapHandleCache=HashMap<"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtSceneBvhState{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtShadowState{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtSoftShadowState{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtCausticState{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "structRtSurfelGiState{"));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "classRendererRayTracingStatefinal:NoCopy,publicRtSceneBvhState,publicRtShadowState,publicRtSoftShadowState,publicRtCausticState,publicRtSurfelGiState{friendclassRendererRayTracingSystem;"
    ));
    EXPECT_EQ(CountText(compactStateHeader, "friendclassRenderer"), 1u);
    EXPECT_TRUE(ContainsText(compactStateHeader, "explicitRtShadowState(Core::Alloc::GlobalArena&arena):m_shadowMeshIndexBuffers(arena)"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "explicitRendererRayTracingState(Core::Alloc::GlobalArena&arena):RtShadowState(arena){}"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "private:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "public:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GpuDescriptorHandlem_tlasHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "f32m_swShadowEdgeThreshold=ECSRenderDetail::s_DefaultSwShadowEdgeThreshold;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "f32m_causticTemporalDecay=ECSRenderDetail::s_DefaultCausticTemporalDecay;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u32m_surfelPoolCapacity=NWB_SURFEL_POOL_CAPACITY;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u32m_surfelHashCellCount=NWB_SURFEL_HASH_CELL_COUNT;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_swShadowAdaptiveEnabled=true;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_swShadowCompactEnabled=true;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u32m_softShadowHistoryFrontIsA=1u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_surfelResourcesNeedClear=false;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "voidRendererRayTracingState::invalidateResources(){"));
    EXPECT_EQ(CountText(compactStateSystem, ".reset();"), 109u);
    EXPECT_EQ(CountText(compactStateSystem, ".clear();"), 16u);
    EXPECT_EQ(CountText(compactStateSystem, "Core::GpuDescriptorHandle::invalid();"), 25u);
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_tlasBackingFresh=false;m_tlasBackingStateHandoffPending=false;"));
    EXPECT_FALSE(ContainsText(compactStateSystem, "m_swShadowEdgeThreshold="));
    EXPECT_FALSE(ContainsText(compactStateSystem, "m_causticTemporalDecay="));
    EXPECT_FALSE(ContainsText(compactStateSystem, "m_shadowMeshHeapHighWater="));
    EXPECT_FALSE(ContainsText(compactStateSystem, "m_swShadowMeshHeapHighWater="));
    EXPECT_FALSE(ContainsText(compactStateSystem, "m_capabilityLogged="));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_surfelAgeFreePipelineFailed=false;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_surfelResourcesNeedClear=false;m_surfelResourcesClearPending=false;"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "shared/renderer_state.h"));
    EXPECT_TRUE(ContainsText(rayTracingHeaderSource, "class RendererRayTracingState;"));
    EXPECT_FALSE(ContainsText(rayTracingHeaderSource, "shared/renderer_state.h"));
    EXPECT_FALSE(ContainsText(rayTracingHeaderSource, "renderer_raytracing_state.h"));
    EXPECT_TRUE(ContainsText(rayTracingSystemSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingDetailSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingSwBvhSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingShadowSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingSoftShadowSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingCausticsSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(rayTracingSurfelSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_FALSE(ContainsText(rayTracingPrivateSource, "renderer_raytracing_state.h"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "#include <impl/ecs_render/raytrace/renderer_raytracing_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "RendererRayTracingState m_rayTracingState;"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/raytrace/renderer_raytracing_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/raytrace/renderer_raytracing_state.h"));
}


TEST(EcsGraphics, MaterialOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString stateHeaderSource;
    AString stateSystemSource;
    AString pipelineTypesSystemSource;
    AString materialHeaderSource;
    AString materialSystemSource;
    AString materialInstanceSource;
    AString materialSurfaceSource;
    AString materialPassSource;
    AString materialPassDrawSource;
    AString materialPassResourcesSource;
    AString materialPipelineSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_material_state.h", stateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_material_state.cpp", stateSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_pipeline_types.cpp", pipelineTypesSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.cpp", materialSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_instance.cpp", materialInstanceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_surface.cpp", materialSurfaceSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_draw.cpp", materialPassDrawSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialPassResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pipeline.cpp", materialPipelineSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactStateHeaderStorage = CompactSource(AStringView(stateHeaderSource.data(), stateHeaderSource.size()));
    const AStringView compactStateHeader(compactStateHeaderStorage.data(), compactStateHeaderStorage.size());
    const AString compactStateSystemStorage = CompactSource(AStringView(stateSystemSource.data(), stateSystemSource.size()));
    const AStringView compactStateSystem(compactStateSystemStorage.data(), compactStateSystemStorage.size());
    const AString compactPipelineTypesSystemStorage = CompactSource(AStringView(pipelineTypesSystemSource.data(), pipelineTypesSystemSource.size()));
    const AStringView compactPipelineTypesSystem(compactPipelineTypesSystemStorage.data(), compactPipelineTypesSystemStorage.size());
    const AString compactMaterialSystemStorage = CompactSource(AStringView(materialSystemSource.data(), materialSystemSource.size()));
    const AStringView compactMaterialSystem(compactMaterialSystemStorage.data(), compactMaterialSystemStorage.size());

    EXPECT_TRUE(ContainsText(compactStateHeader, "structRendererMaterialResourceState{"));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "HashMap<Name,UniquePtr<TextureGpuResource>,Hasher<Name>,EqualTo<Name>,Core::Alloc::GlobalArena>textureAssetCache;"
    ));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "HashMap<Name,UniquePtr<SamplerGpuResource>,Hasher<Name>,EqualTo<Name>,Core::Alloc::GlobalArena>samplerAssetCache;"
    ));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "explicitRendererMaterialResourceState(Core::Alloc::GlobalArena&arena):textureAssetCache(0,Hasher<Name>(),EqualTo<Name>(),arena),samplerAssetCache(0,Hasher<Name>(),EqualTo<Name>(),arena){}"
    ));
    EXPECT_TRUE(ContainsText(compactStateHeader, "classRendererMaterialStatefinal:NoCopy{friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererFramePipeline;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererAvboitSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererCsgSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererDeferredSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererMeshSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererRayTracingSystem;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_materialPassBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_computeBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_instanceBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_materialTypedBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_emulationVertexShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::InputLayoutHandlem_emulationInputLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "usizem_instanceBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "usizem_materialTypedBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "HashMap<Name,MaterialSurfaceInfo,Hasher<Name>,EqualTo<Name>,Core::Alloc::GlobalArena>m_surfaceInfos;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "RendererMaterialResourceStatem_resourceState;"));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "HashMap<MaterialPipelineKey,MaterialPipelineResources,MaterialPipelineKeyHasher,MaterialPipelineKeyEqualTo,Core::Alloc::GlobalArena>m_pipelines;"
    ));
    EXPECT_TRUE(ContainsText(
        compactStateHeader,
        "HashMap<Core::ECS::EntityID,MaterialInstanceMutableCacheEntry,Hasher<Core::ECS::EntityID>,EqualTo<Core::ECS::EntityID>,Core::Alloc::GlobalArena>m_instanceMutableCache;"
    ));
    EXPECT_TRUE(ContainsText(compactStateHeader, "HashMap<Name,RenderPath::Enum,Hasher<Name>,EqualTo<Name>,Core::Alloc::GlobalArena>m_loggedMaterialPaths;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u64m_instanceMutableCacheComponentMutationVersion=0u;"));
    EXPECT_TRUE(ContainsText(
        compactStateSystem,
        "RendererMaterialState::RendererMaterialState(Core::Alloc::GlobalArena&arena):m_surfaceInfos(0,Hasher<Name>(),EqualTo<Name>(),arena),m_resourceState(arena),m_pipelines(0,MaterialPipelineKeyHasher(),MaterialPipelineKeyEqualTo(),arena),m_instanceMutableCache(0,Hasher<Core::ECS::EntityID>(),EqualTo<Core::ECS::EntityID>(),arena),m_loggedMaterialPaths(0,Hasher<Name>(),EqualTo<Name>(),arena){}"
    ));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_pipelines.clear();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_materialPassBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_computeBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_instanceBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_materialTypedBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_emulationVertexShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_emulationInputLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_instanceMutableCache.clear();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_loggedMaterialPaths.clear();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_instanceBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_materialTypedBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_instanceMutableCacheComponentMutationVersion=0u;"));
    EXPECT_TRUE(ContainsText(compactPipelineTypesSystem, "usizeMaterialPipelineKeyHasher::operator()(constMaterialPipelineKey&key)const{"));
    EXPECT_TRUE(ContainsText(compactPipelineTypesSystem, "boolMaterialPipelineKeyEqualTo::operator()(constMaterialPipelineKey&lhs,constMaterialPipelineKey&rhs)const{"));
    EXPECT_TRUE(ContainsText(compactMaterialSystem, "releaseMaterialResourceReferences();m_materialState.invalidateResources();"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <impl/ecs_render/material/renderer_draw_types.h>"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <core/ecs/entity_id.h>"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <core/graphics/rhi/pipeline.h>"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <impl/assets_sampler/loader.h>"));
    EXPECT_TRUE(ContainsText(stateHeaderSource, "#include <impl/assets_texture/loader.h>"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "shared/renderer_state.h"));
    EXPECT_FALSE(ContainsText(stateSystemSource, "shared/renderer_state.h"));
    EXPECT_TRUE(ContainsText(materialHeaderSource, "class RendererMaterialState;"));
    EXPECT_FALSE(ContainsText(materialHeaderSource, "renderer_material_state.h"));
    EXPECT_TRUE(ContainsText(materialSystemSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_FALSE(ContainsText(materialSystemSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(materialInstanceSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_FALSE(ContainsText(materialInstanceSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(materialSurfaceSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_FALSE(ContainsText(materialSurfaceSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(materialPassResourcesSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_FALSE(ContainsText(materialPassResourcesSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(materialPipelineSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_FALSE(ContainsText(materialPipelineSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(materialPassDrawSource, "renderer_material_state.h"));
    EXPECT_FALSE(ContainsText(materialPassDrawSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(materialPassSource, "renderer_material_state.h"));
    EXPECT_FALSE(ContainsText(materialPassSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "#include <impl/ecs_render/material/renderer_material_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "RendererMaterialState m_materialState;"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/material/renderer_material_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/material/renderer_material_state.h"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/material/renderer_pipeline_types.cpp"));
}


TEST(EcsGraphics, MaterialDrawItemsRetainResourcesWithoutMeshStatePrivilege){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString materialHeaderSource;
    AString materialDrawTypesSource;
    AString materialPassSource;
    AString materialResourcesSource;
    AString materialDrawSource;
    AString materialStateSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_draw_types.h", materialDrawTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_draw.cpp", materialDrawSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_material_state.h", materialStateSource));

    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialDrawTypes(materialDrawTypesSource.data(), materialDrawTypesSource.size());
    const AStringView materialPass(materialPassSource.data(), materialPassSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AStringView materialDraw(materialDrawSource.data(), materialDrawSource.size());
    const AString compactMaterialStateStorage = CompactSource(AStringView(materialStateSource.data(), materialStateSource.size()));
    const AStringView compactMaterialState(compactMaterialStateStorage.data(), compactMaterialStateStorage.size());
    const usize primaryPipelineSnapshotOffset = materialPass.find(
        "const MaterialPassPipelineResourceSnapshot pipelineResourceSnapshot"
    );
    const usize receiverSurfacePipelineLookupOffset = materialPass.find(
        "createRendererPipeline(*materialInfo, csgReceiverSurfacePipelineKey"
    );
    const usize primaryDrawItemSnapshotOffset = materialPass.find("drawItem.pipelineResources = pipelineResourceSnapshot;");

    EXPECT_FALSE(ContainsText(materialHeader, "RendererMeshState"));
    EXPECT_FALSE(ContainsText(materialResources, "m_meshState"));
    EXPECT_TRUE(ContainsText(materialDrawTypes, "struct MaterialPassMeshResourceSnapshot{"));
    EXPECT_TRUE(ContainsText(materialDrawTypes, "struct MaterialPassPipelineResourceSnapshot{"));
    EXPECT_TRUE(ContainsText(materialDrawTypes, "MaterialPassMeshResourceSnapshot meshResources;"));
    EXPECT_TRUE(ContainsText(materialDrawTypes, "MaterialPassPipelineResourceSnapshot pipelineResources;"));
    EXPECT_TRUE(ContainsText(materialPass, "drawItem.meshResources = __hidden_material_pass::CaptureMeshResourceSnapshot(mesh);"));
    ASSERT_NE(primaryPipelineSnapshotOffset, AStringView::npos);
    ASSERT_NE(receiverSurfacePipelineLookupOffset, AStringView::npos);
    ASSERT_NE(primaryDrawItemSnapshotOffset, AStringView::npos);
    EXPECT_LT(primaryPipelineSnapshotOffset, receiverSurfacePipelineLookupOffset);
    EXPECT_LT(receiverSurfacePipelineLookupOffset, primaryDrawItemSnapshotOffset);
    EXPECT_EQ(
        materialPass.find("CapturePipelineResourceSnapshot(*pipelineResources)", receiverSurfacePipelineLookupOffset),
        AStringView::npos
    );
    EXPECT_TRUE(ContainsText(materialPass, "csgReceiverSurfaceDrawItem.pipelineResources ="));
    EXPECT_TRUE(ContainsText(materialResources, "const MaterialPassMeshResourceSnapshot& mesh = drawItem.meshResources;"));
    EXPECT_TRUE(ContainsText(materialDraw, "const MaterialPassPipelineResourceSnapshot& pipelineResources = drawItem.pipelineResources;"));
    EXPECT_FALSE(ContainsText(materialHeader, "findMaterialPassDrawItemResources"));
    EXPECT_FALSE(ContainsText(materialResources, "findMaterialPassDrawItemResources"));
    EXPECT_FALSE(ContainsText(materialDraw, "findMaterialPassDrawItemResources"));
    EXPECT_FALSE(ContainsText(materialResources, "findMeshResources(drawItem.meshKey"));
    EXPECT_FALSE(ContainsText(materialDraw, "findMeshResources(drawItem.meshKey"));
    EXPECT_FALSE(ContainsText(compactMaterialState, "friendclassRendererRayTracingSystem;"));
}


TEST(EcsGraphics, MaterialDomainOwnsTheSharedMaterialPassLayout){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString materialHeaderSource;
    AString materialPipelineSource;
    AString materialResourcesSource;
    AString avboitResourcesSource;
    AString avboitStateSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pipeline.cpp", materialPipelineSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp", materialResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_resources.cpp", avboitResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "renderer_avboit_state.h", avboitStateSource));

    const AStringView materialHeader(materialHeaderSource.data(), materialHeaderSource.size());
    const AStringView materialPipeline(materialPipelineSource.data(), materialPipelineSource.size());
    const AStringView materialResources(materialResourcesSource.data(), materialResourcesSource.size());
    const AStringView avboitResources(avboitResourcesSource.data(), avboitResourcesSource.size());
    const AString compactAvboitStateStorage = CompactSource(AStringView(avboitStateSource.data(), avboitStateSource.size()));
    const AStringView compactAvboitState(compactAvboitStateStorage.data(), compactAvboitStateStorage.size());
    const usize avboitStateBegin = compactAvboitState.find("classRendererAvboitStatefinal:NoCopy{");
    ASSERT_NE(avboitStateBegin, AStringView::npos);
    const usize avboitStateEnd = compactAvboitState.find("};", avboitStateBegin);
    ASSERT_NE(avboitStateEnd, AStringView::npos);
    const AStringView avboitState = compactAvboitState.substr(avboitStateBegin, avboitStateEnd - avboitStateBegin);

    EXPECT_FALSE(ContainsText(materialHeader, "RendererAvboitState"));
    EXPECT_FALSE(ContainsText(materialPipeline, "m_avboitState"));
    EXPECT_TRUE(ContainsText(materialResources, "m_materialState.m_materialPassBindingLayout"));
    EXPECT_TRUE(ContainsText(avboitResources, "m_materialSystem.prepareMaterialPassBindingLayout(materialPassBindingLayout)"));
    EXPECT_FALSE(ContainsText(avboitResources, "m_avboitState.m_emptyBindingLayout"));
    EXPECT_FALSE(ContainsText(avboitState, "friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(avboitState, "m_emptyBindingLayout"));
}


TEST(EcsGraphics, AvboitOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString avboitStateHeaderSource;
    AString avboitPrivateSource;
    AString avboitSystemSource;
    AString avboitStateSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "renderer_avboit_state.h", avboitStateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "renderer_avboit_state.cpp", avboitStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_private.h", avboitPrivateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.cpp", avboitSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactAvboitStateHeaderStorage = CompactSource(
        AStringView(avboitStateHeaderSource.data(), avboitStateHeaderSource.size())
    );
    const AStringView compactAvboitStateHeader(compactAvboitStateHeaderStorage.data(), compactAvboitStateHeaderStorage.size());
    const AStringView avboitStateHeader(avboitStateHeaderSource.data(), avboitStateHeaderSource.size());
    const AStringView avboitState(avboitStateSource.data(), avboitStateSource.size());
    const AStringView avboitPrivate(avboitPrivateSource.data(), avboitPrivateSource.size());
    const AStringView avboitSystem(avboitSystemSource.data(), avboitSystemSource.size());
    const AStringView pipelineHeader(pipelineHeaderSource.data(), pipelineHeaderSource.size());
    const AStringView rendererCmake(rendererCmakeSource.data(), rendererCmakeSource.size());

    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "classRendererAvboitStatefinal:NoCopy{"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "friendclassRendererAvboitSystem;"));
    EXPECT_FALSE(ContainsText(compactAvboitStateHeader, "friendclassRendererFramePipeline;"));
    EXPECT_FALSE(ContainsText(compactAvboitStateHeader, "friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(compactAvboitStateHeader, "friendclassRendererCsgSystem;"));
    EXPECT_FALSE(ContainsText(compactAvboitStateHeader, "friendclassRendererDeferredSystem;"));
    EXPECT_FALSE(ContainsText(compactAvboitStateHeader, "friendclassRendererRayTracingSystem;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "Core::SamplerHandlem_linearSampler;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "Core::ShaderHandlem_depthWarpComputeShader;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "Core::ShaderHandlem_integrateComputeShader;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "Core::ComputePipelineHandlem_depthWarpPipeline;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "Core::ComputePipelineHandlem_integratePipeline;"));
    EXPECT_TRUE(ContainsText(compactAvboitStateHeader, "boolm_targetsNeedClear=true;"));
    EXPECT_TRUE(ContainsText(avboitState, "void RendererAvboitState::invalidateResources()"));
    EXPECT_TRUE(ContainsText(avboitState, "m_linearSampler.reset();"));
    EXPECT_TRUE(ContainsText(avboitState, "m_depthWarpComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(avboitState, "m_integrateComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(avboitState, "m_depthWarpPipeline.reset();"));
    EXPECT_TRUE(ContainsText(avboitState, "m_integratePipeline.reset();"));
    EXPECT_TRUE(ContainsText(avboitState, "m_targetsNeedClear = true;"));
    EXPECT_FALSE(ContainsText(avboitStateHeader, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "#include <impl/ecs_render/avboit/renderer_avboit_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "RendererAvboitState m_avboitState;"));
    EXPECT_TRUE(ContainsText(avboitPrivate, "#include <impl/ecs_render/avboit/renderer_avboit_state.h>"));
    EXPECT_FALSE(ContainsText(avboitPrivate, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(avboitSystem, "#include <impl/ecs_render/avboit/renderer_avboit_state.h>"));
    EXPECT_FALSE(ContainsText(avboitSystem, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(rendererCmake, "${CMAKE_CURRENT_LIST_DIR}/avboit/renderer_avboit_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmake, "${CMAKE_CURRENT_LIST_DIR}/avboit/renderer_avboit_state.h"));
}


TEST(EcsGraphics, DeferredOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString stateHeaderSource;
    AString stateSystemSource;
    AString deferredSystemSource;
    AString deferredLightingSource;
    AString deferredCompositeSource;
    AString deferredTargetsSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.h", stateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.cpp", stateSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.cpp", deferredSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_composite.cpp", deferredCompositeSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactStateHeaderStorage = CompactSource(AStringView(stateHeaderSource.data(), stateHeaderSource.size()));
    const AStringView compactStateHeader(compactStateHeaderStorage.data(), compactStateHeaderStorage.size());
    const AString compactStateSystemStorage = CompactSource(AStringView(stateSystemSource.data(), stateSystemSource.size()));
    const AStringView compactStateSystem(compactStateSystemStorage.data(), compactStateSystemStorage.size());

    EXPECT_TRUE(ContainsText(compactStateHeader, "classRendererDeferredStatefinal:NoCopy{friendclassRendererDeferredSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererFramePipeline;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererAvboitSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererCsgSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererRayTracingSystem;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_lightingBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_sceneShadingBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_lightBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_lightingComputeShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ComputePipelineHandlem_lightingPipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_compositeComputeBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_compositeComputeShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ComputePipelineHandlem_compositeComputePipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_presentBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::SamplerHandlem_sampler;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_presentPixelShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GraphicsPipelineHandlem_presentPipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u8m_sceneShadingGpuData[sizeof(f32)*NWB_SCENE_SHADING_BUFFER_FLOAT_COUNT]={};"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_sceneShadingGpuDataValid=false;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u8m_lightGpuData[sizeof(f32)*NWB_SCENE_LIGHT_RECORD_FLOAT_COUNT*NWB_SCENE_MAX_LIGHTS]={};"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u32m_lightGpuDataCount=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_lightGpuDataValid=false;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "voidRendererDeferredState::invalidateResources(){"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightingBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_sceneShadingBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightingComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightingPipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_compositeComputeBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_compositeComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_compositeComputePipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_presentBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_sampler.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_presentPixelShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_presentPipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_sceneShadingGpuDataValid=false;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightGpuDataCount=0u;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_lightGpuDataValid=false;"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "shared/renderer_state.h"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "#include <impl/ecs_render/deferred/renderer_deferred_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "RendererDeferredState m_deferredState;"));
    EXPECT_TRUE(ContainsText(deferredSystemSource, "#include <impl/ecs_render/deferred/renderer_deferred_state.h>"));
    EXPECT_TRUE(ContainsText(deferredLightingSource, "#include <impl/ecs_render/deferred/renderer_deferred_state.h>"));
    EXPECT_TRUE(ContainsText(deferredCompositeSource, "#include <impl/ecs_render/deferred/renderer_deferred_state.h>"));
    EXPECT_TRUE(ContainsText(deferredTargetsSource, "#include <impl/ecs_render/deferred/renderer_deferred_state.h>"));
    EXPECT_FALSE(ContainsText(deferredSystemSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(deferredLightingSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(deferredCompositeSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(deferredTargetsSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/deferred/renderer_deferred_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/deferred/renderer_deferred_state.h"));
}


TEST(EcsGraphics, CsgOwnsItsPrivateRendererState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString stateHeaderSource;
    AString stateSystemSource;
    AString csgFrameStateSource;
    AString csgIntervalPeelSource;
    AString csgIntervalResourcesSource;
    AString csgResourcesSource;
    AString csgSystemSource;
    AString pipelineHeaderSource;
    AString rendererCmakeSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "renderer_csg_state.h", stateHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "renderer_csg_state.cpp", stateSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_frame_state.cpp", csgFrameStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_peel.cpp", csgIntervalPeelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_resources.cpp", csgIntervalResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.cpp", csgSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "CMakeLists.txt", rendererCmakeSource));

    const AString compactStateHeaderStorage = CompactSource(AStringView(stateHeaderSource.data(), stateHeaderSource.size()));
    const AStringView compactStateHeader(compactStateHeaderStorage.data(), compactStateHeaderStorage.size());
    const AString compactStateSystemStorage = CompactSource(AStringView(stateSystemSource.data(), stateSystemSource.size()));
    const AStringView compactStateSystem(compactStateSystemStorage.data(), compactStateSystemStorage.size());

    EXPECT_TRUE(ContainsText(compactStateHeader, "structCsgFrameStateCacheSignature{"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u64contentHash=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "u64shapeRegistryRevision=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "classRendererCsgStatefinal:NoCopy{friendclassRendererCsgSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererFramePipeline;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererAvboitSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererDeferredSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererMaterialSystem;"));
    EXPECT_FALSE(ContainsText(compactStateHeader, "friendclassRendererRayTracingSystem;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_clipBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_intervalPeelBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_receiverSpanBuildBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BindingLayoutHandlem_intervalCombineBindingLayout;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_intervalPeelComputeShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_receiverSpanBuildComputeShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_intervalCombineComputeShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ShaderHandlem_intervalCapFillPixelShader;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ComputePipelineHandlem_intervalPeelPipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ComputePipelineHandlem_receiverSpanBuildPipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::ComputePipelineHandlem_intervalCombinePipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GraphicsPipelineHandlem_intervalCapFillPipeline;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_receiverRangeBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_cutterBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_clipContextSlotsBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::BufferHandlem_intervalSampleStateBuffer;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "CsgFrameStateCacheSignaturem_frameStateCacheSignature;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "CsgFrameStatem_frameStateCache;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "usizem_receiverRangeBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "usizem_cutterBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GpuDescriptorHandlem_receiverRangeBufferHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GpuDescriptorHandlem_cutterBufferHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GpuDescriptorHandlem_clipContextSlotsHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "Core::GpuDescriptorHandlem_intervalSampleStateHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "boolm_frameStateCacheValid=false;"));
    EXPECT_TRUE(ContainsText(compactStateHeader, "static_assert(sizeof(RendererCsgState)==328u,"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "voidRendererCsgState::invalidateResources(){"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_clipBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalPeelBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverSpanBuildBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalCombineBindingLayout.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalPeelComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverSpanBuildComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalCombineComputeShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalCapFillPixelShader.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalPeelPipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverSpanBuildPipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalCombinePipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalCapFillPipeline.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverRangeBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_cutterBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_clipContextSlotsBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverRangeBufferHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_cutterBufferHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_clipContextSlotsHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalSampleStateBuffer.reset();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_intervalSampleStateHeapHandle=Core::GpuDescriptorHandle::invalid();"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_frameStateCacheSignature=CsgFrameStateCacheSignature{};"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_frameStateCache=CsgFrameState{};"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_receiverRangeBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_cutterBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactStateSystem, "m_frameStateCacheValid=false;"));
    EXPECT_FALSE(ContainsText(stateHeaderSource, "shared/renderer_state.h"));
    EXPECT_FALSE(ContainsText(stateSystemSource, "shared/renderer_state.h"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_TRUE(ContainsText(pipelineHeaderSource, "RendererCsgState m_csgState;"));
    EXPECT_TRUE(ContainsText(csgFrameStateSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_TRUE(ContainsText(csgIntervalPeelSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_FALSE(ContainsText(csgIntervalPeelSource, "#include <impl/ecs_render/mesh/mesh_system.h>"));
    EXPECT_TRUE(ContainsText(csgIntervalResourcesSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_TRUE(ContainsText(csgResourcesSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_TRUE(ContainsText(csgSystemSource, "#include <impl/ecs_render/csg/renderer_csg_state.h>"));
    EXPECT_FALSE(ContainsText(csgFrameStateSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(csgIntervalPeelSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(csgIntervalResourcesSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(csgResourcesSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_FALSE(ContainsText(csgSystemSource, "#include <impl/ecs_render/shared/renderer_state.h>"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/csg/renderer_csg_state.cpp"));
    EXPECT_TRUE(ContainsText(rendererCmakeSource, "${CMAKE_CURRENT_LIST_DIR}/csg/renderer_csg_state.h"));
}


TEST(EcsGraphics, AvboitDoesNotDependOnDeferredPrivateState){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString avboitHeaderSource;
    AString avboitSystemSource;
    AString avboitResourcesSource;
    AString deferredStateSource;
    AString rootResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.cpp", avboitSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_resources.cpp", avboitResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.h", deferredStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rootResourcesSource));

    const AStringView avboitHeader(avboitHeaderSource.data(), avboitHeaderSource.size());
    const AStringView avboitSystem(avboitSystemSource.data(), avboitSystemSource.size());
    const AStringView avboitResources(avboitResourcesSource.data(), avboitResourcesSource.size());
    const AString compactAvboitHeaderStorage = CompactSource(avboitHeader);
    const AStringView compactAvboitHeader(compactAvboitHeaderStorage.data(), compactAvboitHeaderStorage.size());
    const AString compactDeferredStateStorage = CompactSource(AStringView(deferredStateSource.data(), deferredStateSource.size()));
    const AStringView compactDeferredState(compactDeferredStateStorage.data(), compactDeferredStateStorage.size());
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

    EXPECT_FALSE(ContainsText(compactDeferredState, "friendclassRendererAvboitSystem;"));

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
    AString csgSnapshotHeaderSource;
    AString csgHeaderSource;
    AString csgSystemSource;
    AString csgResourcesSource;
    AString csgIntervalPeelSource;
    AString meshHeaderSource;
    AString frameBindingsSource;
    AString deferredStateSource;
    AString frameTypesSource;
    AString materialDrawSource;
    AString deferredGbufferTaskHeaderSource;
    AString opaqueCsgTaskHeaderSource;
    AString avboitOccupancyTaskHeaderSource;
    AString rootPrefixSource;
    AString rootGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_graph_resource_snapshot.h", csgSnapshotHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.cpp", csgSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_resources.cpp", csgResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_interval_peel.cpp", csgIntervalPeelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_bindings.h", frameBindingsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.h", deferredStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_types.h", frameTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_draw.cpp", materialDrawSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_gbuffer_task.h", deferredGbufferTaskHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_opaque_interval_tasks.h", opaqueCsgTaskHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_occupancy_tasks.h", avboitOccupancyTaskHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", rootPrefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));

    const AString compactCsgSnapshotHeaderStorage = CompactSource(AStringView(csgSnapshotHeaderSource.data(), csgSnapshotHeaderSource.size()));
    const AStringView compactCsgSnapshotHeader(compactCsgSnapshotHeaderStorage.data(), compactCsgSnapshotHeaderStorage.size());
    const AStringView csgHeader(csgHeaderSource.data(), csgHeaderSource.size());
    const AStringView csgSystem(csgSystemSource.data(), csgSystemSource.size());
    const AStringView csgResources(csgResourcesSource.data(), csgResourcesSource.size());
    const AString compactCsgHeaderStorage = CompactSource(csgHeader);
    const AStringView compactCsgHeader(compactCsgHeaderStorage.data(), compactCsgHeaderStorage.size());
    const AString compactCsgResourcesStorage = CompactSource(csgResources);
    const AStringView compactCsgResources(compactCsgResourcesStorage.data(), compactCsgResourcesStorage.size());
    const AString compactCsgIntervalPeelStorage = CompactSource(AStringView(csgIntervalPeelSource.data(), csgIntervalPeelSource.size()));
    const AStringView compactCsgIntervalPeel(compactCsgIntervalPeelStorage.data(), compactCsgIntervalPeelStorage.size());
    const AString compactMeshHeaderStorage = CompactSource(AStringView(meshHeaderSource.data(), meshHeaderSource.size()));
    const AStringView compactMeshHeader(compactMeshHeaderStorage.data(), compactMeshHeaderStorage.size());
    const AString compactFrameBindingsStorage = CompactSource(
        AStringView(frameBindingsSource.data(), frameBindingsSource.size())
    );
    const AStringView compactFrameBindings(compactFrameBindingsStorage.data(), compactFrameBindingsStorage.size());
    const AString compactDeferredStateStorage = CompactSource(AStringView(deferredStateSource.data(), deferredStateSource.size()));
    const AStringView compactDeferredState(compactDeferredStateStorage.data(), compactDeferredStateStorage.size());
    const AString compactFrameTypesStorage = CompactSource(AStringView(frameTypesSource.data(), frameTypesSource.size()));
    const AStringView compactFrameTypes(compactFrameTypesStorage.data(), compactFrameTypesStorage.size());
    const AString compactMaterialDrawStorage = CompactSource(AStringView(materialDrawSource.data(), materialDrawSource.size()));
    const AStringView compactMaterialDraw(compactMaterialDrawStorage.data(), compactMaterialDrawStorage.size());
    const AString compactDeferredGbufferTaskHeaderStorage = CompactSource(
        AStringView(deferredGbufferTaskHeaderSource.data(), deferredGbufferTaskHeaderSource.size())
    );
    const AStringView compactDeferredGbufferTaskHeader(
        compactDeferredGbufferTaskHeaderStorage.data(),
        compactDeferredGbufferTaskHeaderStorage.size()
    );
    const AString compactOpaqueCsgTaskHeaderStorage = CompactSource(AStringView(opaqueCsgTaskHeaderSource.data(), opaqueCsgTaskHeaderSource.size()));
    const AStringView compactOpaqueCsgTaskHeader(compactOpaqueCsgTaskHeaderStorage.data(), compactOpaqueCsgTaskHeaderStorage.size());
    const AString compactAvboitOccupancyTaskHeaderStorage = CompactSource(
        AStringView(avboitOccupancyTaskHeaderSource.data(), avboitOccupancyTaskHeaderSource.size())
    );
    const AStringView compactAvboitOccupancyTaskHeader(
        compactAvboitOccupancyTaskHeaderStorage.data(),
        compactAvboitOccupancyTaskHeaderStorage.size()
    );
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
            "RendererCsgState&",
            "RendererShaderSystem&",
            "RendererMeshSystem&",
        }
    ));
    EXPECT_FALSE(ContainsText(csgHeader, "RendererDeferredState"));
    EXPECT_FALSE(ContainsText(csgHeader, "m_deferredState"));
    EXPECT_FALSE(ContainsText(csgSystem, "m_deferredState"));
    EXPECT_FALSE(ContainsText(csgResources, "m_deferredState"));
    EXPECT_FALSE(ContainsText(csgHeader, "RendererDrawState"));
    EXPECT_FALSE(ContainsText(csgSystem, "RendererDrawState"));
    EXPECT_FALSE(ContainsText(csgResources, "m_drawState"));
    EXPECT_FALSE(ContainsText(compactCsgIntervalPeel, "m_drawState"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "structCsgGraphResourceSnapshot{"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::BufferHandlereceiverRanges;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::BufferHandlecutters;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::BufferHandleclipContextSlots;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::BufferHandleintervalSampleState;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "usizereceiverRangeCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "usizecutterCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::GpuDescriptorHandlereceiverRangeHeapHandle="));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::GpuDescriptorHandlecutterHeapHandle="));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::GpuDescriptorHandleclipContextSlotsHeapHandle="));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "Core::GpuDescriptorHandleintervalSampleStateHeapHandle="));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "frameReady(constCsgFrameGpuData&csgFrameData)constnoexcept;"));
    EXPECT_TRUE(ContainsText(compactCsgSnapshotHeader, "findClipContextHeapSlot(u32&outHeapSlot)constnoexcept;"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "csgGraphResourceSnapshot()const;"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "structMeshFrameBindingSnapshot{"));
    EXPECT_TRUE(ContainsText(compactMeshHeader, "meshFrameBindingSnapshot()const;"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "constECSRenderDetail::MeshFrameBindingSnapshot&frameBindings"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "prepareCsgClipContextSlotData(constDeferredFrameTargets&targets,"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "setCsgReceiverSurfaceImageStates(Core::CommandList&commandList,constDeferredFrameTargets&targets);"));
    EXPECT_TRUE(ContainsText(compactCsgHeader, "setCsgIntervalSampleImageStates(Core::CommandList&commandList,constDeferredFrameTargets&targets);"));
    EXPECT_TRUE(ContainsText(compactCsgResources, "targets.bindless.slotsBufferDescriptor.slot()"));
    EXPECT_FALSE(ContainsText(compactCsgResources, "createMeshFrameHeapHandles()"));
    EXPECT_FALSE(ContainsText(compactCsgResources, "meshFrameHeapHandlesReady()"));

    EXPECT_FALSE(ContainsText(compactDeferredState, "friendclassRendererCsgSystem;"));

    EXPECT_TRUE(ContainsText(compactFrameTypes, "structMaterialPassDrawContext{Core::CommandList&commandList;constDeferredFrameTargets&deferredTargets;"));
    EXPECT_TRUE(ContainsText(compactFrameTypes, "constECSRenderDetail::MeshFrameBindingSnapshot&frameBindings;"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "context.deferredTargets"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "setCsgReceiverSurfaceImageStates(commandList,deferredTargets)"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "setCsgIntervalSampleImageStates(commandList,deferredTargets)"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "boolRendererMaterialSystem::setMaterialPassDrawPushConstants("));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "NWB_ASSERT(csgContextHeapSlotReady);"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "if(!csgContextHeapSlotReady)returnfalse;"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "if(!frameHeapSlotsReady)returnfalse;"));
    EXPECT_EQ(CountText(compactMaterialDraw, "if(!setMaterialPassDrawPushConstants(context,drawItem,mesh))"), 2u);
    EXPECT_TRUE(ContainsText(compactDeferredGbufferTaskHeader, "MeshFrameBindingSnapshotframeBindings;"));
    EXPECT_TRUE(ContainsText(compactOpaqueCsgTaskHeader, "MeshFrameBindingSnapshotframeBindings;"));
    EXPECT_TRUE(ContainsText(compactAvboitOccupancyTaskHeader, "MeshFrameBindingSnapshotframeBindings;"));
    EXPECT_EQ(CountText(compactRootGraph, "m_meshSystem.meshFrameBindingSnapshot()"), 1u);
    EXPECT_FALSE(ContainsText(compactRootGraph, "m_meshSystem.meshViewBufferSnapshot()"));
    EXPECT_FALSE(ContainsText(compactRootGraph, "m_materialSystem.materialPassBufferSnapshot()"));
    EXPECT_TRUE(ContainsText(compactRootGraph, "meshViewBufferSnapshot=frameBindings.meshView;"));
    EXPECT_TRUE(ContainsText(compactRootGraph, "importBuffer(frameBindings.instanceBuffer,"));
    EXPECT_TRUE(ContainsText(compactRootGraph, "importBuffer(frameBindings.materialTypedBuffer,"));
    EXPECT_TRUE(ContainsText(compactRootPrefix, "gbufferPayload.frameBindings=frameBindings;"));
    EXPECT_TRUE(ContainsText(compactRootPrefix, "csgIntervalSamplePayload.frameBindings=frameBindings;"));
    EXPECT_TRUE(ContainsText(compactRootGraph, "avboitPrePayload.frameBindings=frameBindings;"));
    EXPECT_EQ(CountText(compactRootGraph, "m_csgSystem.csgGraphResourceSnapshot()"), 1u);
    EXPECT_EQ(CountText(compactRootPrefix, "m_csgSystem.csgGraphResourceSnapshot()"), 0u);
    EXPECT_TRUE(ContainsText(
        compactRootGraph,
        "constECSRenderDetail::CsgGraphResourceSnapshotcsgResources=m_csgSystem.csgGraphResourceSnapshot();"
    ));
    EXPECT_TRUE(ContainsText(compactRootPrefix, "constECSRenderDetail::CsgGraphResourceSnapshot&csgResources"));
    EXPECT_EQ(CountText(compactRootPrefix, "prepareCsgClipContextSlotData(deferredTargets,"), 1u);
    EXPECT_EQ(CountText(compactRootGraph, "prepareCsgClipContextSlotData(deferredTargets,"), 4u);
    EXPECT_FALSE(ContainsText(compactCsgHeader, "CsgGraphResourceBuffers"));
    EXPECT_FALSE(ContainsText(compactCsgResources, "CsgGraphResourceBuffers"));
    EXPECT_FALSE(ContainsText(compactCsgHeader, "csgFrameBuffersReady"));
    EXPECT_FALSE(ContainsText(compactCsgHeader, "populateCsgGraphResourceBuffers"));
    EXPECT_FALSE(ContainsText(compactCsgHeader, "findCsgClipContextHeapSlot"));

    const usize frameReadyBegin = compactCsgResources.find("ECSRenderDetail::CsgGraphResourceSnapshot::frameReady(");
    ASSERT_NE(frameReadyBegin, AStringView::npos);
    const usize frameReadyEnd = compactCsgResources.find(
        "ECSRenderDetail::CsgGraphResourceSnapshot::findClipContextHeapSlot(",
        frameReadyBegin
    );
    ASSERT_NE(frameReadyEnd, AStringView::npos);
    const AStringView frameReady = compactCsgResources.substr(frameReadyBegin, frameReadyEnd - frameReadyBegin);
    EXPECT_TRUE(ContainsText(frameReady, "bindingValid()"));
    EXPECT_TRUE(ContainsText(frameReady, "receiverRangeCapacity>=csgFrameData.receiverRanges.size()"));
    EXPECT_TRUE(ContainsText(frameReady, "cutterCapacity>=csgFrameData.cutters.size()"));
    EXPECT_FALSE(ContainsText(frameReady, "m_csgState"));
    EXPECT_FALSE(ContainsText(frameReady, "m_meshSystem"));
}


TEST(EcsGraphics, GraphMaterialRecordingUsesCapturedMeshFrameBindingGeneration){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString meshHeaderSource;
    AString meshBindingsSource;
    AString frameBindingsSource;
    AString frameTypesSource;
    AString materialResourcesSource;
    AString materialPassSource;
    AString materialDrawSource;
    AString avboitHeaderSource;
    AString avboitPassSource;
    AString rootResourcesSource;
    AString prefixSource;
    AString rootGraphSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_bindings.cpp", meshBindingsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_bindings.h", frameBindingsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_types.h", frameTypesSource));
    ASSERT_TRUE(ReadTextFile(
        repoRoot / "impl" / "ecs_render" / "material" / "material_pass_resources.cpp",
        materialResourcesSource
    ));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass.cpp", materialPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_pass_draw.cpp", materialDrawSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_system.h", avboitHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "avboit" / "avboit_pass.cpp", avboitPassSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rootResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", prefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));

    const AString compactMeshHeaderStorage = CompactSource(AStringView(meshHeaderSource.data(), meshHeaderSource.size()));
    const AStringView compactMeshHeader(compactMeshHeaderStorage.data(), compactMeshHeaderStorage.size());
    const AString compactMeshBindingsStorage = CompactSource(AStringView(meshBindingsSource.data(), meshBindingsSource.size()));
    const AStringView compactMeshBindings(compactMeshBindingsStorage.data(), compactMeshBindingsStorage.size());
    const AString compactFrameBindingsStorage = CompactSource(
        AStringView(frameBindingsSource.data(), frameBindingsSource.size())
    );
    const AStringView compactFrameBindings(compactFrameBindingsStorage.data(), compactFrameBindingsStorage.size());
    const AString compactFrameTypesStorage = CompactSource(AStringView(frameTypesSource.data(), frameTypesSource.size()));
    const AStringView compactFrameTypes(compactFrameTypesStorage.data(), compactFrameTypesStorage.size());
    const AString compactMaterialResourcesStorage = CompactSource(
        AStringView(materialResourcesSource.data(), materialResourcesSource.size())
    );
    const AStringView compactMaterialResources(compactMaterialResourcesStorage.data(), compactMaterialResourcesStorage.size());
    const AString compactMaterialPassStorage = CompactSource(AStringView(materialPassSource.data(), materialPassSource.size()));
    const AStringView compactMaterialPass(compactMaterialPassStorage.data(), compactMaterialPassStorage.size());
    const AString compactMaterialDrawStorage = CompactSource(AStringView(materialDrawSource.data(), materialDrawSource.size()));
    const AStringView compactMaterialDraw(compactMaterialDrawStorage.data(), compactMaterialDrawStorage.size());
    const AString compactAvboitHeaderStorage = CompactSource(AStringView(avboitHeaderSource.data(), avboitHeaderSource.size()));
    const AStringView compactAvboitHeader(compactAvboitHeaderStorage.data(), compactAvboitHeaderStorage.size());
    const AString compactAvboitPassStorage = CompactSource(AStringView(avboitPassSource.data(), avboitPassSource.size()));
    const AStringView compactAvboitPass(compactAvboitPassStorage.data(), compactAvboitPassStorage.size());
    const AString compactRootResourcesStorage = CompactSource(
        AStringView(rootResourcesSource.data(), rootResourcesSource.size())
    );
    const AStringView compactRootResources(compactRootResourcesStorage.data(), compactRootResourcesStorage.size());
    const AString compactPrefixStorage = CompactSource(AStringView(prefixSource.data(), prefixSource.size()));
    const AStringView compactPrefix(compactPrefixStorage.data(), compactPrefixStorage.size());
    const AString compactRootGraphStorage = CompactSource(AStringView(rootGraphSource.data(), rootGraphSource.size()));
    const AStringView compactRootGraph(compactRootGraphStorage.data(), compactRootGraphStorage.size());

    EXPECT_TRUE(ContainsText(compactFrameBindings, "usizeinstanceBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "usizematerialTypedBufferCapacity=0u;"));
    EXPECT_TRUE(ContainsText(
        compactFrameBindings,
        "frameReady(constusizeinstanceCount,constusizematerialTypedByteCount)constnoexcept{"
    ));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "instanceBufferCapacity>=instanceCount"));
    EXPECT_TRUE(ContainsText(
        compactFrameBindings,
        "materialTypedBufferCapacity>=Max<usize>(materialTypedByteCount,sizeof(u32))"
    ));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "boolmatches(constMaterialPassBufferSnapshot&materialBuffers,"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "instanceBuffer==materialBuffers.instanceBuffer"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "materialTypedBuffer==materialBuffers.materialTypedBuffer"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "instanceBufferCapacity==materialBuffers.instanceBufferCapacity"));
    EXPECT_TRUE(ContainsText(compactFrameBindings, "materialTypedBufferCapacity==materialBuffers.materialTypedBufferCapacity"));
    EXPECT_TRUE(ContainsText(
        compactMeshHeader,
        "prepareMeshFrameBindings(constECSRenderDetail::MaterialPassBufferSnapshot&materialBuffers);"
    ));
    EXPECT_TRUE(ContainsText(
        compactMeshBindings,
        ".instanceBufferCapacity=materialBuffers.instanceBufferCapacity,"
    ));
    EXPECT_TRUE(ContainsText(
        compactMeshBindings,
        ".materialTypedBufferCapacity=materialBuffers.materialTypedBufferCapacity,"
    ));
    EXPECT_TRUE(ContainsText(
        compactFrameTypes,
        "constECSRenderDetail::MeshFrameBindingSnapshot&frameBindings;"
    ));
    EXPECT_FALSE(ContainsText(compactFrameTypes, "MeshFrameBindingSnapshot*frameBindings"));

    const usize failedRegistration = compactMeshBindings.find("if(!registered){");
    const usize retainedGeneration = compactMeshBindings.find(
        "MeshFrameBindingSnapshotretired=Move(m_meshState.m_frameBindings);"
    );
    const usize publishedGeneration = compactMeshBindings.find("m_meshState.m_frameBindings=Move(replacement);");
    const usize retiredInstanceRelease = compactMeshBindings.find("heap.free(retired.instanceHeapHandle)", publishedGeneration);
    ASSERT_NE(failedRegistration, AStringView::npos);
    ASSERT_NE(retainedGeneration, AStringView::npos);
    ASSERT_NE(publishedGeneration, AStringView::npos);
    ASSERT_NE(retiredInstanceRelease, AStringView::npos);
    EXPECT_LT(failedRegistration, retainedGeneration);
    EXPECT_LT(retainedGeneration, publishedGeneration);
    EXPECT_LT(publishedGeneration, retiredInstanceRelease);
    EXPECT_TRUE(ContainsText(
        compactMeshBindings,
        "if(m_meshState.m_frameBindings.matches(materialBuffers,m_meshState.m_meshViewBuffer))returntrue;"
    ));
    EXPECT_TRUE(ContainsText(compactMeshBindings, "snapshot.meshView=meshViewBufferSnapshot();returnsnapshot;"));

    EXPECT_TRUE(ContainsText(compactMaterialPass, "frameBindings.frameReady(instanceCount,materialTypedByteCount)"));
    EXPECT_TRUE(ContainsText(
        compactMaterialPass,
        "materialPassDrawResourcesReady(drawItems.regular,frameBindings)"
    ));
    EXPECT_TRUE(ContainsText(compactMaterialPass, "nullptr,frameBindings"));
    EXPECT_TRUE(ContainsText(compactMaterialPass, "&csgResources,frameBindings"));
    EXPECT_TRUE(ContainsText(compactMaterialDraw, "if(!context.frameBindings.bindingValid())returnfalse;"));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "outSlots.instance=context.frameBindings.instanceHeapHandle.slot();"
    ));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "outSlots.materialTyped=context.frameBindings.materialTypedHeapHandle.slot();"
    ));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "outSlots.view=context.frameBindings.meshView.heapHandle.slot();"
    ));
    EXPECT_FALSE(ContainsText(compactMaterialDraw, "meshFrameHeapHandlesReady"));
    EXPECT_FALSE(ContainsText(compactMaterialDraw, "context.frameBindings->"));
    EXPECT_FALSE(ContainsText(compactMaterialDraw, "m_drawState"));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "setBufferState(context.frameBindings.instanceBuffer.get(),Core::ResourceStates::ShaderResource)"
    ));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "setBufferState(context.frameBindings.meshView.buffer.get(),Core::ResourceStates::ConstantBuffer)"
    ));
    EXPECT_TRUE(ContainsText(
        compactMaterialDraw,
        "setBufferState(context.frameBindings.materialTypedBuffer.get(),Core::ResourceStates::ShaderResource)"
    ));
    EXPECT_FALSE(ContainsText(compactMaterialResources, "createMeshFrameHeapHandles"));
    EXPECT_FALSE(ContainsText(compactMaterialResources, "releaseMeshFrameHeapHandles"));
    EXPECT_EQ(CountText(compactRootResources, "m_materialSystem.materialPassBufferSnapshot()"), 1u);
    EXPECT_EQ(CountText(compactRootResources, "m_meshSystem.prepareMeshFrameBindings(materialBuffers)"), 1u);
    const usize transparentPreparation = compactRootResources.find("m_avboitSystem.prepareAvboitPassResources(");
    const usize materialSnapshot = compactRootResources.find("m_materialSystem.materialPassBufferSnapshot()");
    const usize meshPublication = compactRootResources.find("m_meshSystem.prepareMeshFrameBindings(materialBuffers)");
    const usize csgPreparation = compactRootResources.find("m_csgSystem.prepareCsgFrameResources(");
    ASSERT_NE(transparentPreparation, AStringView::npos);
    ASSERT_NE(materialSnapshot, AStringView::npos);
    ASSERT_NE(meshPublication, AStringView::npos);
    ASSERT_NE(csgPreparation, AStringView::npos);
    EXPECT_LT(transparentPreparation, materialSnapshot);
    EXPECT_LT(materialSnapshot, meshPublication);
    EXPECT_LT(meshPublication, csgPreparation);
    EXPECT_TRUE(ContainsText(compactRootGraph, "!meshViewBufferSnapshot.valid()"));
    EXPECT_TRUE(ContainsText(compactRootGraph, "(!csgFrameState.empty()&&!frameBindings.bindingValid())"));

    EXPECT_TRUE(ContainsText(
        compactAvboitHeader,
        "constECSRenderDetail::MeshFrameBindingSnapshot*preparedOccupancyFrameBindings=nullptr"
    ));
    EXPECT_TRUE(ContainsText(
        compactAvboitHeader,
        "constECSRenderDetail::MeshFrameBindingSnapshot*preparedExtinctionFrameBindings=nullptr"
    ));
    EXPECT_TRUE(ContainsText(
        compactAvboitHeader,
        "constECSRenderDetail::MeshFrameBindingSnapshot*preparedAccumulationFrameBindings=nullptr"
    ));
    EXPECT_TRUE(ContainsText(compactAvboitPass, "NWB_ASSERT(preparedOccupancyFrameBindings);"));
    EXPECT_TRUE(ContainsText(compactAvboitPass, "NWB_ASSERT(preparedExtinctionFrameBindings);"));
    EXPECT_TRUE(ContainsText(compactAvboitPass, "NWB_ASSERT(preparedAccumulationFrameBindings);"));
    EXPECT_TRUE(ContainsText(avboitPassSource, "#include <impl/ecs_render/shared/renderer_frame_bindings.h>"));
    EXPECT_FALSE(ContainsText(avboitPassSource, "#include <impl/ecs_render/mesh/mesh_system.h>"));
    EXPECT_EQ(CountText(compactAvboitPass, "*preparedOccupancyFrameBindings,"), 1u);
    EXPECT_EQ(CountText(compactAvboitPass, "*preparedExtinctionFrameBindings,"), 1u);
    EXPECT_EQ(CountText(compactAvboitPass, "*preparedAccumulationFrameBindings,"), 1u);

    const TestPath taskHeaderPaths[] = {
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_gbuffer_task.h",
        repoRoot / "impl" / "ecs_render" / "material" / "task_graph_opaque_compute_tasks.h",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_opaque_compute_tasks.h",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_opaque_interval_tasks.h",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_transparent_interval_tasks.h",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_occupancy_tasks.h",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_extinction_integration_tasks.h",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_accumulation_tasks.h",
    };
    usize payloadFrameBindingCount = 0u;
    for(const TestPath& taskHeaderPath : taskHeaderPaths){
        AString taskHeaderSource;
        ASSERT_TRUE(ReadTextFile(taskHeaderPath, taskHeaderSource));
        const AStringView taskHeader(taskHeaderSource.data(), taskHeaderSource.size());
        const AString compactTaskHeaderStorage = CompactSource(
            AStringView(taskHeaderSource.data(), taskHeaderSource.size())
        );
        const AStringView compactTaskHeader(compactTaskHeaderStorage.data(), compactTaskHeaderStorage.size());
        EXPECT_TRUE(ContainsText(taskHeader, "#include <impl/ecs_render/shared/renderer_frame_bindings.h>"));
        EXPECT_FALSE(ContainsText(taskHeader, "#include <impl/ecs_render/mesh/mesh_system.h>"));
        payloadFrameBindingCount += CountText(compactTaskHeader, "MeshFrameBindingSnapshotframeBindings;");
    }
    EXPECT_EQ(payloadFrameBindingCount, 20u);

    const TestPath taskSourcePaths[] = {
        repoRoot / "impl" / "ecs_render" / "deferred" / "task_graph_gbuffer_task.cpp",
        repoRoot / "impl" / "ecs_render" / "material" / "task_graph_opaque_compute_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_opaque_compute_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_opaque_interval_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "csg" / "task_graph_transparent_interval_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_occupancy_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_extinction_integration_tasks.cpp",
        repoRoot / "impl" / "ecs_render" / "avboit" / "task_graph_accumulation_tasks.cpp",
    };
    usize capturedReadinessCount = 0u;
    usize liveReadinessCount = 0u;
    usize capturedContextCount = 0u;
    for(const TestPath& taskSourcePath : taskSourcePaths){
        AString taskSource;
        ASSERT_TRUE(ReadTextFile(taskSourcePath, taskSource));
        const AString compactTaskSourceStorage = CompactSource(AStringView(taskSource.data(), taskSource.size()));
        const AStringView compactTaskSource(compactTaskSourceStorage.data(), compactTaskSourceStorage.size());
        capturedReadinessCount += CountText(compactTaskSource, "frameBindings.frameReady(");
        liveReadinessCount += CountText(compactTaskSource, "materialPassDrawBuffersReady(");
        capturedContextCount += CountText(compactTaskSource, "payload.frameBindings};");
    }
    EXPECT_EQ(capturedReadinessCount, 16u);
    EXPECT_EQ(liveReadinessCount, 0u);
    EXPECT_EQ(capturedContextCount, 13u);

    EXPECT_EQ(CountText(compactPrefix, ".frameBindings=frameBindings;"), 8u);
    EXPECT_EQ(CountText(compactRootGraph, ".frameBindings=frameBindings;"), 12u);
    EXPECT_EQ(CountText(compactPrefix, "frameBindings.frameReady("), 1u);
    EXPECT_EQ(CountText(compactRootGraph, "frameBindings.frameReady("), 4u);
    EXPECT_EQ(CountText(compactPrefix, "materialPassDrawBuffersReady("), 0u);
    EXPECT_EQ(CountText(compactRootGraph, "materialPassDrawBuffersReady("), 0u);
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
    AString csgStateSource;
    AString meshStateSource;
    AString materialStateSource;
    AString rootResourcesSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.h", meshHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "mesh_system.cpp", meshSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.h", materialHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "material_system.cpp", materialSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.h", csgHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "csg_system.cpp", csgSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "csg" / "renderer_csg_state.h", csgStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "mesh" / "renderer_mesh_state.h", meshStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "material" / "renderer_material_state.h", materialStateSource));
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
    const AString compactCsgStateStorage = CompactSource(AStringView(csgStateSource.data(), csgStateSource.size()));
    const AStringView compactCsgState(compactCsgStateStorage.data(), compactCsgStateStorage.size());
    const AString compactMeshStateStorage = CompactSource(AStringView(meshStateSource.data(), meshStateSource.size()));
    const AStringView compactMeshState(compactMeshStateStorage.data(), compactMeshStateStorage.size());
    const AString compactMaterialStateStorage = CompactSource(AStringView(materialStateSource.data(), materialStateSource.size()));
    const AStringView compactMaterialState(compactMaterialStateStorage.data(), compactMaterialStateStorage.size());
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

    EXPECT_TRUE(ContainsText(compactMeshState, "private:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactMaterialState, "private:voidinvalidateResources();"));
    EXPECT_TRUE(ContainsText(compactCsgState, "private:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(compactMeshState, "public:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(compactMaterialState, "public:voidinvalidateResources();"));
    EXPECT_FALSE(ContainsText(compactCsgState, "public:voidinvalidateResources();"));

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
    const usize csgInvalidation = compactRootResources.find("m_csgSystem.invalidateResources()");
    const usize deferredInvalidation = compactRootResources.find("m_deferredSystem.invalidateResources()");
    ASSERT_NE(rayTracingInvalidation, AStringView::npos);
    ASSERT_NE(avboitInvalidation, AStringView::npos);
    ASSERT_NE(shaderInvalidation, AStringView::npos);
    ASSERT_NE(meshInvalidation, AStringView::npos);
    ASSERT_NE(materialInvalidation, AStringView::npos);
    ASSERT_NE(csgInvalidation, AStringView::npos);
    ASSERT_NE(deferredInvalidation, AStringView::npos);
    EXPECT_LT(rayTracingInvalidation, meshInvalidation);
    EXPECT_LT(avboitInvalidation, materialInvalidation);
    EXPECT_LT(meshInvalidation, materialInvalidation);
    EXPECT_FALSE(ContainsText(compactRootResources, "m_drawState"));
}


TEST(EcsGraphics, RootMediatesDeferredRayTracingLightingClassification){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString frameTypesSource;
    AString deferredHeaderSource;
    AString deferredSystemSource;
    AString deferredLightingSource;
    AString rayTracingHeaderSource;
    AString rayTracingSystemSource;
    AString rendererStateSource;
    AString rootPrefixSource;
    AString rootGraphSource;
    AString rootExecuteSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_types.h", frameTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.cpp", deferredSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_lighting.cpp", deferredLightingSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "renderer_raytracing_state.h", rendererStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graphics_prefix.cpp", rootPrefixSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", rootExecuteSource));

    const AString compactFrameTypesStorage = CompactSource(AStringView(frameTypesSource.data(), frameTypesSource.size()));
    const AStringView compactFrameTypes(compactFrameTypesStorage.data(), compactFrameTypesStorage.size());
    const AString compactDeferredHeaderStorage = CompactSource(AStringView(deferredHeaderSource.data(), deferredHeaderSource.size()));
    const AStringView compactDeferredHeader(compactDeferredHeaderStorage.data(), compactDeferredHeaderStorage.size());
    const AStringView deferredHeader(deferredHeaderSource.data(), deferredHeaderSource.size());
    const AStringView deferredSystem(deferredSystemSource.data(), deferredSystemSource.size());
    const AStringView deferredLighting(deferredLightingSource.data(), deferredLightingSource.size());
    const AString compactRayTracingHeaderStorage = CompactSource(AStringView(rayTracingHeaderSource.data(), rayTracingHeaderSource.size()));
    const AStringView compactRayTracingHeader(compactRayTracingHeaderStorage.data(), compactRayTracingHeaderStorage.size());
    const AString compactRayTracingSystemStorage = CompactSource(AStringView(rayTracingSystemSource.data(), rayTracingSystemSource.size()));
    const AStringView compactRayTracingSystem(compactRayTracingSystemStorage.data(), compactRayTracingSystemStorage.size());
    const AStringView rendererState(rendererStateSource.data(), rendererStateSource.size());
    const AString compactRootPrefixStorage = CompactSource(AStringView(rootPrefixSource.data(), rootPrefixSource.size()));
    const AStringView compactRootPrefix(compactRootPrefixStorage.data(), compactRootPrefixStorage.size());
    const AString compactRootGraphStorage = CompactSource(AStringView(rootGraphSource.data(), rootGraphSource.size()));
    const AStringView compactRootGraph(compactRootGraphStorage.data(), compactRootGraphStorage.size());
    const AStringView rootExecute(rootExecuteSource.data(), rootExecuteSource.size());

    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactDeferredHeader,
        "RendererDeferredSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::ECS::World&",
            "Core::Graphics&",
            "RendererDeferredState&",
            "RendererShaderSystem&",
        }
    ));
    EXPECT_FALSE(ContainsText(deferredHeader, "RendererRayTracingState"));
    EXPECT_FALSE(ContainsText(deferredHeader, "m_rayTracingState"));
    EXPECT_FALSE(ContainsText(deferredSystem, "m_rayTracingState"));
    EXPECT_FALSE(ContainsText(deferredLighting, "m_rayTracingState"));
    EXPECT_TRUE(ContainsText(compactFrameTypes, "structRayTracingLightingClassificationInput{u32refractiveInstanceCount=0u;};"));
    EXPECT_TRUE(ContainsText(compactFrameTypes, "structRayTracingLightingClassification{u32causticLightCount=0u;u32softShadowSlotMask=0u;};"));
    EXPECT_TRUE(ContainsText(compactDeferredHeader, "constRayTracingLightingClassificationInput&rayTracingInput"));
    EXPECT_TRUE(ContainsText(compactDeferredHeader, "RayTracingLightingClassification&outRayTracingClassification"));

    const usize inputSnapshot = compactRootPrefix.find("m_raytracingSystem.snapshotLightingClassificationInput()");
    const usize deferredClassification = compactRootPrefix.find("m_deferredSystem.prepareSceneShadingBufferUploads(");
    const usize graphicsPrefixAcceptance = compactRootPrefix.find("if(!m_graphicsPrefixTask.valid())");
    const usize classificationPublication = compactRootPrefix.find("m_raytracingSystem.publishPreparedLightingClassification(");
    ASSERT_NE(inputSnapshot, AStringView::npos);
    ASSERT_NE(deferredClassification, AStringView::npos);
    ASSERT_NE(graphicsPrefixAcceptance, AStringView::npos);
    ASSERT_NE(classificationPublication, AStringView::npos);
    EXPECT_LT(inputSnapshot, deferredClassification);
    EXPECT_LT(deferredClassification, graphicsPrefixAcceptance);
    EXPECT_LT(graphicsPrefixAcceptance, classificationPublication);

    EXPECT_TRUE(ContainsText(compactRayTracingSystem, "m_rayTracingState.m_causticLightCount=classification.causticLightCount;"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, "m_rayTracingState.m_softShadowSlotMask=classification.softShadowSlotMask;"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, ".softShadowSlotMask=m_rayTracingState.m_softShadowSlotMask"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, ".causticLightCount=m_rayTracingState.m_causticLightCount"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, ".causticEmissionGateLogged=m_rayTracingState.m_causticEmissionGateLogged"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, "m_rayTracingState.m_softShadowSlotMask=snapshot.softShadowSlotMask;"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, "m_rayTracingState.m_causticLightCount=snapshot.causticLightCount;"));
    EXPECT_TRUE(ContainsText(compactRayTracingSystem, "m_rayTracingState.m_causticEmissionGateLogged=snapshot.causticEmissionGateLogged;"));
    EXPECT_FALSE(ContainsText(compactRayTracingHeader, "shadowSlotCount"));
    EXPECT_FALSE(ContainsText(rendererState, "m_shadowSlotCount"));
    EXPECT_FALSE(ContainsText(deferredLighting, "shadowSlotCount"));
    EXPECT_TRUE(ContainsText(rootExecute, "m_raytracingSystem.restorePreparedLightingCpuState(rayTracingCpuState);"));

    const usize graphicsPrefixDeclaration = compactRootGraph.find("if(!declareDeferredGraphicsPrefixTasks(");
    const usize shadowPlanSnapshot = compactRootGraph.find("m_raytracingSystem.snapshotShadowVisibilityGraphPlan(");
    const usize shadowVisibilityDeclaration = compactRootGraph.find("if(!declareDeferredShadowVisibilityTask(");
    ASSERT_NE(graphicsPrefixDeclaration, AStringView::npos);
    ASSERT_NE(shadowPlanSnapshot, AStringView::npos);
    ASSERT_NE(shadowVisibilityDeclaration, AStringView::npos);
    EXPECT_LT(graphicsPrefixDeclaration, shadowPlanSnapshot);
    EXPECT_LT(shadowPlanSnapshot, shadowVisibilityDeclaration);
}


TEST(EcsGraphics, RootFreezesDeferredLightingResourcesForRayTracingTasks){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString frameTypesSource;
    AString deferredHeaderSource;
    AString rayTracingHeaderSource;
    AString rayTracingSystemSource;
    AString rayTracingShadowSource;
    AString rayTracingSoftShadowSource;
    AString rayTracingCausticsSource;
    AString rayTracingSurfelSource;
    AString deferredStateSource;
    AString rootGraphSource;
    AString rootShadowSource;
    AString rootCausticsSource;
    AString rootSurfelSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "shared" / "renderer_frame_types.h", frameTypesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.h", rayTracingHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "raytracing_system.cpp", rayTracingSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_shadow.cpp", rayTracingShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_softshadow.cpp", rayTracingSoftShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_caustics.cpp", rayTracingCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "raytrace" / "rt_surfel_gi.cpp", rayTracingSurfelSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.h", deferredStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph.cpp", rootGraphSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_shadow_visibility.cpp", rootShadowSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_caustics.cpp", rootCausticsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_graph_surfel_gi.cpp", rootSurfelSource));

    const AString compactFrameTypesStorage = CompactSource(AStringView(frameTypesSource.data(), frameTypesSource.size()));
    const AStringView compactFrameTypes(compactFrameTypesStorage.data(), compactFrameTypesStorage.size());
    const AString compactDeferredHeaderStorage = CompactSource(AStringView(deferredHeaderSource.data(), deferredHeaderSource.size()));
    const AStringView compactDeferredHeader(compactDeferredHeaderStorage.data(), compactDeferredHeaderStorage.size());
    const AString compactRayTracingHeaderStorage = CompactSource(AStringView(rayTracingHeaderSource.data(), rayTracingHeaderSource.size()));
    const AStringView compactRayTracingHeader(compactRayTracingHeaderStorage.data(), compactRayTracingHeaderStorage.size());
    const AString compactRayTracingShadowStorage = CompactSource(AStringView(rayTracingShadowSource.data(), rayTracingShadowSource.size()));
    const AStringView compactRayTracingShadow(compactRayTracingShadowStorage.data(), compactRayTracingShadowStorage.size());
    const AString compactRayTracingCausticsStorage = CompactSource(AStringView(rayTracingCausticsSource.data(), rayTracingCausticsSource.size()));
    const AStringView compactRayTracingCaustics(compactRayTracingCausticsStorage.data(), compactRayTracingCausticsStorage.size());
    const AString compactRayTracingSurfelStorage = CompactSource(AStringView(rayTracingSurfelSource.data(), rayTracingSurfelSource.size()));
    const AStringView compactRayTracingSurfel(compactRayTracingSurfelStorage.data(), compactRayTracingSurfelStorage.size());
    const AString compactDeferredStateStorage = CompactSource(AStringView(deferredStateSource.data(), deferredStateSource.size()));
    const AStringView compactDeferredState(compactDeferredStateStorage.data(), compactDeferredStateStorage.size());
    const AString compactRootGraphStorage = CompactSource(AStringView(rootGraphSource.data(), rootGraphSource.size()));
    const AStringView compactRootGraph(compactRootGraphStorage.data(), compactRootGraphStorage.size());
    const AString compactRootShadowStorage = CompactSource(AStringView(rootShadowSource.data(), rootShadowSource.size()));
    const AStringView compactRootShadow(compactRootShadowStorage.data(), compactRootShadowStorage.size());
    const AString compactRootCausticsStorage = CompactSource(AStringView(rootCausticsSource.data(), rootCausticsSource.size()));
    const AStringView compactRootCaustics(compactRootCausticsStorage.data(), compactRootCausticsStorage.size());
    const AString compactRootSurfelStorage = CompactSource(AStringView(rootSurfelSource.data(), rootSurfelSource.size()));
    const AStringView compactRootSurfel(compactRootSurfelStorage.data(), compactRootSurfelStorage.size());

    EXPECT_TRUE(ContainsText(
        compactFrameTypes,
        "structDeferredLightingGraphResources{Core::BufferHandlesceneShadingBuffer;Core::BufferHandlelightBuffer;"
    ));
    EXPECT_FALSE(ContainsText(compactDeferredHeader, "structDeferredLightingGraphResources{"));
    EXPECT_TRUE(ConstructorParameterTypesMatch(
        compactRayTracingHeader,
        "public:RendererRayTracingSystem(",
        {
            "Core::Alloc::GlobalArena&",
            "Core::ECS::World&",
            "Core::Graphics&",
            "RendererShaderSystem&",
            "RendererMeshSystem&",
            "RendererMaterialSystem&",
            "RendererRayTracingState&",
        }
    ));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingHeaderSource.data(), rayTracingHeaderSource.size()), "RendererDeferredState"));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingSystemSource.data(), rayTracingSystemSource.size()), "m_deferredState"));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingShadowSource.data(), rayTracingShadowSource.size()), "m_deferredState"));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingSoftShadowSource.data(), rayTracingSoftShadowSource.size()), "m_deferredState"));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingCausticsSource.data(), rayTracingCausticsSource.size()), "m_deferredState"));
    EXPECT_FALSE(ContainsText(AStringView(rayTracingSurfelSource.data(), rayTracingSurfelSource.size()), "m_deferredState"));

    EXPECT_FALSE(ContainsText(compactDeferredState, "friendclassRendererRayTracingSystem;"));

    EXPECT_TRUE(ContainsText(compactRayTracingHeader, "renderShadowVisibility(Core::CommandList&commandList,DeferredFrameTargets&targets,constDeferredLightingGraphResources&deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRayTracingHeader, "renderGpuBvhShadowVisibility(Core::CommandList&commandList,DeferredFrameTargets&targets,constDeferredLightingGraphResources&deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRayTracingHeader, "renderGpuBvhCaustics(Core::CommandList&commandList,DeferredFrameTargets&targets,constDeferredLightingGraphResources&deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRayTracingHeader, "renderHwCaustics(Core::CommandList&commandList,DeferredFrameTargets&targets,constDeferredLightingGraphResources&deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRayTracingHeader, "renderSurfelGi(Core::CommandList&commandList,DeferredFrameTargets&targets,constDeferredLightingGraphResources&deferredLightingResources,"));

    EXPECT_EQ(CountText(compactRayTracingShadow, "DeferredLightingGraphResourcesdeferredLightingResources;"), 8u);
    EXPECT_EQ(CountText(compactRayTracingCaustics, "DeferredLightingGraphResourcesdeferredLightingResources;"), 2u);
    EXPECT_EQ(CountText(compactRayTracingSurfel, "DeferredLightingGraphResourcesdeferredLightingResources;"), 7u);
    EXPECT_EQ(CountText(compactRayTracingShadow, ".deferredLightingResources=deferredLightingResources,"), 8u);
    EXPECT_EQ(CountText(compactRayTracingCaustics, ".deferredLightingResources=deferredLightingResources,"), 2u);
    EXPECT_EQ(CountText(compactRayTracingSurfel, ".deferredLightingResources=deferredLightingResources,"), 7u);
    EXPECT_TRUE(ContainsText(compactRayTracingShadow, "deferredLightingResources.sceneShadingBuffer.get()"));
    EXPECT_TRUE(ContainsText(compactRayTracingCaustics, "deferredLightingResources.lightBuffer.get()"));
    EXPECT_TRUE(ContainsText(compactRayTracingSurfel, "deferredLightingResources.sceneShadingBuffer.get()"));

    const usize lightingSnapshot = compactRootGraph.find("constDeferredLightingGraphResourcesdeferredLightingResources=m_deferredSystem.lightingGraphResources();");
    const usize sceneShadingImport = compactRootGraph.find("deferredLightingResources.sceneShadingBuffer", lightingSnapshot);
    const usize shadowDeclaration = compactRootGraph.find("declareDeferredShadowVisibilityTask(deferredTargets,deferredLightingResources,", lightingSnapshot);
    const usize softwareCausticsDeclaration = compactRootGraph.find("declareDeferredSoftwareCausticsTask(declaresHardwareCaustics,deferredTargets,deferredLightingResources,", lightingSnapshot);
    const usize surfelDeclaration = compactRootGraph.find("declareDeferredSurfelGiTask(deferredTargets,deferredLightingResources,", lightingSnapshot);
    const usize hardwareCausticsDeclaration = compactRootGraph.find("declareHardwareCausticsTask(m_deferredLightingTaskGraph,hardwarePhotonDesc,deferredTargets,deferredLightingResources,", lightingSnapshot);
    ASSERT_NE(lightingSnapshot, AStringView::npos);
    ASSERT_NE(sceneShadingImport, AStringView::npos);
    ASSERT_NE(shadowDeclaration, AStringView::npos);
    ASSERT_NE(softwareCausticsDeclaration, AStringView::npos);
    ASSERT_NE(surfelDeclaration, AStringView::npos);
    ASSERT_NE(hardwareCausticsDeclaration, AStringView::npos);
    EXPECT_LT(lightingSnapshot, sceneShadingImport);
    EXPECT_LT(sceneShadingImport, shadowDeclaration);
    EXPECT_TRUE(ContainsText(compactRootShadow, "declareDeferredShadowVisibilityTask(DeferredFrameTargets&deferredTargets,constDeferredLightingGraphResources&deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRootShadow, "declareShadowVisibilityOpaqueTask(m_deferredLightingTaskGraph,opaqueDesc,deferredTargets,deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRootCaustics, "declareSoftwareCausticsTask(m_deferredLightingTaskGraph,photonDesc,deferredTargets,deferredLightingResources,"));
    EXPECT_TRUE(ContainsText(compactRootSurfel, "declareSurfelGiAgeFreeTask(m_deferredLightingTaskGraph,ageFreeDesc,deferredTargets,deferredLightingResources,"));
}


TEST(EcsGraphics, RootOwnsTheCrossDomainFrameTargetAggregate){
    TestArena testArena;
    const TestPath repoRoot = RepoRoot(testArena);
    AString pipelineHeaderSource;
    AString deferredStateSource;
    AString deferredHeaderSource;
    AString deferredSystemSource;
    AString deferredTargetsSource;
    AString rootResourcesSource;
    AString rootExecuteSource;
    AString rootTelemetrySource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "renderer_deferred_state.h", deferredStateSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.h", deferredHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_system.cpp", deferredSystemSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "deferred" / "deferred_targets.cpp", deferredTargetsSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_resources.cpp", rootResourcesSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_execute.cpp", rootExecuteSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "impl" / "ecs_render" / "renderer_frame_pipeline_telemetry.cpp", rootTelemetrySource));

    const AString compactPipelineHeaderStorage = CompactSource(AStringView(pipelineHeaderSource.data(), pipelineHeaderSource.size()));
    const AStringView compactPipelineHeader(compactPipelineHeaderStorage.data(), compactPipelineHeaderStorage.size());
    const AString compactDeferredStateStorage = CompactSource(AStringView(deferredStateSource.data(), deferredStateSource.size()));
    const AStringView compactDeferredState(compactDeferredStateStorage.data(), compactDeferredStateStorage.size());
    const AString compactDeferredHeaderStorage = CompactSource(AStringView(deferredHeaderSource.data(), deferredHeaderSource.size()));
    const AStringView compactDeferredHeader(compactDeferredHeaderStorage.data(), compactDeferredHeaderStorage.size());
    const AString compactDeferredSystemStorage = CompactSource(AStringView(deferredSystemSource.data(), deferredSystemSource.size()));
    const AStringView compactDeferredSystem(compactDeferredSystemStorage.data(), compactDeferredSystemStorage.size());
    const AString compactDeferredTargetsStorage = CompactSource(AStringView(deferredTargetsSource.data(), deferredTargetsSource.size()));
    const AStringView compactDeferredTargets(compactDeferredTargetsStorage.data(), compactDeferredTargetsStorage.size());
    const AString compactRootResourcesStorage = CompactSource(AStringView(rootResourcesSource.data(), rootResourcesSource.size()));
    const AStringView compactRootResources(compactRootResourcesStorage.data(), compactRootResourcesStorage.size());
    const AString compactRootExecuteStorage = CompactSource(AStringView(rootExecuteSource.data(), rootExecuteSource.size()));
    const AStringView compactRootExecute(compactRootExecuteStorage.data(), compactRootExecuteStorage.size());
    const AString compactRootTelemetryStorage = CompactSource(AStringView(rootTelemetrySource.data(), rootTelemetrySource.size()));
    const AStringView compactRootTelemetry(compactRootTelemetryStorage.data(), compactRootTelemetryStorage.size());

    EXPECT_EQ(CountText(compactPipelineHeader, "DeferredFrameTargetsm_frameTargets;"), 1u);
    const usize frameTargetStorage = compactPipelineHeader.find("DeferredFrameTargetsm_frameTargets;");
    const usize firstDomainSystem = compactPipelineHeader.find("RendererShaderSystemm_shaderSystem;");
    ASSERT_NE(frameTargetStorage, AStringView::npos);
    ASSERT_NE(firstDomainSystem, AStringView::npos);
    EXPECT_LT(frameTargetStorage, firstDomainSystem);

    EXPECT_FALSE(ContainsText(compactDeferredState, "DeferredFrameTargets"));
    EXPECT_FALSE(ContainsText(compactDeferredHeader, "frameTargetsMatch("));
    EXPECT_FALSE(ContainsText(compactDeferredHeader, "tryFrameTargets("));
    EXPECT_FALSE(ContainsText(compactDeferredHeader, "commitDeferredFrameTargets("));
    EXPECT_FALSE(ContainsText(compactDeferredHeader, "resetDeferredFrameTargets();"));
    EXPECT_TRUE(ContainsText(compactDeferredHeader, "resetDeferredFrameTargets(DeferredFrameTargets&targets);"));
    EXPECT_FALSE(ContainsText(compactDeferredSystem, "m_deferredState.m_targets"));
    EXPECT_FALSE(ContainsText(compactDeferredTargets, "m_deferredState.m_targets"));

    EXPECT_TRUE(ContainsText(compactRootResources, "voidRendererFramePipeline::commitFrameTargets(DeferredFrameTargets&&targets){m_frameTargets=Move(targets);"));
    const usize resetFunction = compactRootResources.find("voidRendererFramePipeline::resetFrameTargets(){");
    const usize resetAvboit = compactRootResources.find("m_avboitSystem.resetAvboitFrameTargets(m_frameTargets.avboit);", resetFunction);
    const usize resetDeferred = compactRootResources.find("m_deferredSystem.resetDeferredFrameTargets(m_frameTargets);", resetFunction);
    ASSERT_NE(resetFunction, AStringView::npos);
    ASSERT_NE(resetAvboit, AStringView::npos);
    ASSERT_NE(resetDeferred, AStringView::npos);
    EXPECT_LT(resetAvboit, resetDeferred);
    EXPECT_FALSE(ContainsText(compactRootResources, "m_deferredSystem.tryFrameTargets("));
    EXPECT_FALSE(ContainsText(compactRootResources, "m_deferredSystem.frameTargetsMatch("));
    EXPECT_TRUE(ContainsText(compactRootExecute, "if(!m_frameTargets.valid())return;DeferredFrameTargets&deferredTargets=m_frameTargets;"));
    EXPECT_TRUE(ContainsText(compactRootTelemetry, "if(!m_frameTargets.valid())returnfalse;"));

    const usize createDeferred = compactRootResources.find("m_deferredSystem.createDeferredFrameTargets(createdTargets,width,height)");
    const usize createAvboit = compactRootResources.find("m_avboitSystem.createAvboitFrameTargets(createdTargets)", createDeferred);
    const usize createCsg = compactRootResources.find("m_csgSystem.createCsgPeelTargets(createdTargets)", createAvboit);
    const usize createShadow = compactRootResources.find("m_raytracingSystem.createShadowVisibilityTarget(createdTargets)", createCsg);
    const usize createCaustics = compactRootResources.find("m_raytracingSystem.createCausticTargets(createdTargets)", createShadow);
    const usize createSharedResources = compactRootResources.find("m_deferredSystem.createDeferredFrameTargetResources(createdTargets,", createCaustics);
    const usize registerAvboit = compactRootResources.find("m_avboitSystem.registerAvboitFrameTargetDescriptors(createdTargets,createdTargets.avboit)", createSharedResources);
    const usize commitTargets = compactRootResources.find("commitFrameTargets(Move(createdTargets));", registerAvboit);
    ASSERT_NE(createDeferred, AStringView::npos);
    ASSERT_NE(createAvboit, AStringView::npos);
    ASSERT_NE(createCsg, AStringView::npos);
    ASSERT_NE(createShadow, AStringView::npos);
    ASSERT_NE(createCaustics, AStringView::npos);
    ASSERT_NE(createSharedResources, AStringView::npos);
    ASSERT_NE(registerAvboit, AStringView::npos);
    ASSERT_NE(commitTargets, AStringView::npos);
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


TEST(EcsGraphics, KernelDoesNotOwnRootOrAllDomainUmbrellas){
    TestArena testArena;
    const TestPath rendererDirectory = RepoRoot(testArena) / "impl" / "ecs_render";
    AString pipelineHeaderSource;
    AString cmakeSource;
    ASSERT_TRUE(ReadTextFile(rendererDirectory / "renderer_frame_pipeline.h", pipelineHeaderSource));
    ASSERT_TRUE(ReadTextFile(rendererDirectory / "CMakeLists.txt", cmakeSource));
    const AStringView pipelineHeader(pipelineHeaderSource.data(), pipelineHeaderSource.size());
    const AStringView cmake(cmakeSource.data(), cmakeSource.size());

    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/avboit/avboit_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/csg/csg_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/deferred/deferred_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/material/material_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/mesh/mesh_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/raytrace/raytracing_system.h"));
    EXPECT_TRUE(ContainsText(pipelineHeader, "impl/ecs_render/shader/shader_system.h"));

    ErrorCode rendererPrivateError;
    ErrorCode rendererTypesError;
    ErrorCode subsystemsError;
    EXPECT_FALSE(FileExists(rendererDirectory / "kernel" / "renderer_private.h", rendererPrivateError));
    EXPECT_FALSE(rendererPrivateError);
    EXPECT_FALSE(FileExists(rendererDirectory / "kernel" / "renderer_types.h", rendererTypesError));
    EXPECT_FALSE(rendererTypesError);
    EXPECT_FALSE(FileExists(rendererDirectory / "kernel" / "subsystems.h", subsystemsError));
    EXPECT_FALSE(subsystemsError);
    EXPECT_FALSE(ContainsText(cmake, "kernel/renderer_private.h"));
    EXPECT_FALSE(ContainsText(cmake, "kernel/renderer_types.h"));
    EXPECT_FALSE(ContainsText(cmake, "kernel/subsystems.h"));

    for(const StringView domainNameStorage : {
        "avboit",
        "csg",
        "deferred",
        "kernel",
        "material",
        "mesh",
        "raytrace",
        "shader",
        "shared",
    }){
        const AStringView domainName(domainNameStorage.data(), domainNameStorage.size());
        ErrorCode directoryError;
        RecursiveDirectoryIterator domainDirectory(rendererDirectory / domainName.data(), directoryError);
        ASSERT_FALSE(directoryError);
        for(const auto& entry : domainDirectory){
            ErrorCode regularFileError;
            const bool regularFile = entry.is_regular_file(regularFileError);
            ASSERT_FALSE(regularFileError);
            if(!regularFile)
                continue;

            AString sourceStorage;
            ASSERT_TRUE(ReadTextFile(entry.path(), sourceStorage));
            const AStringView source(sourceStorage.data(), sourceStorage.size());
            EXPECT_FALSE(ContainsText(source, "kernel/renderer_private.h"));
            EXPECT_FALSE(ContainsText(source, "kernel/renderer_types.h"));
            EXPECT_FALSE(ContainsText(source, "kernel/subsystems.h"));
            EXPECT_FALSE(ContainsText(source, "renderer_frame_pipeline.h"));

            if(domainName != "kernel")
                continue;
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/avboit/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/csg/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/deferred/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/material/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/mesh/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/raytrace/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/shader/"));
            EXPECT_FALSE(ContainsText(source, "impl/ecs_render/shared/renderer_state.h"));
        }
    }
}


TEST(EcsGraphics, RendererCMakeListsIncludesEverySourceFileOnce){
    TestArena testArena;
    const TestPath rendererDirectory = RepoRoot(testArena) / "impl" / "ecs_render";
    AString cmakeSource;
    ASSERT_TRUE(ReadTextFile(rendererDirectory / "CMakeLists.txt", cmakeSource));
    const AStringView cmake(cmakeSource.data(), cmakeSource.size());

    ErrorCode directoryError;
    RecursiveDirectoryIterator sourceDirectory(rendererDirectory, directoryError);
    ASSERT_FALSE(directoryError);

    usize sourceFileCount = 0u;
    for(const auto& entry : sourceDirectory){
        ErrorCode regularFileError;
        const bool regularFile = entry.is_regular_file(regularFileError);
        ASSERT_FALSE(regularFileError);
        if(!regularFile)
            continue;

        const TestPath extension = entry.path().extension();
        const TStringView extensionText = extension.native();
        if(extensionText != NWB_TEXT(".h") && extensionText != NWB_TEXT(".cpp"))
            continue;

        const TestPath relativePath = entry.path().lexically_relative(rendererDirectory);
        const AInteropString relativeSourcePath = relativePath.generic_string();
        AString cmakeEntry = "\"${CMAKE_CURRENT_LIST_DIR}/";
        cmakeEntry.append(relativeSourcePath.data(), relativeSourcePath.size());
        cmakeEntry += '"';
        const AStringView cmakeEntryView(cmakeEntry.data(), cmakeEntry.size());
        EXPECT_EQ(CountText(cmake, cmakeEntryView), 1u);
        ++sourceFileCount;
    }
    EXPECT_GT(sourceFileCount, 0u);
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

