// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <core/task/gpu/task_graph.h>

#include <tests/common/graphics_metadata_test_objects.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_task_graph_import_index_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;

struct GraphStamp{
    u64 generation;
    u64 revision;
    usize resources;
    usize sets;
};

[[nodiscard]] static Core::GpuGraphResourceDesc ResourceDescription(const Name& identity, Core::GpuGraphResourceType::Enum type){
    return Core::GpuGraphResourceDesc{}
        .setIdentity(identity)
        .setMarkerLabel("Graph Import Index Resource")
        .setType(type)
        .setInitialState(Core::ResourceStates::Unknown)
    ;
}

[[nodiscard]] static Name IndexedName(const Name& prefix, const usize index){
    char text[32u] = {};
    return DeriveName(prefix, FormatDecimal(index, text));
}

[[nodiscard]] static GraphStamp ReadStamp(const Core::GpuTaskGraph& graph){
    const Core::GpuTaskGraph::DeclarationReadView view(graph);
    EXPECT_TRUE(view.valid());
    return GraphStamp{ view.generation(), view.declarationRevision(), view.resourceCount(), view.resourceSetCount() };
}

static void ExpectUnchanged(const Core::GpuTaskGraph& graph, const GraphStamp& before){
    const GraphStamp after = ReadStamp(graph);
    EXPECT_EQ(after.generation, before.generation);
    EXPECT_EQ(after.revision, before.revision);
    EXPECT_EQ(after.resources, before.resources);
    EXPECT_EQ(after.sets, before.sets);
}

