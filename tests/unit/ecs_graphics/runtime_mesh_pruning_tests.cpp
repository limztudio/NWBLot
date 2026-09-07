// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include <impl/ecs_mesh/skinning/runtime_mesh_liveness.h>
#include <impl/ecs_mesh/skinning/resource_names.h>
#include <impl/ecs_mesh/skinning/runtime_instance.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/mesh/runtime_mesh_pruning.h>

#include <core/ecs/entity.h>
#include <tests/common/graphics_metadata_test_objects.h>

#include <global/text_utils.h>
#include <global/timer.h>

#include <gtest/gtest.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_runtime_mesh_pruning_tests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using namespace NWB;
using namespace NWB::Impl;
using ResourceMap = HashMap<Name, MeshResources, Hasher<Name>, EqualTo<Name>, Core::Alloc::GlobalArena>;
using EntityVector = Vector<Core::ECS::EntityID, Core::Alloc::GlobalArena>;
using NameVector = Vector<Name, Core::Alloc::GlobalArena>;

inline constexpr Core::BufferHandle MeshSkinningRuntimeInstance::* s_InstanceBuffers[] = {
    &MeshSkinningRuntimeInstance::restPositionBuffer,
    &MeshSkinningRuntimeInstance::restNormalBuffer,
    &MeshSkinningRuntimeInstance::restTangentBuffer,
    &MeshSkinningRuntimeInstance::skinnedPositionBuffer,
    &MeshSkinningRuntimeInstance::skinnedNormalBuffer,
    &MeshSkinningRuntimeInstance::skinnedTangentBuffer,
    &MeshSkinningRuntimeInstance::uv0Buffer,
    &MeshSkinningRuntimeInstance::colorBuffer,
    &MeshSkinningRuntimeInstance::meshletDescBuffer,
    &MeshSkinningRuntimeInstance::meshletBoundsBuffer,
    &MeshSkinningRuntimeInstance::meshletPositionRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletAttributeRefDeltaBuffer,
    &MeshSkinningRuntimeInstance::meshletLocalVertexRefBuffer,
    &MeshSkinningRuntimeInstance::meshletPrimitiveIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeSkinBuffer,
    &MeshSkinningRuntimeInstance::triangleIndexBuffer,
    &MeshSkinningRuntimeInstance::attributeBuffer,
};

struct RuntimePayload{
    MeshSkinningRuntimeInstance instance;
    Name fixedKey = NAME_NONE;
    Optional<u64> versionOverride;
    u32 providerId = 0u;

    explicit RuntimePayload(Core::Alloc::GlobalArena& arena)
        : instance(arena)
    {}
};

class WorldRuntimeProvider final : public IRuntimeMeshProvider{
public:
    WorldRuntimeProvider(Core::ECS::World& world, MeshSystem& registry, const u32 providerId)
        : m_world(world)
        , m_registry(registry)
        , m_providerId(providerId)
    {
        m_registry.registerRuntimeMeshProvider(*this);
    }
    virtual ~WorldRuntimeProvider()override{ m_registry.unregisterRuntimeMeshProvider(*this); }


public:
    [[nodiscard]] virtual bool resolveRuntimeMesh(const Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh)override{
        ++m_resolveCalls;
        outMesh = {};
        const auto* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity);
        const auto* payload = m_world.tryGetComponent<RuntimePayload>(entity);
        if(!binding || !binding->runtimeMesh || !payload || payload->providerId != m_providerId)
            return false;
        const MeshSkinningRuntimeInstance* const instance = binding->runtimeMesh == payload->instance.handle ? &payload->instance : nullptr;
        if(!BuildSkinnedRuntimeMeshDesc(entity, binding->runtimeMesh, instance, false, false, outMesh))
            return false;
        if(payload->fixedKey)
            outMesh.meshKey = payload->fixedKey;
        if(payload->versionOverride)
            outMesh.version = *payload->versionOverride;
        return true;
    }

    virtual void markLiveRuntimeMeshes(RuntimeMeshRequestSet& requests)override{
        ++m_membershipCalls;
        MarkLiveSkinnedRuntimeMeshes(
            m_world, requests,
            [this](const Core::ECS::EntityID entity, const SkinnedMeshBindingComponent& binding, Name& meshKey, u64& version){
                ++m_identityCalls;
                const auto* payload = m_world.tryGetComponent<RuntimePayload>(entity);
                if(!payload || payload->providerId != m_providerId)
                    return false;
                const MeshSkinningRuntimeInstance* const instance = binding.runtimeMesh == payload->instance.handle ? &payload->instance : nullptr;
                if(!ResolveSkinnedRuntimeMeshIdentity(entity, binding.runtimeMesh, instance, meshKey, version))
                    return false;
                if(payload->fixedKey)
                    meshKey = payload->fixedKey;
                if(payload->versionOverride)
                    version = *payload->versionOverride;
                return true;
            }
        );
    }


