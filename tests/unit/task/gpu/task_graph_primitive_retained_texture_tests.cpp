// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "task_graph_test_utils.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_primitive_retained_texture_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace TaskGraphTestUtils;
using TaskGraphTestUtils::TestArena;


TEST(GpuTaskGraph, RejectsRetainedInitialStateMismatchesForTexturePrimitives){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createTexture = [&](const Graphics::TextureDesc& sourceDescription){
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!textureObject)
            return Graphics::TextureHandle{};

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return texture;
    };

    const Graphics::TextureDesc copySourceDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::CopySource)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc copyDestinationDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::CopyDest)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc resolveSourceDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::ResolveSource)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc resolveDestinationDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::ResolveDest)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc rectDestinationDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setInitialState(Graphics::ResourceStates::CopyDest)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc copyCommonDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc resolveBadSourceDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setSampleCount(4u)
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc resolveBadDestinationDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setSampleCount(1u)
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    const Graphics::TextureDesc rectBadDestinationDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UINT)
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    Graphics::TextureHandle copySource = createTexture(copySourceDesc);
    Graphics::TextureHandle copyDestination = createTexture(copyDestinationDesc);
    Graphics::TextureHandle copyLaterSource = createTexture(copySourceDesc);
    Graphics::TextureHandle copyLaterDestination = createTexture(copyDestinationDesc);
    Graphics::TextureHandle resolveSource = createTexture(resolveSourceDesc);
    Graphics::TextureHandle resolveDestination = createTexture(resolveDestinationDesc);
    Graphics::TextureHandle resolveLaterSource = createTexture(resolveSourceDesc);
    Graphics::TextureHandle resolveLaterDestination = createTexture(resolveDestinationDesc);
    Graphics::TextureHandle rectDestination = createTexture(rectDestinationDesc);
    Graphics::TextureHandle copyBadSource = createTexture(copyCommonDesc);
    Graphics::TextureHandle copyBadDestination = createTexture(copyCommonDesc);
    Graphics::TextureHandle resolveBadSource = createTexture(resolveBadSourceDesc);
    Graphics::TextureHandle resolveBadDestination = createTexture(resolveBadDestinationDesc);
    Graphics::TextureHandle rectBadDestination = createTexture(rectBadDestinationDesc);
    ASSERT_NE(copySource.get(), nullptr);
    ASSERT_NE(copyDestination.get(), nullptr);
    ASSERT_NE(copyLaterSource.get(), nullptr);
    ASSERT_NE(copyLaterDestination.get(), nullptr);
    ASSERT_NE(resolveSource.get(), nullptr);
    ASSERT_NE(resolveDestination.get(), nullptr);
    ASSERT_NE(resolveLaterSource.get(), nullptr);
    ASSERT_NE(resolveLaterDestination.get(), nullptr);
    ASSERT_NE(rectDestination.get(), nullptr);
    ASSERT_NE(copyBadSource.get(), nullptr);
    ASSERT_NE(copyBadDestination.get(), nullptr);
    ASSERT_NE(resolveBadSource.get(), nullptr);
    ASSERT_NE(resolveBadDestination.get(), nullptr);
    ASSERT_NE(rectBadDestination.get(), nullptr);

    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId copySourceResource = graph.importTexture(
        copySource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_source"))
            .setMarkerLabel("Retained Copy Texture Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId copyDestinationResource = graph.importTexture(
        copyDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_destination"))
            .setMarkerLabel("Retained Copy Texture Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    const Graphics::GpuGraphResourceId copyLaterSourceResource = graph.importTexture(
        copyLaterSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_later_source"))
            .setMarkerLabel("Retained Copy Texture Later Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId copyLaterDestinationResource = graph.importTexture(
        copyLaterDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_later_destination"))
            .setMarkerLabel("Retained Copy Texture Later Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    const Graphics::GpuGraphResourceId resolveSourceResource = graph.importTexture(
        resolveSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_source"))
            .setMarkerLabel("Retained Resolve Texture Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveSource)
    );
    const Graphics::GpuGraphResourceId resolveDestinationResource = graph.importTexture(
        resolveDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_destination"))
            .setMarkerLabel("Retained Resolve Texture Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveDest)
    );
    const Graphics::GpuGraphResourceId resolveLaterSourceResource = graph.importTexture(
        resolveLaterSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_later_source"))
            .setMarkerLabel("Retained Resolve Texture Later Source")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveSource)
    );
    const Graphics::GpuGraphResourceId resolveLaterDestinationResource = graph.importTexture(
        resolveLaterDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_later_destination"))
            .setMarkerLabel("Retained Resolve Texture Later Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveDest)
    );
    const Graphics::GpuGraphResourceId rectDestinationResource = graph.importTexture(
        rectDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_rect_clear_texture_destination"))
            .setMarkerLabel("Retained Rect Clear Texture Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    const Graphics::GpuGraphResourceId copyBadSourceResource = graph.importTexture(
        copyBadSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_bad_source_resource"))
            .setMarkerLabel("Retained Copy Texture Bad Source Resource")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopySource)
    );
    const Graphics::GpuGraphResourceId copyBadDestinationResource = graph.importTexture(
        copyBadDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_copy_texture_bad_destination_resource"))
            .setMarkerLabel("Retained Copy Texture Bad Destination Resource")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    const Graphics::GpuGraphResourceId resolveBadSourceResource = graph.importTexture(
        resolveBadSource,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_bad_source_resource"))
            .setMarkerLabel("Retained Resolve Texture Bad Source Resource")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveSource)
    );
    const Graphics::GpuGraphResourceId resolveBadDestinationResource = graph.importTexture(
        resolveBadDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_resolve_texture_bad_destination_resource"))
            .setMarkerLabel("Retained Resolve Texture Bad Destination Resource")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::ResolveDest)
    );
    const Graphics::GpuGraphResourceId rectBadDestinationResource = graph.importTexture(
        rectBadDestination,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/retained_rect_clear_texture_bad_destination_resource"))
            .setMarkerLabel("Retained Rect Clear Texture Bad Destination Resource")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::CopyDest)
    );
    ASSERT_TRUE(copySourceResource.valid());
    ASSERT_TRUE(copyDestinationResource.valid());
    ASSERT_TRUE(copyLaterSourceResource.valid());
    ASSERT_TRUE(copyLaterDestinationResource.valid());
    ASSERT_TRUE(resolveSourceResource.valid());
    ASSERT_TRUE(resolveDestinationResource.valid());
    ASSERT_TRUE(resolveLaterSourceResource.valid());
    ASSERT_TRUE(resolveLaterDestinationResource.valid());
    ASSERT_TRUE(rectDestinationResource.valid());
    ASSERT_TRUE(copyBadSourceResource.valid());
    ASSERT_TRUE(copyBadDestinationResource.valid());
    ASSERT_TRUE(resolveBadSourceResource.valid());
    ASSERT_TRUE(resolveBadDestinationResource.valid());
    ASSERT_TRUE(rectBadDestinationResource.valid());

    Graphics::GpuTaskDesc desc;
    desc.setQueue(Graphics::GpuQueueRequest{
        Graphics::GpuQueueCapability::Transfer,
        Graphics::GpuQueuePreference::Transfer,
        true,
        true,
    });
    Graphics::GpuTaskDesc resolveTaskDesc = desc;
    resolveTaskDesc.setQueue(Graphics::GpuQueueRequest{
        Graphics::GpuQueueCapability::Graphics,
        Graphics::GpuQueuePreference::Graphics,
        true,
        true,
    });
    const Graphics::GpuCopyTextureTaskRegion copyBadSourceRegions[]{
        Graphics::GpuCopyTextureTaskRegion{
            .source = copySourceResource,
            .sourceSlice = {},
            .destination = copyDestinationResource,
            .destinationSlice = {},
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = copyBadSourceResource,
            .sourceSlice = {},
            .destination = copyDestinationResource,
            .destinationSlice = {},
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_texture_bad_source"))
        .setMarkerLabel("Retained Copy Texture Bad Source")
    ;
    EXPECT_FALSE(graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = copyBadSourceRegions,
            .regionCount = LengthOf(copyBadSourceRegions),
        }
    ).valid());

    const Graphics::GpuCopyTextureTaskRegion copyBadDestinationRegions[]{
        Graphics::GpuCopyTextureTaskRegion{
            .source = copySourceResource,
            .sourceSlice = {},
            .destination = copyDestinationResource,
            .destinationSlice = {},
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = copySourceResource,
            .sourceSlice = {},
            .destination = copyBadDestinationResource,
            .destinationSlice = {},
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_texture_bad_destination"))
        .setMarkerLabel("Retained Copy Texture Bad Destination")
    ;
    EXPECT_FALSE(graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = copyBadDestinationRegions,
            .regionCount = LengthOf(copyBadDestinationRegions),
        }
    ).valid());

    const Graphics::GpuResolveTextureTaskRegion resolveBadSourceRegions[]{
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveBadSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
    };
    resolveTaskDesc
        .setIdentity(Name("tests/task_graph/retained_resolve_texture_bad_source"))
        .setMarkerLabel("Retained Resolve Texture Bad Source")
    ;
    EXPECT_FALSE(graph.addResolveTextureTask(
        resolveTaskDesc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = resolveBadSourceRegions,
            .regionCount = LengthOf(resolveBadSourceRegions),
        }
    ).valid());

    const Graphics::GpuResolveTextureTaskRegion resolveBadDestinationRegions[]{
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveBadDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
    };
    resolveTaskDesc
        .setIdentity(Name("tests/task_graph/retained_resolve_texture_bad_destination"))
        .setMarkerLabel("Retained Resolve Texture Bad Destination")
    ;
    EXPECT_FALSE(graph.addResolveTextureTask(
        resolveTaskDesc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = resolveBadDestinationRegions,
            .regionCount = LengthOf(resolveBadDestinationRegions),
        }
    ).valid());

    Graphics::GpuClearTextureTaskDesc clearDesc;
    clearDesc.destination = copyDestinationResource;
    clearDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearDesc.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    Graphics::GpuClearTextureTaskDesc badClearDesc = clearDesc;
    badClearDesc.destination = copyBadDestinationResource;
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_texture_bad_destination"))
        .setMarkerLabel("Retained Clear Texture Bad Destination")
    ;
    EXPECT_FALSE(graph.addClearTextureTask(desc, badClearDesc).valid());

    Graphics::GpuClearTextureRectUIntTaskDesc clearRectDesc;
    clearRectDesc.destination = rectDestinationResource;
    clearRectDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearRectDesc.rect = Graphics::Rect(4, 4);
    Graphics::GpuClearTextureRectUIntTaskDesc badClearRectDesc = clearRectDesc;
    badClearRectDesc.destination = rectBadDestinationResource;
    desc
        .setIdentity(Name("tests/task_graph/retained_rect_clear_texture_bad_destination"))
        .setMarkerLabel("Retained Rect Clear Texture Bad Destination")
    ;
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(desc, badClearRectDesc).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    const Graphics::GpuCopyTextureTaskRegion copyValidRegions[]{
        Graphics::GpuCopyTextureTaskRegion{
            .source = copySourceResource,
            .sourceSlice = {},
            .destination = copyDestinationResource,
            .destinationSlice = {},
        },
        Graphics::GpuCopyTextureTaskRegion{
            .source = copyLaterSourceResource,
            .sourceSlice = {},
            .destination = copyLaterDestinationResource,
            .destinationSlice = {},
        },
    };
    const Graphics::GpuResolveTextureTaskRegion resolveValidRegions[]{
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
        Graphics::GpuResolveTextureTaskRegion{
            .source = resolveLaterSourceResource,
            .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .destination = resolveLaterDestinationResource,
            .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        },
    };
    desc
        .setIdentity(Name("tests/task_graph/retained_copy_texture_valid"))
        .setMarkerLabel("Retained Copy Texture Valid")
    ;
    EXPECT_TRUE(graph.addCopyTextureTask(
        desc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = copyValidRegions,
            .regionCount = LengthOf(copyValidRegions),
        }
    ).valid());
    resolveTaskDesc
        .setIdentity(Name("tests/task_graph/retained_resolve_texture_valid"))
        .setMarkerLabel("Retained Resolve Texture Valid")
    ;
    EXPECT_TRUE(graph.addResolveTextureTask(
        resolveTaskDesc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = resolveValidRegions,
            .regionCount = LengthOf(resolveValidRegions),
        }
    ).valid());
    desc
        .setIdentity(Name("tests/task_graph/retained_clear_texture_valid"))
        .setMarkerLabel("Retained Clear Texture Valid")
    ;
    EXPECT_TRUE(graph.addClearTextureTask(desc, clearDesc).valid());
    desc
        .setIdentity(Name("tests/task_graph/retained_rect_clear_texture_valid"))
        .setMarkerLabel("Retained Rect Clear Texture Valid")
    ;
    EXPECT_TRUE(graph.addClearTextureRectUIntTask(desc, clearRectDesc).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.taskCount(), 4u);
}