struct ImportContext{
    Core::Alloc::GlobalArena inputArena{ Name("tests/graph_import_index/inputs") };
    Core::GraphicsAllocator graphicsAllocator{ inputArena };
    Core::CpuTaskScheduler cpuScheduler{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, cpuScheduler, 17u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::Alloc::GlobalArena graphArena{ Name("tests/graph_import_index/graph") };
    Core::GpuTaskGraph graph{ graphArena };
    Vector<Core::BufferHandle, Core::Alloc::GlobalArena> buffers{ inputArena };
    Vector<Core::TextureHandle, Core::Alloc::GlobalArena> textures{ inputArena };
    Vector<Core::GpuGraphResourceDesc, Core::Alloc::GlobalArena> genericDescriptions{ inputArena };
    Vector<Core::GpuGraphResourceDesc, Core::Alloc::GlobalArena> typedDescriptions{ inputArena };
    Vector<Core::GpuGraphResourceSetDesc, Core::Alloc::GlobalArena> setDescriptions{ inputArena };
    Vector<Core::GpuGraphResourceId, Core::Alloc::GlobalArena> resources{ inputArena };
    Vector<Core::GpuGraphResourceSetId, Core::Alloc::GlobalArena> sets{ inputArena };

    [[nodiscard]] Core::BufferHandle makeBuffer(const Name& identity){
        Core::Buffer* const buffer = Tests::NewMetadataOnlyBuffer(
            inputArena, context, allocator, Core::BufferDesc{}.setByteSize(256u).setDebugName(identity)
        );
        return Core::BufferHandle(buffer, Core::BufferHandle::deleter_type(&inputArena), AdoptRef);
    }

    [[nodiscard]] Core::TextureHandle makeTexture(const Name& identity){
        Core::TextureDesc description;
        description.setName(identity).setWidth(4u).setHeight(4u).setFormat(Core::Format::RGBA8_UNORM);
        Core::Texture* const texture = Tests::NewMetadataOnlyTexture(inputArena, context, allocator, description);
        return Core::TextureHandle(texture, Core::TextureHandle::deleter_type(&inputArena), AdoptRef);
    }

    [[nodiscard]] Core::RayTracingAccelStructHandle makeAccelStruct(){
        auto* const accelStruct = NewArenaObject<Core::RayTracingAccelStruct>(inputArena, context);
        return Core::RayTracingAccelStructHandle(
            accelStruct, Core::RayTracingAccelStructHandle::deleter_type(&inputArena), AdoptRef
        );
    }

    void prepare(const usize count){
        buffers.resize(count);
        textures.resize(count);
        genericDescriptions.resize(count);
        typedDescriptions.resize(count);
        setDescriptions.resize(count);
        resources.resize(count);
        sets.resize(count);
        for(usize index = 0u; index < count; ++index){
            genericDescriptions[index] = ResourceDescription(
                IndexedName(Name("tests/graph_import_index/generic"), index), Core::GpuGraphResourceType::HazardDomain
            );
            const Name typedIdentity = IndexedName(Name("tests/graph_import_index/typed"), index);
            if(index % 2u == 0u){
                buffers[index] = makeBuffer(typedIdentity);
                typedDescriptions[index] = ResourceDescription(typedIdentity, Core::GpuGraphResourceType::Buffer);
            }
            else{
                textures[index] = makeTexture(typedIdentity);
                typedDescriptions[index] = ResourceDescription(typedIdentity, Core::GpuGraphResourceType::Texture);
            }
            setDescriptions[index]
                .setIdentity(IndexedName(Name("tests/graph_import_index/set"), index))
                .setMarkerLabel("Graph Import Index Set")
                .setMembers(&resources[index], 1u)
            ;
        }
    }

    [[nodiscard]] Core::GpuGraphResourceId importTyped(const usize index){
        return buffers[index]
            ? graph.importBuffer(buffers[index], typedDescriptions[index])
            : graph.importTexture(textures[index], typedDescriptions[index])
        ;
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(TaskGraphImportIndex, GenericFirstDoesNotAcquireTypedOwnershipAndTypedFirstRetainsItsExactHandle){
    constexpr Core::GpuGraphResourceType::Enum s_Types[]{
        Core::GpuGraphResourceType::Buffer, Core::GpuGraphResourceType::Texture, Core::GpuGraphResourceType::AccelStruct,
    };
    for(const auto type : s_Types){
        ImportContext context;
        const Name identity("tests/graph_import_index/typed_precedence");
        const auto buffer = context.makeBuffer(identity);
        const auto texture = context.makeTexture(identity);
        const auto accelStruct = context.makeAccelStruct();
        const auto description = ResourceDescription(identity, type);
        const auto importTyped = [&](){
            if(type == Core::GpuGraphResourceType::Buffer)
                return context.graph.importBuffer(buffer, description);
            if(type == Core::GpuGraphResourceType::Texture)
                return context.graph.importTexture(texture, description);
            return context.graph.importAccelStruct(accelStruct, description);
        };
        const auto generic = context.graph.importResource(description);
        ASSERT_TRUE(generic.valid());
        const GraphStamp genericStamp = ReadStamp(context.graph);
        EXPECT_FALSE(importTyped().valid());
        ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, genericStamp));
        {
            const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
            ASSERT_TRUE(view.valid());
            EXPECT_EQ(view.bufferForResource(generic), nullptr);
            EXPECT_EQ(view.textureForResource(generic), nullptr);
            EXPECT_EQ(view.accelStructForResource(generic), nullptr);
            EXPECT_FALSE(view.findImportedBuffer(buffer).valid());
            EXPECT_FALSE(view.findImportedTexture(texture).valid());
        }
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
        EXPECT_EQ(texture->getReferenceCount(), 1u);
        EXPECT_EQ(accelStruct->getReferenceCount(), 1u);
        context.graph.reset();
        const auto typed = importTyped();
        ASSERT_TRUE(typed.valid());
        EXPECT_NE(typed.generation, generic.generation);
        const GraphStamp typedStamp = ReadStamp(context.graph);
        EXPECT_EQ(context.graph.importResource(description), typed);
        EXPECT_EQ(importTyped(), typed);
        ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, typedStamp));
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.bufferForResource(typed), type == Core::GpuGraphResourceType::Buffer ? buffer.get() : nullptr);
        EXPECT_EQ(view.textureForResource(typed), type == Core::GpuGraphResourceType::Texture ? texture.get() : nullptr);
        EXPECT_EQ(view.accelStructForResource(typed), type == Core::GpuGraphResourceType::AccelStruct ? accelStruct.get() : nullptr);
    }
}