public:
    u64 m_resolveCalls = 0u;
    u64 m_membershipCalls = 0u;
    u64 m_identityCalls = 0u;


private:
    Core::ECS::World& m_world;
    MeshSystem& m_registry;
    u32 m_providerId;
};

struct PruneContext{
    Core::Alloc::GlobalArena arena{ Name("tests/runtime_mesh_pruning/owner") };
    Core::GraphicsAllocator graphicsAllocator{ arena };
    Core::Alloc::ThreadPool threadPool{ 0u };
    Core::GraphicsBackend::VulkanContext context{ graphicsAllocator, threadPool, 1u };
    Core::GraphicsBackend::VulkanAllocator allocator{ context };
    Core::ECS::World world{ arena, threadPool };
    MeshSystem& meshSystem = world.addSystem<MeshSystem>(world);
    Core::BufferHandle buffer;
    WorldRuntimeProvider firstProvider{ world, meshSystem, 0u };
    WorldRuntimeProvider secondProvider{ world, meshSystem, 1u };
    ResourceMap resources{ 0u, Hasher<Name>(), EqualTo<Name>(), arena };
    EntityVector entities{ arena };
    NameVector retired{ arena };

    PruneContext(){
        Core::Buffer* const rawBuffer = Tests::NewMetadataOnlyBuffer(
            arena, context, allocator, Core::BufferDesc{}.setByteSize(256u)
        );
        buffer = Core::BufferHandle(rawBuffer, Core::BufferHandle::deleter_type(&arena), AdoptRef);
    }

    [[nodiscard]] Core::ECS::EntityID addBinding(const u32 providerId = 0u, const Name fixedKey = NAME_NONE){
        auto entity = world.createEntity();
        auto& binding = entity.addComponent<SkinnedMeshBindingComponent>();
        binding.runtimeMesh.value = (1ull << 40u) + static_cast<u64>(entity.id().id) + 1u;
        auto& payload = entity.addComponent<RuntimePayload>(arena);
        payload.providerId = providerId;
        payload.fixedKey = fixedKey;
        MeshSkinningRuntimeInstance& instance = payload.instance;
        instance.handle = binding.runtimeMesh;
        instance.entity = entity.id();
        instance.sourceName = Name("tests/runtime_mesh_pruning/source");
        instance.localBounds.minBounds.w = s_RuntimeMeshBoundsValidFlag | s_RuntimeMeshBoundsFiniteFlag;
        instance.restPositions.resize(1u);
        instance.restNormals.resize(1u);
        instance.restTangents.resize(1u);
        instance.uv0.resize(1u);
        instance.colors.resize(1u);
        instance.meshlets.resize(1u);
        instance.meshletBounds.resize(1u);
        instance.meshletPositionRefDeltas.resize(1u);
        instance.meshletAttributeRefDeltas.resize(1u);
        instance.meshletLocalVertexRefs.resize(1u);
        instance.meshletPrimitiveIndices.resize(3u);
        instance.attributeSkins.resize(1u);
        instance.skin.resize(1u);
        instance.meshletPositionRefCount = 1u;
        instance.meshletAttributeRefCount = 1u;
        instance.editRevision = 7u;
        instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
        for(const auto member : s_InstanceBuffers)
            instance.*member = buffer;
        entities.push_back(entity.id());
        return entity.id();
    }

