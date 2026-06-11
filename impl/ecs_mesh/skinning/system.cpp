// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include "resource_names.h"

#include <core/common/log.h>
#include <core/ecs/world.h>
#include <core/graphics/module.h>
#include <impl/ecs_render/components.h>
#include <impl/ecs_skeleton/runtime_helpers.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasPotentialSkinnedMeshWork(
    const SkinnedMeshRuntimeMeshInstance& instance,
    const SkeletonJointPaletteComponent* jointPalette,
    const SkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & (RuntimeMeshDirtyFlag::SkinnedMeshInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return true;

    return
        !instance.skin.empty()
        && (
            (jointPalette && !jointPalette->joints.empty())
            || SkeletonRuntime::HasSkeletonPose(skeletonPose)
        )
    ;
}

static bool RuntimeMeshRenderVisible(Core::ECS::World& world, const Core::ECS::EntityID entity){
    const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
    return renderer && renderer->visible;
}

static void ResolveSkeletonComponents(
    Core::ECS::World& world,
    const Core::ECS::EntityID fallbackEntity,
    const Core::ECS::EntityID skeletonEntity,
    const SkeletonJointPaletteComponent*& outJointPalette,
    const SkeletonPoseComponent*& outSkeletonPose
){
    const Core::ECS::EntityID resolvedEntity = skeletonEntity.valid() ? skeletonEntity : fallbackEntity;
    outJointPalette = world.tryGetComponent<SkeletonJointPaletteComponent>(resolvedEntity);
    outSkeletonPose = world.tryGetComponent<SkeletonPoseComponent>(resolvedEntity);
}

static constexpr bool s_RuntimeSkinnedMeshletFrustumCullingEnabled = true;
static constexpr bool s_RuntimeSkinnedMeshletConeCullingEnabled = false; // Runtime deformations can make meshlet cones unsafe; benchmarks override through a test provider only.


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SkinnedMeshSystem::SkinnedMeshSystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeMeshRegistry& runtimeMeshRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeMeshRegistry(runtimeMeshRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(arena, graphics, assetManager)
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
{
    writeAccess<SkinnedMeshBindingComponent>();
    readAccess<RendererComponent>();
    readAccess<SkeletonJointPaletteComponent>();
    readAccess<SkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

SkinnedMeshSystem::~SkinnedMeshSystem(){
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void SkinnedMeshSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(world);
    static_cast<void>(delta);
}

bool SkinnedMeshSystem::prepareResources(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    m_runtimeMeshCache.prepareResources(m_world);
    pruneRuntimeResources();

    bool ready = true;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(!ready)
                return;
            if(!__hidden_system::RuntimeMeshRenderVisible(m_world, entity) || !binding.runtimeMesh.valid())
                return;

            SkinnedMeshRuntimeMeshInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
#if defined(NWB_DEBUG)
            if(!instance->valid())
                return;
#endif

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            ready = prepareRuntimeMeshResources(*instance, jointPalette, skeletonPose);
        }
    );

    return ready;
}