TEST(TaskGraphImportIndex, PointerAliasesKeepFirstIdentityAndLabelWhileConflictingRequestsLeaveTheGraphUnchanged){
    ImportContext context;
    const auto buffer = context.makeBuffer(NAME_NONE);
    const auto texture = context.makeTexture(NAME_NONE);
    auto bufferDescription = ResourceDescription(Name("tests/graph_import_index/buffer_alias"), Core::GpuGraphResourceType::Buffer);
    auto textureDescription = ResourceDescription(Name("tests/graph_import_index/texture_alias"), Core::GpuGraphResourceType::Texture);
    const auto bufferId = context.graph.importBuffer(buffer, bufferDescription);
    const auto textureId = context.graph.importTexture(texture, textureDescription);
    ASSERT_TRUE(bufferId.valid());
    ASSERT_TRUE(textureId.valid());
    const GraphStamp before = ReadStamp(context.graph);
    bufferDescription.markerLabel = "Later Buffer Label";
    textureDescription.markerLabel = "Later Texture Label";
    EXPECT_EQ(context.graph.importBuffer(buffer, bufferDescription), bufferId);
    EXPECT_EQ(context.graph.importTexture(texture, textureDescription), textureId);
    auto differentIdentity = bufferDescription;
    differentIdentity.identity = Name("tests/graph_import_index/different_alias");
    EXPECT_FALSE(context.graph.importBuffer(buffer, differentIdentity).valid());
    const auto otherBuffer = context.makeBuffer(bufferDescription.identity);
    EXPECT_FALSE(context.graph.importBuffer(otherBuffer, bufferDescription).valid());
    auto wrongType = textureDescription;
    wrongType.identity = bufferDescription.identity;
    EXPECT_FALSE(context.graph.importTexture(texture, wrongType).valid());
    wrongType = bufferDescription;
    wrongType.type = Core::GpuGraphResourceType::Texture;
    EXPECT_FALSE(context.graph.importResource(wrongType).valid());
    auto wrongState = bufferDescription;
    wrongState.initialState = Core::ResourceStates::CopyDest;
    EXPECT_FALSE(context.graph.importBuffer(buffer, wrongState).valid());
    auto emptyLabel = bufferDescription;
    emptyLabel.markerLabel = {};
    EXPECT_FALSE(context.graph.importBuffer(buffer, emptyLabel).valid());
    EXPECT_FALSE(context.graph.importResource(emptyLabel).valid());
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.findImportedBuffer(buffer), bufferId);
    EXPECT_EQ(view.findImportedTexture(texture), textureId);
    EXPECT_FALSE(view.findImportedBuffer(otherBuffer).valid());
    EXPECT_FALSE(view.findImportedBuffer({}).valid());
    EXPECT_FALSE(view.findImportedTexture({}).valid());
    EXPECT_EQ(view.resourceAt(bufferId.index).identity, bufferDescription.identity);
    EXPECT_EQ(view.resourceAt(bufferId.index).markerLabel, "Graph Import Index Resource");
    EXPECT_EQ(view.resourceAt(textureId.index).markerLabel, "Graph Import Index Resource");
    EXPECT_EQ(buffer->getReferenceCount(), 2u);
    EXPECT_EQ(texture->getReferenceCount(), 2u);
    EXPECT_EQ(otherBuffer->getReferenceCount(), 1u);
}

TEST(TaskGraphImportIndex, ReimportsRevalidateCurrentDescriptorsButPointerQueriesRetainTheirOriginalAliases){
    ImportContext context;
    context.prepare(2u);
    const auto bufferId = context.importTyped(0u);
    const auto textureId = context.importTyped(1u);
    ASSERT_TRUE(bufferId.valid());
    ASSERT_TRUE(textureId.valid());
    const GraphStamp before = ReadStamp(context.graph);
    Core::BufferDesc& bufferDescription = const_cast<Core::BufferDesc&>(context.buffers[0u]->getDescription());
    Core::TextureDesc& textureDescription = const_cast<Core::TextureDesc&>(context.textures[1u]->getDescription());
    ++bufferDescription.byteSize;
    ++textureDescription.width;
    EXPECT_FALSE(context.importTyped(0u).valid());
    EXPECT_FALSE(context.importTyped(1u).valid());
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.findImportedBuffer(context.buffers[0u]), bufferId);
        EXPECT_EQ(view.findImportedTexture(context.textures[1u]), textureId);
    }
    bufferDescription = context.buffers[0u]->getCreationDescription();
    textureDescription = context.textures[1u]->getCreationDescription();
    EXPECT_EQ(context.importTyped(0u), bufferId);
    EXPECT_EQ(context.importTyped(1u), textureId);
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
}