    [[nodiscard]] Name retainBinding(const Core::ECS::EntityID entity){
        auto& payload = world.entity(entity).getComponent<RuntimePayload>();
        RuntimeMeshDesc description;
        WorldRuntimeProvider& provider = payload.providerId == 0u ? firstProvider : secondProvider;
        if(!provider.resolveRuntimeMesh(entity, description)){
            ADD_FAILURE() << "The fixture binding must resolve before retaining its renderer resources";
            return NAME_NONE;
        }
        MeshResources mesh;
        mesh.meshName = description.meshKey;
        mesh.runtimeMesh = true;
        mesh.runtimeMeshVersion = description.version;
        mesh.positionBuffer = description.positionBuffer;
        const Name key = mesh.meshName;
        resources.emplace(key, Move(mesh));
        return key;
    }

    void retainUnbound(const Name key, const bool runtime = true, const u64 version = 7u){
        MeshResources mesh;
        mesh.meshName = key;
        mesh.runtimeMesh = runtime;
        mesh.runtimeMeshVersion = version;
        mesh.positionBuffer = buffer;
        resources.emplace(key, Move(mesh));
    }

    void prune(Core::Alloc::ScratchArena& scratch, const MeshSystem* system){
        retired.clear();
        retired.reserve(resources.size());
        ECSRenderDetail::PruneRuntimeMeshResources(
            resources, system, [&](MeshResources& mesh){ retired.push_back(mesh.meshName); }, scratch
        );
    }
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


TEST(RuntimeMeshPruning, DescriptorBuildPreservesOwningRolesAndClearsRejectedCurrentInstances){
    PruneContext context;
    const auto entity = context.addBinding();
    auto& instance = context.world.entity(entity).getComponent<RuntimePayload>().instance;
    ASSERT_TRUE(instance.valid());
    const auto referencesBefore = context.buffer->getReferenceCount();
    RuntimeMeshDesc description;
    for(u32 failure = 0u; failure < 5u; ++failure){
        ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(entity, instance.handle, &instance, true, false, description));
        EXPECT_TRUE(description.valid());
        EXPECT_EQ(description.entity, entity);
        EXPECT_EQ(description.meshKey, DeriveRuntimeResourceName(instance.sourceName, instance.handle.value, instance.editRevision, "skinned_draw"));
        EXPECT_EQ(description.version, instance.editRevision);
        EXPECT_EQ(description.meshletCount, 1u);
        EXPECT_EQ(description.meshletPrimitiveIndexCount, 3u);
        EXPECT_TRUE(description.dynamicMeshletBoundsFresh);
        EXPECT_FALSE(description.dynamicMeshletConesFresh);
        EXPECT_EQ(description.positionBuffer, instance.skinnedPositionBuffer);
        EXPECT_EQ(description.triangleIndexBuffer, instance.triangleIndexBuffer);
        EXPECT_EQ(description.attributeBuffer, instance.attributeBuffer);
        EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore + 13u);
        RuntimeMeshHandle requestedHandle = instance.handle;
        const MeshSkinningRuntimeInstance* selectedInstance = &instance;
        if(failure == 0u)
            requestedHandle.reset();
        else if(failure == 1u)
            selectedInstance = nullptr;
        else if(failure == 2u)
            instance.entity = Core::ECS::EntityID(entity.index(), entity.generation() + 1u);
        else if(failure == 3u)
            instance.dirtyFlags = RuntimeMeshDirtyFlag::SkinningInputDirty;
        else
            instance.dirtyFlags = RuntimeMeshDirtyFlag::MeshletBoundsDirty;
        EXPECT_FALSE(BuildSkinnedRuntimeMeshDesc(entity, requestedHandle, selectedInstance, true, true, description));
        EXPECT_FALSE(description.valid());
        EXPECT_EQ(description.meshKey, NAME_NONE);
        EXPECT_EQ(description.entity, Core::ECS::ENTITY_ID_INVALID);
        EXPECT_EQ(description.version, 0u);
        EXPECT_FALSE(description.dynamicMeshletBoundsFresh);
        EXPECT_FALSE(description.dynamicMeshletConesFresh);
        EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore);
        instance.entity = entity;
        instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
    }
    instance.triangleIndexBuffer = nullptr;
    instance.attributeBuffer = nullptr;
    instance.dirtyFlags = RuntimeMeshDirtyFlag::TopologyDirty | RuntimeMeshDirtyFlag::AttributesDirty;
    ASSERT_TRUE(instance.valid());
    ASSERT_TRUE(BuildSkinnedRuntimeMeshDesc(entity, instance.handle, &instance, false, true, description));
    EXPECT_TRUE(description.valid());
    EXPECT_EQ(description.triangleIndexBuffer, nullptr);
    EXPECT_EQ(description.attributeBuffer, nullptr);
    EXPECT_FALSE(description.dynamicMeshletBoundsFresh);
    EXPECT_TRUE(description.dynamicMeshletConesFresh);
}