bool SkinnedMeshSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh){
    outMesh = RuntimeMeshDesc{};
    if(!__hidden_system::RuntimeMeshRenderVisible(m_world, entity))
        return false;

    RuntimeMeshHandle runtimeMesh;
    if(const SkinnedMeshBindingComponent* binding = m_world.tryGetComponent<SkinnedMeshBindingComponent>(entity))
        runtimeMesh = binding->runtimeMesh;
    if(!runtimeMesh.valid())
        return false;

    const SkinnedMeshRuntimeMeshInstance* instance = m_runtimeMeshCache.findInstance(runtimeMesh);
    if(!instance || instance->entity != entity)
        return false;
    if((instance->dirtyFlags & (RuntimeMeshDirtyFlag::SkinnedMeshInputDirty | RuntimeMeshDirtyFlag::MeshletBoundsDirty)) != 0u)
        return false;
#if defined(NWB_DEBUG)
    if(!instance->valid())
        return false;
    if(instance->meshlets.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;
#endif

    outMesh.entity = entity;
    outMesh.meshKey = DeriveRuntimeResourceName(
        instance->sourceName,
        instance->handle.value,
        instance->editRevision,
        "skinned_draw"
    );
    outMesh.positionBuffer = instance->skinnedPositionBuffer;
    outMesh.normalBuffer = instance->skinnedNormalBuffer;
    outMesh.tangentBuffer = instance->skinnedTangentBuffer;
    outMesh.uv0Buffer = instance->uv0Buffer;
    outMesh.colorBuffer = instance->colorBuffer;
    outMesh.meshletDescBuffer = instance->meshletDescBuffer;
    outMesh.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    outMesh.meshletPositionRefDeltaBuffer = instance->meshletPositionRefDeltaBuffer;
    outMesh.meshletAttributeRefDeltaBuffer = instance->meshletAttributeRefDeltaBuffer;
    outMesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    outMesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outMesh.localBounds = instance->localBounds;
    outMesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    outMesh.version = instance->editRevision;
    outMesh.dynamicMeshletBoundsFresh = __hidden_system::s_RuntimeSkinnedMeshletFrustumCullingEnabled;
    outMesh.dynamicMeshletConesFresh = __hidden_system::s_RuntimeSkinnedMeshletConeCullingEnabled;
#if defined(NWB_DEBUG)
    return outMesh.valid();
#else
    return true;
#endif
}

bool SkinnedMeshSystem::containsRuntimeMesh(const Name& meshKey, const u64 version){
    if(!meshKey)
        return false;

    auto testEntity = [&](Core::ECS::EntityID entity, bool& found){
            if(found)
                return;

            RuntimeMeshDesc desc;
            if(!resolveRuntimeMesh(entity, desc))
                return;

            found = desc.meshKey == meshKey && desc.version == version;
    };

    bool found = false;
    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& component){
            static_cast<void>(component);
            testEntity(entity, found);
        }
    );
    return found;
}

void SkinnedMeshSystem::render(Core::Framebuffer* framebuffer){
    static_cast<void>(framebuffer);

    auto* device = m_graphics.getDevice();
    Core::CommandListHandle commandList;
    bool commandListOpen = false;
    bool commandListFailed = false;
    bool submittedWork = false;

    auto ensureCommandList = [&]() -> bool{
        if(commandListOpen)
            return true;
        if(commandListFailed)
            return false;

        commandList = device->createCommandList();
        if(!commandList){
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedMeshSystem: failed to create command list"));
            commandListFailed = true;
            return false;
        }

        commandList->open();
        commandListOpen = true;
        return true;
    };

    m_world.view<SkinnedMeshBindingComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshBindingComponent& binding){
            if(!__hidden_system::RuntimeMeshRenderVisible(m_world, entity) || !binding.runtimeMesh.valid())
                return;

            SkinnedMeshRuntimeMeshInstance* instance = m_runtimeMeshCache.findInstance(binding.runtimeMesh);
            if(!instance)
                return;
#if defined(NWB_DEBUG)
            if(!instance->valid())
                return;
#endif

            const SkeletonJointPaletteComponent* jointPalette = nullptr;
            const SkeletonPoseComponent* skeletonPose = nullptr;
            __hidden_system::ResolveSkeletonComponents(m_world, entity, binding.skeletonEntity, jointPalette, skeletonPose);
            const auto foundResources = m_runtimeResources.find(instance->handle.value);
            const bool hadSkinningResources = foundResources != m_runtimeResources.end() && foundResources.value().usesSkinning();
            if(!__hidden_system::HasPotentialSkinnedMeshWork(
                *instance,
                jointPalette,
                skeletonPose
            ) && !hadSkinningResources)
                return;
            if(!ensureCommandList())
                return;
            if(dispatchRuntimeMesh(*commandList, *instance, jointPalette, skeletonPose))
                submittedWork = true;
        }
    );

    if(!commandListOpen)
        return;

    commandList->close();

    if(submittedWork){
        Core::CommandList* commandLists[] = { commandList.get() };
        device->executeCommandLists(commandLists, 1);
    }
}

void SkinnedMeshSystem::pruneRuntimeResources(){
    if(m_runtimeResources.empty())
        return;

    for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
        const RuntimeResources& resources = it.value();
        const SkinnedMeshRuntimeMeshInstance* instance = m_runtimeMeshCache.findInstance(resources.handle);
        if(instance && instance->valid() && instance->editRevision == resources.editRevision){
            ++it;
            continue;
        }

        it = m_runtimeResources.erase(it);
    }
}

void SkinnedMeshSystem::invalidateResources(){
    m_runtimeResources.clear();
    m_runtimeMeshCache.clear();

    m_skinningBindingLayout.reset();
    m_skinningComputeShader.reset();
    m_skinningComputePipeline.reset();
    m_boundsBindingLayout.reset();
    m_boundsComputeShader.reset();
    m_boundsComputePipeline.reset();
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