TEST(TaskGraphImportIndex, EveryNameHashLaneParticipatesAndResourceSetIdentityHasItsOwnNamespace){
    ImportContext context;
    constexpr usize s_Count = 64u;
    context.resources.resize(s_Count);
    context.sets.resize(s_Count);
    Vector<Name, Core::Alloc::GlobalArena> identities(context.inputArena);
    identities.reserve(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        NameHash hash = ComputeNameHash("tests/graph_import_index/full_identity");
        hash.qwords[index % s_NameHashLaneCount] ^= static_cast<u64>(index + 1u);
        identities.emplace_back(hash);
        const auto description = ResourceDescription(identities.back(), Core::GpuGraphResourceType::HazardDomain);
        context.resources[index] = context.graph.importResource(description);
        ASSERT_TRUE(context.resources[index].valid());
        EXPECT_EQ(context.resources[index].index, index);
        context.sets[index] = context.graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(identities.back())
                .setMarkerLabel("Binary Identity Set")
                .setMembers(&context.resources[index], 1u)
        );
        ASSERT_TRUE(context.sets[index].valid());
        EXPECT_EQ(context.sets[index].index, index);
    }
    const GraphStamp before = ReadStamp(context.graph);
    for(usize remaining = s_Count; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        const Name equalIdentity(identities[index].identityHash());
        EXPECT_EQ(context.graph.importResource(ResourceDescription(equalIdentity, Core::GpuGraphResourceType::HazardDomain)), context.resources[index]);
        EXPECT_EQ(context.graph.importResourceSet(
            Core::GpuGraphResourceSetDesc{}
                .setIdentity(equalIdentity)
                .setMarkerLabel("Later Binary Identity Label")
                .setMembers(&context.resources[index], 1u)
        ), context.sets[index]);
    }
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    EXPECT_EQ(before.resources, s_Count);
    EXPECT_EQ(before.sets, s_Count);
}

TEST(TaskGraphImportIndex, RejectedAppendDoesNotReserveIdentityOrAcquireOwnership){
    ImportContext context;
    context.prepare(2u);
    const GraphStamp empty = ReadStamp(context.graph);
    auto invalidBuffer = context.typedDescriptions[0u];
    invalidBuffer.initialAvailabilityCompletion = { 0u, empty.generation };
    auto invalidTexture = context.typedDescriptions[1u];
    invalidTexture.initialAvailabilityCompletion = { 0u, empty.generation };
    EXPECT_FALSE(context.graph.importBuffer(context.buffers[0u], invalidBuffer).valid());
    EXPECT_FALSE(context.graph.importTexture(context.textures[1u], invalidTexture).valid());
    auto invalidGeneric = context.genericDescriptions[0u];
    invalidGeneric.initialAvailabilityCompletion = { 0u, empty.generation };
    EXPECT_FALSE(context.graph.importResource(invalidGeneric).valid());
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, empty));
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 1u);
    EXPECT_EQ(context.textures[1u]->getReferenceCount(), 1u);
    context.resources[0u] = context.importTyped(0u);
    context.resources[1u] = context.importTyped(1u);
    ASSERT_TRUE(context.resources[0u].valid());
    ASSERT_TRUE(context.resources[1u].valid());
    ASSERT_TRUE(context.graph.importResource(context.genericDescriptions[0u]).valid());
    const GraphStamp populated = ReadStamp(context.graph);
    const Core::GpuGraphResourceId invalidMember{ 999u, populated.generation };
    auto invalidSet = context.setDescriptions[0u];
    invalidSet.setMembers(&invalidMember, 1u);
    EXPECT_FALSE(context.graph.importResourceSet(invalidSet).valid());
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, populated));
    ASSERT_TRUE(context.graph.importResourceSet(context.setDescriptions[0u]).valid());
    EXPECT_EQ(context.buffers[0u]->getReferenceCount(), 2u);
    EXPECT_EQ(context.textures[1u]->getReferenceCount(), 2u);
}