TEST(RuntimeMeshPruning, StaticResourcesAvoidProviderQueriesAndMissingSystemRetiresOnlyRuntimeMeshes){
    PruneContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/static_scratch"));
    context.retainUnbound(Name("tests/runtime_mesh_pruning/static"), false);
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
    EXPECT_TRUE(context.retired.empty());
    EXPECT_EQ(context.firstProvider.m_membershipCalls, 0u);
    EXPECT_EQ(context.secondProvider.m_membershipCalls, 0u);
    context.retainUnbound(Name("tests/runtime_mesh_pruning/unbound_a"));
    context.retainUnbound(Name("tests/runtime_mesh_pruning/unbound_b"));
    NameVector expected(context.arena);
    for(const auto& entry : context.resources){
        if(entry.second.runtimeMesh)
            expected.push_back(entry.second.meshName);
    }
    context.prune(scratch, nullptr);
    EXPECT_EQ(context.retired, expected);
    EXPECT_EQ(context.resources.size(), 1u);
    EXPECT_NE(context.resources.find(Name("tests/runtime_mesh_pruning/static")), context.resources.end());
}

TEST(RuntimeMeshPruning, InvisibleLiveBindingsSurviveWhileRemovedBindingsAndRecycledEntitiesRetire){
    PruneContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/entities_scratch"));
    const auto live = context.addBinding();
    const auto removedBinding = context.addBinding();
    const auto destroyed = context.addBinding();
    const Name liveKey = context.retainBinding(live);
    const Name removedKey = context.retainBinding(removedBinding);
    const Name destroyedKey = context.retainBinding(destroyed);
    context.world.entity(removedBinding).removeComponent<SkinnedMeshBindingComponent>();
    context.world.destroyEntity(destroyed);
    const auto recycled = context.addBinding();
    EXPECT_EQ(recycled.index(), destroyed.index());
    EXPECT_NE(recycled.generation(), destroyed.generation());
    context.prune(scratch, &context.meshSystem);
    ASSERT_EQ(context.resources.size(), 1u);
    EXPECT_NE(context.resources.find(liveKey), context.resources.end());
    EXPECT_EQ(context.resources.find(removedKey), context.resources.end());
    EXPECT_EQ(context.resources.find(destroyedKey), context.resources.end());
}