TEST(GpuTaskGraph, AllowsFreshRetainedTextureUploadAndRetainedClearWhenTheyPublishDescriptorState){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
        testArena.arena,
        context,
        allocator,
        Graphics::TextureDesc()
            .setWidth(4u)
            .setHeight(4u)
            .setMipLevels(2u)
            .setFormat(Graphics::Format::RGBA8_UNORM)
            .setInitialState(Graphics::ResourceStates::ShaderResource)
            .setKeepInitialState(true)
    );
    ASSERT_NE(textureObject, nullptr);
    Graphics::TextureHandle texture(
        textureObject,
        Graphics::TextureHandle::deleter_type(&testArena.arena),
        AdoptRef
    );
    // An all-unknown fresh retained texture keeps the long-standing descriptor-state import behavior.
    Graphics::GpuTaskGraph freshImportGraph(testArena.arena);
    const Graphics::GpuGraphResourceId freshImport = freshImportGraph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/fresh_retained_unspecified_import"))
            .setMarkerLabel("Fresh Retained Unspecified Import")
            .setType(Graphics::GpuGraphResourceType::Texture)
    );
    ASSERT_TRUE(freshImport.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(freshImportGraph);

        EXPECT_EQ(declarations.resourceAt(freshImport.index).initialState, Graphics::ResourceStates::ShaderResource);
    }

    Graphics::GpuTaskGraph graph(testArena.arena);
    const Graphics::GpuGraphResourceId destination = graph.importTexture(
        texture,
        Graphics::GpuGraphResourceDesc{}
            .setIdentity(Name("tests/task_graph/fresh_retained_upload_destination"))
            .setMarkerLabel("Fresh Retained Upload Destination")
            .setType(Graphics::GpuGraphResourceType::Texture)
            .setInitialState(Graphics::ResourceStates::Unknown)
    );
    ASSERT_TRUE(destination.valid());
    const u8 uploadBytes[4u * 4u * 4u]{};
    const Graphics::GpuUploadBlobId source = graph.copyUploadData(uploadBytes, sizeof(uploadBytes), alignof(u32));
    ASSERT_TRUE(source.valid());

    Graphics::GpuTaskDesc uploadTaskDesc;
    uploadTaskDesc
        .setIdentity(Name("tests/task_graph/fresh_retained_upload"))
        .setMarkerLabel("Fresh Retained Upload")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuUploadTextureTaskDesc validUploadDesc{
        .source = source,
        .destination = destination,
        .mipLevel = 0u,
        .finalState = Graphics::ResourceStates::ShaderResource,
    };
    const Graphics::GpuTaskId uploadTask = graph.addUploadTextureTask(uploadTaskDesc, validUploadDesc);
    ASSERT_TRUE(uploadTask.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);
        const Graphics::GpuTaskGraphTaskView uploadTaskView = declarations.taskAt(uploadTask.index);

        ASSERT_EQ(uploadTaskView.resourceUseCount, 2u);
        ASSERT_NE(uploadTaskView.resourceUses, nullptr);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].resource, destination);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].requiredState, Graphics::ResourceStates::CopyDest);
        EXPECT_EQ(uploadTaskView.resourceUses[0u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
        EXPECT_EQ(uploadTaskView.resourceUses[1u].resource, destination);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].requiredState, Graphics::ResourceStates::ShaderResource);
        EXPECT_EQ(uploadTaskView.resourceUses[1u].range.textureSubresources, Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u));
    }
    EXPECT_FALSE(graph.addUploadTextureTask(
        uploadTaskDesc,
        Graphics::GpuUploadTextureTaskDesc{
            .source = source,
            .destination = destination,
            .finalState = Graphics::ResourceStates::CopyDest,
        }
    ).valid());

    Graphics::GpuClearTextureTaskDesc clearDesc;
    clearDesc.destination = destination;
    clearDesc.subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u);
    clearDesc.valueType = Graphics::GpuClearTextureTaskValueType::Float;
    // A full retained clear restores the descriptor state at packet close, so it does not need a fabricated reader
    // source after the earlier upload.
    Graphics::GpuTaskDesc clearTaskDesc = uploadTaskDesc;
    clearTaskDesc
        .setIdentity(Name("tests/task_graph/fresh_retained_clear"))
        .setMarkerLabel("Fresh Retained Clear")
    ;
    EXPECT_TRUE(graph.addClearTextureTask(clearTaskDesc, clearDesc).valid());
    const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

    EXPECT_EQ(declarations.taskCount(), 2u);
}