TEST(TaskGraphImportIndex, RejectedImportsAndRetriesPreserveOwnershipOnBothSidesOfTheSmallRegistryBoundary){
    constexpr usize s_PrefixCounts[]{ 32u, 33u };
    for(const usize prefixCount : s_PrefixCounts){
        ImportContext context;
        context.prepare(prefixCount);
        for(usize index = 0u; index < prefixCount; ++index){
            context.resources[index] = context.importTyped(index);
            ASSERT_TRUE(context.resources[index].valid());
            context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
            ASSERT_TRUE(context.sets[index].valid());
        }
        const GraphStamp before = ReadStamp(context.graph);
        const Name bufferIdentity("tests/graph_import_index/boundary_new_buffer");
        const Name textureIdentity("tests/graph_import_index/boundary_new_texture");
        const auto buffer = context.makeBuffer(bufferIdentity);
        const auto texture = context.makeTexture(textureIdentity);
        const auto bufferDescription = ResourceDescription(bufferIdentity, Core::GpuGraphResourceType::Buffer);
        const auto textureDescription = ResourceDescription(textureIdentity, Core::GpuGraphResourceType::Texture);
        auto invalidBuffer = bufferDescription;
        invalidBuffer.initialAvailabilityCompletion = { 0u, before.generation };
        auto invalidTexture = textureDescription;
        invalidTexture.initialAvailabilityCompletion = { 0u, before.generation };
        auto invalidGeneric = context.genericDescriptions[0u];
        invalidGeneric.initialAvailabilityCompletion = { 0u, before.generation };
        EXPECT_FALSE(context.graph.importBuffer(buffer, invalidBuffer).valid());
        EXPECT_FALSE(context.graph.importTexture(texture, invalidTexture).valid());
        EXPECT_FALSE(context.graph.importResource(invalidGeneric).valid());
        EXPECT_FALSE(context.graph.importBuffer(buffer, context.typedDescriptions[0u]).valid());
        EXPECT_FALSE(context.graph.importBuffer(context.buffers[0u], bufferDescription).valid());
        auto wrongType = context.typedDescriptions[0u];
        wrongType.type = Core::GpuGraphResourceType::Texture;
        EXPECT_FALSE(context.graph.importResource(wrongType).valid());
        auto conflictingSet = context.setDescriptions[0u];
        conflictingSet.setMembers(&context.resources[1u], 1u);
        EXPECT_FALSE(context.graph.importResourceSet(conflictingSet).valid());
        const Core::GpuGraphResourceId invalidMember{ 999u, before.generation };
        auto newSet = Core::GpuGraphResourceSetDesc{}
            .setIdentity(Name("tests/graph_import_index/boundary_new_set"))
            .setMarkerLabel("Boundary New Set")
            .setMembers(&invalidMember, 1u)
        ;
        EXPECT_FALSE(context.graph.importResourceSet(newSet).valid());
        ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
        EXPECT_EQ(buffer->getReferenceCount(), 1u);
        EXPECT_EQ(texture->getReferenceCount(), 1u);
        const auto bufferId = context.graph.importBuffer(buffer, bufferDescription);
        ASSERT_TRUE(bufferId.valid());
        EXPECT_EQ(bufferId.index, prefixCount);
        ASSERT_TRUE(context.graph.importTexture(texture, textureDescription).valid());
        ASSERT_TRUE(context.graph.importResource(context.genericDescriptions[0u]).valid());
        newSet.setMembers(&bufferId, 1u);
        const auto setId = context.graph.importResourceSet(newSet);
        ASSERT_TRUE(setId.valid());
        EXPECT_EQ(setId.index, prefixCount);
        EXPECT_EQ(context.importTyped(0u), context.resources[0u]);
        EXPECT_EQ(context.graph.importResourceSet(context.setDescriptions[0u]), context.sets[0u]);
        EXPECT_EQ(buffer->getReferenceCount(), 2u);
        EXPECT_EQ(texture->getReferenceCount(), 2u);
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.findImportedBuffer(context.buffers[0u]), context.resources[0u]);
        EXPECT_EQ(view.findImportedBuffer(buffer), bufferId);
    }
}