TEST(RuntimeMeshPruning, RechecksCurrentHandleEntityReadinessAndFullVersionOnEveryOperation){
    PruneContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/revision_scratch"));
    const Name sharedKey("tests/runtime_mesh_pruning/unchanged_key");
    const auto entity = context.addBinding(0u, sharedKey);
    auto& payload = context.world.entity(entity).getComponent<RuntimePayload>();
    payload.versionOverride = (1ull << 40u) + 7u;
    ASSERT_NE(context.retainBinding(entity), NAME_NONE);
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
    payload.versionOverride = 7u;
    context.prune(scratch, &context.meshSystem);
    EXPECT_TRUE(context.resources.empty());
    for(u32 failure = 0u; failure < 4u; ++failure){
        ASSERT_NE(context.retainBinding(entity), NAME_NONE);
        auto& binding = context.world.entity(entity).getComponent<SkinnedMeshBindingComponent>();
        const RuntimeMeshHandle previousHandle = binding.runtimeMesh;
        if(failure == 0u)
            ++binding.runtimeMesh.value;
        else if(failure == 1u)
            payload.instance.entity = Core::ECS::EntityID(entity.index(), entity.generation() + 1u);
        else if(failure == 2u)
            payload.instance.dirtyFlags = RuntimeMeshDirtyFlag::SkinningInputDirty;
        else
            payload.instance.dirtyFlags = RuntimeMeshDirtyFlag::MeshletBoundsDirty;
        context.prune(scratch, &context.meshSystem);
        EXPECT_TRUE(context.resources.empty());
        binding.runtimeMesh = previousHandle;
        payload.instance.entity = entity;
        payload.instance.dirtyFlags = RuntimeMeshDirtyFlag::None;
    }
    ASSERT_NE(context.retainBinding(entity), NAME_NONE);
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
}

TEST(RuntimeMeshPruning, SharedIdentitiesRemainLiveAcrossProvidersUntilTheirLastCurrentBindingDisappears){
    PruneContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/providers_scratch"));
    const Name sharedKey("tests/runtime_mesh_pruning/shared");
    const auto first = context.addBinding(0u, sharedKey);
    const auto second = context.addBinding(1u, sharedKey);
    ASSERT_NE(context.retainBinding(first), NAME_NONE);
    context.meshSystem.registerRuntimeMeshProvider(context.firstProvider);
    context.world.entity(first).removeComponent<SkinnedMeshBindingComponent>();
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
    context.meshSystem.unregisterRuntimeMeshProvider(context.secondProvider);
    context.prune(scratch, &context.meshSystem);
    EXPECT_TRUE(context.resources.empty());
    context.meshSystem.registerRuntimeMeshProvider(context.secondProvider);
    ASSERT_NE(context.retainBinding(second), NAME_NONE);
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
    context.world.destroyEntity(second);
    context.prune(scratch, &context.meshSystem);
    EXPECT_TRUE(context.resources.empty());
}

TEST(RuntimeMeshPruning, RetiresInSourceOrderWhileOwningHandlesRemainAliveThroughReleaseCallback){
    PruneContext context;
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/lifetime_scratch"));
    const auto live = context.addBinding();
    const Name liveKey = context.retainBinding(live);
    for(u32 index = 0u; index < 48u; ++index){
        const auto text = StringFormat(context.arena, "tests/runtime_mesh_pruning/stale_{}", index);
        context.retainUnbound(Name(AStringView(text)));
    }
    NameVector expected(context.arena);
    expected.reserve(context.resources.size());
    for(const auto& entry : context.resources){
        if(entry.second.meshName != liveKey)
            expected.push_back(entry.second.meshName);
    }
    context.retired.reserve(expected.size());
    const auto referencesBefore = context.buffer->getReferenceCount();
    ECSRenderDetail::PruneRuntimeMeshResources(
        context.resources, &context.meshSystem, [&](MeshResources& mesh){
            ASSERT_NE(mesh.positionBuffer, nullptr);
            EXPECT_EQ(mesh.positionBuffer->getCreationDescription().byteSize, 256u);
            EXPECT_EQ(mesh.positionBuffer->getReferenceCount(), referencesBefore - context.retired.size());
            context.retired.push_back(mesh.meshName);
        }, scratch
    );
    EXPECT_EQ(context.retired, expected);
    EXPECT_EQ(context.resources.size(), 1u);
    EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore - expected.size());
}