TEST(GpuTaskGraph, RetainedTextureStateCompletenessHasNoProductionTestMutationHook){
    TestArena testArena;
    const TestPath repoRoot = TestPath(testArena.arena, __FILE__)
        .parent_path()
        .parent_path()
        .parent_path()
        .parent_path()
        .lexically_normal()
    ;
    TestAString backendHeaderSource;
    TestAString textureSource;
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "backend.h", backendHeaderSource));
    ASSERT_TRUE(ReadTextFile(repoRoot / "core" / "graphics" / "vulkan" / "texture.cpp", textureSource));

    const AStringView backendHeader(backendHeaderSource.data(), backendHeaderSource.size());
    const AStringView textureImplementation(textureSource.data(), textureSource.size());
    EXPECT_EQ(backendHeader.find("MarkRetainedTextureSubresourceStateKnownForTesting"), AStringView::npos);
    EXPECT_EQ(textureImplementation.find("MarkRetainedTextureSubresourceStateKnownForTesting"), AStringView::npos);
}

TEST(GpuTaskGraph, AllowsExplicitUnknownRetainedTextureFirstWriteDestinations){
    TestArena testArena;
    Graphics::GraphicsAllocator graphicsAllocator(testArena.arena);
    Core::CpuTaskScheduler cpuScheduler(0u);
    Graphics::GraphicsBackend::VulkanContext context(graphicsAllocator, cpuScheduler, 1u);
    Graphics::GraphicsBackend::VulkanAllocator allocator(context);
    const auto createTexture = [&](const Graphics::TextureDesc& sourceDescription){
        Graphics::Texture* const textureObject = NewMetadataOnlyTexture(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!textureObject)
            return Graphics::TextureHandle{};

        Graphics::TextureHandle texture(
            textureObject,
            Graphics::TextureHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return texture;
    };
    const auto createBuffer = [&](const Graphics::BufferDesc& sourceDescription){
        Graphics::Buffer* const bufferObject = NewMetadataOnlyBuffer(
            testArena.arena,
            context,
            allocator,
            sourceDescription
        );
        if(!bufferObject)
            return Graphics::BufferHandle{};

        Graphics::BufferHandle buffer(
            bufferObject,
            Graphics::BufferHandle::deleter_type(&testArena.arena),
            AdoptRef
        );
        return buffer;
    };
    const Graphics::TextureDesc retainedCommonDesc = Graphics::TextureDesc()
        .setWidth(4u)
        .setHeight(4u)
        .setFormat(Graphics::Format::RGBA8_UNORM)
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    Graphics::TextureDesc resolveSourceDesc = retainedCommonDesc;
    resolveSourceDesc.setSampleCount(4u);
    Graphics::TextureDesc rectClearDesc = retainedCommonDesc;
    rectClearDesc.setFormat(Graphics::Format::RGBA8_UINT);
    const Graphics::BufferDesc retainedCommonBufferDesc = Graphics::BufferDesc()
        .setByteSize(sizeof(u32))
        .setInitialState(Graphics::ResourceStates::Common)
        .setKeepInitialState(true)
    ;
    Graphics::TextureHandle copySource = createTexture(retainedCommonDesc);
    Graphics::TextureHandle unknownCopySource = createTexture(retainedCommonDesc);
    Graphics::TextureHandle copyDestination = createTexture(retainedCommonDesc);
    Graphics::TextureHandle clearDestination = createTexture(retainedCommonDesc);
    Graphics::TextureHandle resolveSource = createTexture(resolveSourceDesc);
    Graphics::TextureHandle resolveDestination = createTexture(retainedCommonDesc);
    Graphics::TextureHandle rectClearDestination = createTexture(rectClearDesc);
    Graphics::BufferHandle bufferSource = createBuffer(retainedCommonBufferDesc);
    Graphics::BufferHandle bufferDestination = createBuffer(retainedCommonBufferDesc);
    ASSERT_NE(copySource.get(), nullptr);
    ASSERT_NE(unknownCopySource.get(), nullptr);
    ASSERT_NE(copyDestination.get(), nullptr);
    ASSERT_NE(clearDestination.get(), nullptr);
    ASSERT_NE(resolveSource.get(), nullptr);
    ASSERT_NE(resolveDestination.get(), nullptr);
    ASSERT_NE(rectClearDestination.get(), nullptr);
    ASSERT_NE(bufferSource.get(), nullptr);
    ASSERT_NE(bufferDestination.get(), nullptr);

    Graphics::GpuTaskGraph graph(testArena.arena);
    const auto importTexture = [&](const Graphics::TextureHandle& texture, const Name& identity, const AStringView label, const bool firstWrite){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Texture)
        ;
        if(firstWrite)
            desc.setInitialState(Graphics::ResourceStates::Unknown);
        return graph.importTexture(texture, desc);
    };
    const auto importBuffer = [&](const Graphics::BufferHandle& buffer, const Name& identity, const AStringView label, const bool firstWrite){
        Graphics::GpuGraphResourceDesc desc;
        desc
            .setIdentity(identity)
            .setMarkerLabel(label)
            .setType(Graphics::GpuGraphResourceType::Buffer)
        ;
        if(firstWrite)
            desc.setInitialState(Graphics::ResourceStates::Unknown);
        return graph.importBuffer(buffer, desc);
    };
    const Graphics::GpuGraphResourceId copySourceResource = importTexture(
        copySource,
        Name("tests/task_graph/unknown_retained_copy_source"),
        "Unknown Retained Copy Source",
        false
    );
    const Graphics::GpuGraphResourceId unknownCopySourceResource = importTexture(
        unknownCopySource,
        Name("tests/task_graph/unknown_retained_copy_unknown_source"),
        "Unknown Retained Copy Unknown Source",
        true
    );
    const Graphics::GpuGraphResourceId copyDestinationResource = importTexture(
        copyDestination,
        Name("tests/task_graph/unknown_retained_copy_destination"),
        "Unknown Retained Copy Destination",
        true
    );
    const Graphics::GpuGraphResourceId clearDestinationResource = importTexture(
        clearDestination,
        Name("tests/task_graph/unknown_retained_clear_destination"),
        "Unknown Retained Clear Destination",
        true
    );
    const Graphics::GpuGraphResourceId resolveSourceResource = importTexture(
        resolveSource,
        Name("tests/task_graph/unknown_retained_resolve_source"),
        "Unknown Retained Resolve Source",
        false
    );
    const Graphics::GpuGraphResourceId resolveDestinationResource = importTexture(
        resolveDestination,
        Name("tests/task_graph/unknown_retained_resolve_destination"),
        "Unknown Retained Resolve Destination",
        true
    );
    const Graphics::GpuGraphResourceId rectClearDestinationResource = importTexture(
        rectClearDestination,
        Name("tests/task_graph/unknown_retained_rect_clear_destination"),
        "Unknown Retained Rect Clear Destination",
        true
    );
    const Graphics::GpuGraphResourceId bufferSourceResource = importBuffer(
        bufferSource,
        Name("tests/task_graph/unknown_retained_buffer_source"),
        "Unknown Retained Buffer Source",
        false
    );
    const Graphics::GpuGraphResourceId bufferDestinationResource = importBuffer(
        bufferDestination,
        Name("tests/task_graph/unknown_retained_buffer_destination"),
        "Unknown Retained Buffer Destination",
        true
    );
    ASSERT_TRUE(copySourceResource.valid());
    ASSERT_TRUE(unknownCopySourceResource.valid());
    ASSERT_TRUE(copyDestinationResource.valid());
    ASSERT_TRUE(clearDestinationResource.valid());
    ASSERT_TRUE(resolveSourceResource.valid());
    ASSERT_TRUE(resolveDestinationResource.valid());
    ASSERT_TRUE(rectClearDestinationResource.valid());
    ASSERT_TRUE(bufferSourceResource.valid());
    ASSERT_TRUE(bufferDestinationResource.valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.resourceAt(copyDestinationResource.index).initialState, Graphics::ResourceStates::Unknown);
        EXPECT_EQ(declarations.resourceAt(clearDestinationResource.index).initialState, Graphics::ResourceStates::Unknown);
    }

    Graphics::GpuTaskDesc copyTaskDesc;
    copyTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_copy"))
        .setMarkerLabel("Unknown Retained Copy")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Transfer,
            Graphics::GpuQueuePreference::Transfer,
            true,
            true,
        })
    ;
    const Graphics::GpuCopyTextureTaskRegion rejectedSourceRegion{
        .source = unknownCopySourceResource,
        .sourceSlice = {},
        .destination = copyDestinationResource,
        .destinationSlice = {},
    };
    EXPECT_FALSE(graph.addCopyTextureTask(
        copyTaskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &rejectedSourceRegion,
            .regionCount = 1u,
        }
    ).valid());

    Graphics::GpuTaskDesc resolveTaskDesc = copyTaskDesc;
    resolveTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_resolve_destination"))
        .setMarkerLabel("Unknown Retained Resolve Destination")
        .setQueue(Graphics::GpuQueueRequest{
            Graphics::GpuQueueCapability::Graphics,
            Graphics::GpuQueuePreference::Graphics,
            true,
            true,
        })
    ;
    const Graphics::GpuResolveTextureTaskRegion rejectedResolveDestinationRegion{
        .source = resolveSourceResource,
        .sourceSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
        .destination = resolveDestinationResource,
        .destinationSubresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
    };
    EXPECT_FALSE(graph.addResolveTextureTask(
        resolveTaskDesc,
        Graphics::GpuResolveTextureTaskDesc{
            .regions = &rejectedResolveDestinationRegion,
            .regionCount = 1u,
        }
    ).valid());

    Graphics::GpuTaskDesc rectClearTaskDesc = copyTaskDesc;
    rectClearTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_rect_clear_destination"))
        .setMarkerLabel("Unknown Retained Rect Clear Destination")
    ;
    EXPECT_FALSE(graph.addClearTextureRectUIntTask(
        rectClearTaskDesc,
        Graphics::GpuClearTextureRectUIntTaskDesc{
            .destination = rectClearDestinationResource,
            .subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .rect = Graphics::Rect(4, 4),
            .uintValue = Graphics::UIntColor(0u, 0u, 0u, 0u),
            .recordHooks = {},
        }
    ).valid());

    Graphics::GpuTaskDesc bufferTaskDesc = copyTaskDesc;
    bufferTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_copy_buffer_destination"))
        .setMarkerLabel("Unknown Retained Copy Buffer Destination")
    ;
    const Graphics::GpuCopyBufferTaskRegion rejectedBufferDestinationRegion{
        .source = bufferSourceResource,
        .sourceOffsetBytes = 0u,
        .destination = bufferDestinationResource,
        .destinationOffsetBytes = 0u,
        .dataSizeBytes = sizeof(u32),
    };
    EXPECT_FALSE(graph.addCopyBufferTask(
        bufferTaskDesc,
        Graphics::GpuCopyBufferTaskDesc{
            .regions = &rejectedBufferDestinationRegion,
            .regionCount = 1u,
        }
    ).valid());
    bufferTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_clear_buffer_destination"))
        .setMarkerLabel("Unknown Retained Clear Buffer Destination")
    ;
    EXPECT_FALSE(graph.addClearBufferTask(
        bufferTaskDesc,
        Graphics::GpuClearBufferTaskDesc{
            .destination = bufferDestinationResource,
        }
    ).valid());
    {
        const Graphics::GpuTaskGraph::DeclarationReadView declarations(graph);

        EXPECT_EQ(declarations.taskCount(), 0u);
    }

    const Graphics::GpuCopyTextureTaskRegion copyRegion{
        .source = copySourceResource,
        .sourceSlice = {},
        .destination = copyDestinationResource,
        .destinationSlice = {},
    };
    const Graphics::GpuTaskId copyTask = graph.addCopyTextureTask(
        copyTaskDesc,
        Graphics::GpuCopyTextureTaskDesc{
            .regions = &copyRegion,
            .regionCount = 1u,
        }
    );
    ASSERT_TRUE(copyTask.valid());

    Graphics::GpuTaskDesc clearTaskDesc = copyTaskDesc;
    clearTaskDesc
        .setIdentity(Name("tests/task_graph/unknown_retained_clear"))
        .setMarkerLabel("Unknown Retained Clear")
    ;
    const Graphics::GpuTaskId clearTask = graph.addClearTextureTask(
        clearTaskDesc,
        Graphics::GpuClearTextureTaskDesc{
            .destination = clearDestinationResource,
            .subresources = Graphics::TextureSubresourceSet(0u, 1u, 0u, 1u),
            .valueType = Graphics::GpuClearTextureTaskValueType::Float,
        }
    );
    ASSERT_TRUE(clearTask.valid());

    const Graphics::GpuPhysicalQueueInfo queues[]{ GraphicsQueue() };
    const Graphics::GpuTaskGraphQueueTopology topology{
        .queues = queues,
        .queueCount = LengthOf(queues),
    };
    Graphics::GpuTaskGraphAnalysis analysis(testArena.arena);
    Graphics::GpuTaskGraphQueueAssignments assignments(testArena.arena);
    Graphics::GpuCompiledGraph compiledGraph(testArena.arena);
    ASSERT_TRUE(Compile(graph, analysis, topology, assignments, compiledGraph));
    const Graphics::GpuCompiledGraph::ReadView compiledPlan(compiledGraph);


    const auto findPrologueBarrier = [&](const Graphics::GpuTaskId task, const Graphics::GpuGraphResourceId resource){
        const Graphics::GpuCompiledTask* const compiledTask = compiledPlan.findTask(task).plan;
        const Graphics::GpuCompiledBarrier* const barriers = compiledPlan.findTask(task).prologueBarriers;
        if(!compiledTask || !barriers)
            return static_cast<const Graphics::GpuCompiledBarrier*>(nullptr);
        for(u32 barrierIndex = 0u; barrierIndex < compiledTask->prologueBarrierCount; ++barrierIndex){
            if(barriers[barrierIndex].resource == resource)
                return &barriers[barrierIndex];
        }
        return static_cast<const Graphics::GpuCompiledBarrier*>(nullptr);
    };
    const Graphics::GpuCompiledBarrier* const copyBarrier = findPrologueBarrier(copyTask, copyDestinationResource);
    ASSERT_NE(copyBarrier, nullptr);
    EXPECT_EQ(copyBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_EQ(copyBarrier->after, Graphics::ResourceStates::CopyDest);
    EXPECT_FALSE(copyBarrier->isGraphInitialState);
    const Graphics::GpuCompiledBarrier* const clearBarrier = findPrologueBarrier(clearTask, clearDestinationResource);
    ASSERT_NE(clearBarrier, nullptr);
    EXPECT_EQ(clearBarrier->before, Graphics::ResourceStates::Unknown);
    EXPECT_EQ(clearBarrier->after, Graphics::ResourceStates::CopyDest);
    EXPECT_FALSE(clearBarrier->isGraphInitialState);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