TEST(TaskGraphImportIndex, DeclarationReadClaimsRejectAllMutatingImportsWithoutPublishingIndexEntries){
    ImportContext context;
    context.prepare(4u);
    context.resources[0u] = context.importTyped(0u);
    context.resources[1u] = context.importTyped(1u);
    ASSERT_TRUE(context.resources[0u].valid());
    ASSERT_TRUE(context.resources[1u].valid());
    context.sets[0u] = context.graph.importResourceSet(context.setDescriptions[0u]);
    ASSERT_TRUE(context.sets[0u].valid());
    const GraphStamp before = ReadStamp(context.graph);
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_FALSE(context.graph.importResource(context.genericDescriptions[0u]).valid());
        EXPECT_FALSE(context.importTyped(0u).valid());
        EXPECT_FALSE(context.importTyped(1u).valid());
        EXPECT_FALSE(context.importTyped(2u).valid());
        EXPECT_FALSE(context.importTyped(3u).valid());
        EXPECT_FALSE(context.graph.importResourceSet(context.setDescriptions[0u]).valid());
        EXPECT_FALSE(context.graph.importResourceSet(context.setDescriptions[1u]).valid());
        EXPECT_EQ(view.declarationRevision(), before.revision);
        EXPECT_EQ(view.resourceCount(), before.resources);
        EXPECT_EQ(view.resourceSetCount(), before.sets);
        EXPECT_EQ(view.findImportedBuffer(context.buffers[0u]), context.resources[0u]);
        EXPECT_EQ(view.findImportedTexture(context.textures[1u]), context.resources[1u]);
    }
    EXPECT_EQ(context.importTyped(0u), context.resources[0u]);
    EXPECT_EQ(context.importTyped(1u), context.resources[1u]);
    ASSERT_TRUE(context.importTyped(2u).valid());
    ASSERT_TRUE(context.importTyped(3u).valid());
    ASSERT_TRUE(context.graph.importResource(context.genericDescriptions[0u]).valid());
    EXPECT_EQ(context.graph.importResourceSet(context.setDescriptions[0u]), context.sets[0u]);
    ASSERT_TRUE(context.graph.importResourceSet(context.setDescriptions[1u]).valid());
}

TEST(TaskGraphImportIndex, ManySetsReuseOnlyTheirOriginalOrderedMembersAndRetainFirstLabels){
    ImportContext context;
    constexpr usize s_Count = 96u;
    context.prepare(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        context.resources[index] = context.graph.importResource(context.genericDescriptions[index]);
        ASSERT_TRUE(context.resources[index].valid());
        context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
        ASSERT_TRUE(context.sets[index].valid());
    }
    const GraphStamp before = ReadStamp(context.graph);
    for(usize index = 0u; index < s_Count; ++index){
        const usize selected = (index * 73u) % s_Count;
        auto description = context.setDescriptions[selected];
        description.markerLabel = "Different Later Set Label";
        EXPECT_EQ(context.graph.importResourceSet(description), context.sets[selected]);
        description.setMembers(&context.resources[(selected + 1u) % s_Count], 1u);
        EXPECT_FALSE(context.graph.importResourceSet(description).valid());
    }
    ASSERT_NO_FATAL_FAILURE(ExpectUnchanged(context.graph, before));
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    for(usize index = 0u; index < s_Count; ++index){
        const auto set = view.resourceSetAt(context.sets[index].index);
        EXPECT_EQ(set.identity, context.setDescriptions[index].identity);
        EXPECT_EQ(set.markerLabel, "Graph Import Index Set");
        ASSERT_EQ(set.memberCount, 1u);
        EXPECT_EQ(set.members[0u], context.resources[index]);
    }
}