TEST(RuntimeMeshPruning, RequestedIdentitiesKeepFullNameAndVersionAcrossHintedAndGrowingIndices){
    Name names[40u];
    for(usize index = 0u; index < 40u; ++index){
        NameHash hash = ComputeNameHash("tests/runtime_mesh_pruning/full_identity");
        hash.qwords[s_NameHashLaneCount - 1u] ^= index + 1u;
        names[index] = Name(hash);
    }
    for(const usize capacityHint : { 0u, 64u }){
        Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/request_scratch"));
        RuntimeMeshRequestSet requests(scratch, capacityHint);
        EXPECT_TRUE(requests.complete());
        requests.add(NAME_NONE, 7u);
        requests.markLive(NAME_NONE, 7u);
        EXPECT_TRUE(requests.complete());
        EXPECT_FALSE(requests.containsLive(NAME_NONE, 7u));
        for(usize index = 0u; index < 40u; ++index){
            requests.add(names[index], 7u);
            requests.add(names[index], 7u);
            if(index < 16u)
                requests.markLive(names[index], 7u);
        }
        requests.add(names[0u], 0u);
        requests.add(names[0u], (1ull << 40u) + 7u);
        for(usize index = 0u; index < 40u; ++index){
            EXPECT_EQ(requests.containsLive(names[index], 7u), index < 16u);
            EXPECT_FALSE(requests.containsLive(names[index], 8u));
        }
        requests.markLive(Name("tests/runtime_mesh_pruning/not_requested"), 7u);
        EXPECT_FALSE(requests.complete());
        for(usize index = 0u; index < 40u; ++index){
            requests.markLive(names[index], 7u);
            requests.markLive(names[index], 7u);
        }
        EXPECT_FALSE(requests.complete());
        EXPECT_FALSE(requests.containsLive(names[0u], 0u));
        EXPECT_FALSE(requests.containsLive(names[0u], (1ull << 40u) + 7u));
        requests.markLive(names[0u], (1ull << 40u) + 7u);
        EXPECT_FALSE(requests.complete());
        requests.markLive(names[0u], 0u);
        EXPECT_TRUE(requests.complete());
        requests.add(names[0u], 7u);
        EXPECT_TRUE(requests.complete());
        for(usize index = 0u; index < 40u; ++index)
            EXPECT_TRUE(requests.containsLive(names[index], 7u));
    }
}

TEST(RuntimeMeshPruning, MembershipStopsAtItsLastRequestedIdentityWithoutRetainingDescriptorBuffers){
    PruneContext context;
    const auto first = context.addBinding();
    const Name retainedKey = context.retainBinding(first);
    ASSERT_NE(retainedKey, NAME_NONE);
    for(u32 index = 0u; index < 64u; ++index)
        ASSERT_TRUE(context.addBinding().valid());
    context.firstProvider.m_resolveCalls = 0u;
    context.secondProvider.m_resolveCalls = 0u;
    const auto referencesBefore = context.buffer->getReferenceCount();
    Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/early_exit_scratch"));
    const auto allocationsBefore = scratch.memoryStats().allocationCount;
    context.prune(scratch, &context.meshSystem);
    EXPECT_EQ(context.resources.size(), 1u);
    EXPECT_NE(context.resources.find(retainedKey), context.resources.end());
    EXPECT_EQ(context.firstProvider.m_identityCalls, 1u);
    EXPECT_EQ(context.firstProvider.m_membershipCalls, 1u);
    EXPECT_EQ(context.secondProvider.m_identityCalls, 0u);
    EXPECT_EQ(context.secondProvider.m_membershipCalls, 0u);
    EXPECT_EQ(context.firstProvider.m_resolveCalls, 0u);
    EXPECT_EQ(context.secondProvider.m_resolveCalls, 0u);
    EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore);
    EXPECT_EQ(scratch.memoryStats().allocationCount, allocationsBefore);

    const auto& instance = context.world.entity(first).getComponent<RuntimePayload>().instance;
    Name meshKey = NAME_NONE;
    u64 version = 0u;
    ASSERT_TRUE(ResolveSkinnedRuntimeMeshIdentity(first, instance.handle, &instance, meshKey, version));
    EXPECT_EQ(meshKey, retainedKey);
    EXPECT_EQ(version, 7u);
    EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore);
    EXPECT_FALSE(ResolveSkinnedRuntimeMeshIdentity(first, RuntimeMeshHandle{}, &instance, meshKey, version));
    EXPECT_EQ(meshKey, NAME_NONE);
    EXPECT_EQ(version, 0u);
    EXPECT_EQ(context.buffer->getReferenceCount(), referencesBefore);
}

static void RecordUnsignedProperty(const NotNull<const char*> key, const u64 value){
    char text[32u] = {};
    const AStringView formatted = FormatDecimal(value, text);
    text[formatted.size()] = '\0';
    testing::Test::RecordProperty(key.get(), text);
}

static void BenchmarkPruning(const usize bindingCount, const usize cachedCount, const usize iterations, const bool shared){
    PruneContext context;
    context.entities.reserve(bindingCount);
    context.resources.reserve(cachedCount);
    for(usize index = 0u; index < bindingCount; ++index)
        ASSERT_TRUE(context.addBinding(0u, shared ? Name("tests/runtime_mesh_pruning/benchmark_shared") : NAME_NONE).valid());
    for(usize index = 0u; index < cachedCount; ++index){
        const usize entityIndex = shared ? 0u : ((index + 1u) * bindingCount / cachedCount - 1u);
        ASSERT_NE(context.retainBinding(context.entities[entityIndex]), NAME_NONE);
    }
    context.retired.reserve(context.resources.size());
    context.firstProvider.m_resolveCalls = 0u;
    context.firstProvider.m_membershipCalls = 0u;
    context.firstProvider.m_identityCalls = 0u;
    context.secondProvider.m_resolveCalls = 0u;
    context.secondProvider.m_membershipCalls = 0u;
    context.secondProvider.m_identityCalls = 0u;
    u64 elapsed = 0u;
    u64 retainedCount = 0u;
    u64 scratchPeak = 0u;
    u64 scratchReserved = 0u;
    for(usize iteration = 0u; iteration < iterations; ++iteration){
        const Timer begin = TimerNow();
        {
            Core::Alloc::ScratchArena scratch(Name("tests/runtime_mesh_pruning/benchmark_scratch"));
            context.prune(scratch, &context.meshSystem);
            retainedCount += context.resources.size();
            scratchPeak = Max(scratchPeak, scratch.memoryStats().peakUsedBytes);
            scratchReserved = Max(scratchReserved, scratch.memoryStats().reservedBytes);
        }
        elapsed += DurationInNS<u64>(TimerNow(), begin);
    }
    EXPECT_EQ(retainedCount, cachedCount * iterations);
    EXPECT_TRUE(context.retired.empty());
    RecordUnsignedProperty(MakeNotNull("runtime_prune_ns"), elapsed);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_iterations"), iterations);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_binding_count"), bindingCount);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_cached_count"), cachedCount);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_resolve_count"), context.firstProvider.m_resolveCalls + context.secondProvider.m_resolveCalls);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_identity_count"), context.firstProvider.m_identityCalls + context.secondProvider.m_identityCalls);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_scratch_peak_bytes"), scratchPeak);
    RecordUnsignedProperty(MakeNotNull("runtime_prune_scratch_reserved_bytes"), scratchReserved);
}

TEST(RuntimeMeshPruningBenchmark, DISABLED_SingleLiveBinding){
    BenchmarkPruning(1u, 1u, 2048u, false);
}

TEST(RuntimeMeshPruningBenchmark, DISABLED_Unique1024Bindings){
    BenchmarkPruning(1024u, 1024u, 1u, false);
}

TEST(RuntimeMeshPruningBenchmark, DISABLED_Sparse32Of4096Bindings){
    BenchmarkPruning(4096u, 32u, 1u, false);
}

TEST(RuntimeMeshPruningBenchmark, DISABLED_Shared4096BindingsOneRetainedMesh){
    BenchmarkPruning(4096u, 1u, 8u, true);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