TEST(TaskGraphImportIndex, LargeResetAndReverseRefillClearTypedAndGenericIdentityStateWithoutReleasingCallerHandles){
    ImportContext context;
    constexpr usize s_Count = 128u;
    context.prepare(s_Count);
    for(usize index = 0u; index < s_Count; ++index){
        context.resources[index] = context.importTyped(index);
        ASSERT_TRUE(context.resources[index].valid());
        context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
        ASSERT_TRUE(context.sets[index].valid());
    }
    const auto oldResource = context.resources[0u];
    const auto oldSet = context.sets[0u];
    context.graph.reset();
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_FALSE(view.validResource(oldResource));
        EXPECT_FALSE(view.validResourceSet(oldSet));
        EXPECT_FALSE(view.findImportedBuffer(context.buffers[0u]).valid());
        EXPECT_FALSE(view.findImportedTexture(context.textures[1u]).valid());
        EXPECT_EQ(view.resourceCount(), 0u);
        EXPECT_EQ(view.resourceSetCount(), 0u);
    }
    for(usize remaining = s_Count; remaining != 0u; --remaining){
        const usize index = remaining - 1u;
        if(context.buffers[index])
            EXPECT_EQ(context.buffers[index]->getReferenceCount(), 1u);
        else
            EXPECT_EQ(context.textures[index]->getReferenceCount(), 1u);
        context.resources[index] = context.importTyped(index);
        ASSERT_TRUE(context.resources[index].valid());
        EXPECT_EQ(context.resources[index].index, s_Count - 1u - index);
        EXPECT_NE(context.resources[index].generation, oldResource.generation);
        context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
        ASSERT_TRUE(context.sets[index].valid());
    }
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        for(usize index = 0u; index < s_Count; ++index){
            const auto found = context.buffers[index]
                ? view.findImportedBuffer(context.buffers[index])
                : view.findImportedTexture(context.textures[index])
            ;
            EXPECT_EQ(found, context.resources[index]);
        }
    }
    context.graph.reset();
    for(usize index = 0u; index < s_Count; ++index){
        EXPECT_TRUE(context.graph.importResource(context.typedDescriptions[index]).valid());
        EXPECT_FALSE(context.importTyped(index).valid());
    }
    const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
    ASSERT_TRUE(view.valid());
    EXPECT_EQ(view.resourceCount(), s_Count);
    EXPECT_EQ(view.resourceSetCount(), 0u);
    EXPECT_FALSE(view.findImportedBuffer(context.buffers[0u]).valid());
    EXPECT_FALSE(view.findImportedTexture(context.textures[1u]).valid());
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkImports(const usize count, const usize cycles, const usize lookupPasses){
    ImportContext context;
    context.prepare(count);
    u64 genericNanoseconds = 0u;
    u64 genericRepeatNanoseconds = 0u;
    u64 typedNanoseconds = 0u;
    u64 typedRepeatNanoseconds = 0u;
    u64 setNanoseconds = 0u;
    u64 setRepeatNanoseconds = 0u;
    u64 lookupNanoseconds = 0u;
    u64 resetNanoseconds = 0u;
    u64 refillNanoseconds = 0u;
    bool valid = true;
    for(usize cycle = 0u; cycle < cycles; ++cycle){
        if(cycle != 0u){
            const Timer begin = TimerNow();
            context.graph.reset();
            resetNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                context.resources[index] = context.graph.importResource(context.genericDescriptions[index]);
                valid &= context.resources[index].valid();
            }
            genericNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                const usize selected = (index * 73u) % count;
                valid &= context.graph.importResource(context.genericDescriptions[selected]) == context.resources[selected];
            }
            genericRepeatNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                context.sets[index] = context.graph.importResourceSet(context.setDescriptions[index]);
                valid &= context.sets[index].valid();
            }
            setNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                const usize selected = (index * 73u) % count;
                valid &= context.graph.importResourceSet(context.setDescriptions[selected]) == context.sets[selected];
            }
            setRepeatNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            context.graph.reset();
            resetNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                context.resources[index] = context.importTyped(index);
                valid &= context.resources[index].valid();
            }
            typedNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                const usize selected = (index * 73u) % count;
                valid &= context.importTyped(selected) == context.resources[selected];
            }
            typedRepeatNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
            ASSERT_TRUE(view.valid());
            const Timer begin = TimerNow();
            for(usize pass = 0u; pass < lookupPasses; ++pass){
                for(usize index = 0u; index < count; ++index){
                    const usize selected = (index * 73u) % count;
                    const auto found = context.buffers[selected]
                        ? view.findImportedBuffer(context.buffers[selected])
                        : view.findImportedTexture(context.textures[selected])
                    ;
                    valid &= found == context.resources[selected];
                }
            }
            lookupNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        const u64 previousGeneration = context.resources[0u].generation;
        {
            const Timer begin = TimerNow();
            context.graph.reset();
            resetNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        {
            const Timer begin = TimerNow();
            for(usize index = 0u; index < count; ++index){
                context.resources[index] = context.importTyped(index);
                valid &= context.resources[index].valid();
            }
            refillNanoseconds += DurationInNS<u64>(TimerNow(), begin);
        }
        valid &= context.resources[0u].generation != previousGeneration;
    }
    EXPECT_TRUE(valid);
    {
        const Core::GpuTaskGraph::DeclarationReadView view(context.graph);
        ASSERT_TRUE(view.valid());
        EXPECT_EQ(view.resourceCount(), count);
        EXPECT_EQ(view.resourceSetCount(), 0u);
        for(usize index = 0u; index < count; ++index){
            if(context.buffers[index]){
                EXPECT_EQ(view.bufferForResource(context.resources[index]), context.buffers[index].get());
                EXPECT_EQ(context.buffers[index]->getReferenceCount(), 2u);
            }
            else{
                EXPECT_EQ(view.textureForResource(context.resources[index]), context.textures[index].get());
                EXPECT_EQ(context.textures[index]->getReferenceCount(), 2u);
            }
        }
    }
    const ArenaMemoryStats memory = context.graphArena.memoryStats();
    RecordUnsignedProperty(MakeNotNull("graph_import_count"), count);
    RecordUnsignedProperty(MakeNotNull("graph_import_cycles"), cycles);
    RecordUnsignedProperty(MakeNotNull("graph_import_lookup_passes"), lookupPasses);
    RecordUnsignedProperty(MakeNotNull("graph_import_generic_ns"), genericNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_generic_repeat_ns"), genericRepeatNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_typed_ns"), typedNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_typed_repeat_ns"), typedRepeatNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_sets_ns"), setNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_sets_repeat_ns"), setRepeatNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_lookup_ns"), lookupNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_reset_ns"), resetNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_refill_ns"), refillNanoseconds);
    RecordUnsignedProperty(MakeNotNull("graph_import_arena_used_bytes"), memory.usedBytes);
    RecordUnsignedProperty(MakeNotNull("graph_import_arena_peak_bytes"), memory.peakUsedBytes);
    RecordUnsignedProperty(MakeNotNull("graph_import_arena_reserved_bytes"), memory.reservedBytes);
    RecordUnsignedProperty(MakeNotNull("graph_import_arena_allocation_count"), memory.allocationCount);
    RecordUnsignedProperty(MakeNotNull("graph_import_arena_deallocation_count"), memory.deallocationCount);
}

TEST(TaskGraphImportIndexBenchmark, DISABLED_Resources1){
    BenchmarkImports(1u, 256u, 32u);
}

TEST(TaskGraphImportIndexBenchmark, DISABLED_Resources8){
    BenchmarkImports(8u, 64u, 16u);
}

TEST(TaskGraphImportIndexBenchmark, DISABLED_Resources32){
    BenchmarkImports(32u, 16u, 8u);
}

TEST(TaskGraphImportIndexBenchmark, DISABLED_Resources1024){
    BenchmarkImports(1024u, 1u, 2u);
}

TEST(TaskGraphImportIndexBenchmark, DISABLED_Resources4096){
    BenchmarkImports(4096u, 1u, 1u);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

