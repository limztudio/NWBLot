// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_mesh_system.h"

#include "skinned_mesh_runtime_mesh_cache.h"

#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <impl/ecs_render/components.h>
#include <impl/ecs_skinned_mesh/skinned_mesh_runtime_helpers.h>
#include <core/common/log.h>

#include "skinned_mesh_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_mesh_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasPotentialSkinnedMeshWork(
    const SkinnedMeshRuntimeMeshInstance& instance,
    const SkinnedMeshJointPaletteComponent* jointPalette,
    const SkinnedMeshSkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedMeshInputDirty) != 0u)
        return true;

    return
        !instance.skin.empty()
        && ((jointPalette && !jointPalette->joints.empty()) || SkinnedMeshRuntime::HasSkeletonPose(skeletonPose))
    ;
}

static bool RuntimeMeshRenderVisible(Core::ECS::World& world, const Core::ECS::EntityID entity){
    const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
    return renderer && renderer->visible;
}

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
    , m_runtimeMeshCache(Core::MakeGlobalUnique<SkinnedMeshRuntimeMeshCache>(arena, arena, graphics, assetManager))
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
{
    writeAccess<SkinnedMeshComponent>();
    readAccess<RendererComponent>();
    readAccess<SkinnedMeshJointPaletteComponent>();
    readAccess<SkinnedMeshSkeletonPoseComponent>();

    m_runtimeMeshRegistry.registerRuntimeMeshProvider(*this);
}

SkinnedMeshSystem::~SkinnedMeshSystem()
{
    m_runtimeMeshRegistry.unregisterRuntimeMeshProvider(*this);
}

void SkinnedMeshSystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(delta);
    m_runtimeMeshCache->update(world);
}

bool SkinnedMeshSystem::resolveRuntimeMesh(const Core::ECS::EntityID entity, RuntimeMeshDesc& outMesh){
    outMesh = RuntimeMeshDesc{};
    if(!__hidden_skinned_mesh_system::RuntimeMeshRenderVisible(m_world, entity))
        return false;

    const SkinnedMeshComponent* renderer = m_world.tryGetComponent<SkinnedMeshComponent>(entity);
    if(!renderer || !renderer->runtimeMesh.valid())
        return false;

    const SkinnedMeshRuntimeMeshInstance* instance = m_runtimeMeshCache->findInstance(renderer->runtimeMesh);
    if(!instance || !instance->valid() || instance->entity != entity)
        return false;
    if(instance->meshlets.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outMesh.entity = entity;
    outMesh.meshKey = DeriveRuntimeResourceName(
        instance->source.name(),
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
    outMesh.meshletPositionRefBuffer = instance->meshletPositionRefBuffer;
    outMesh.meshletAttributeRefBuffer = instance->meshletAttributeRefBuffer;
    outMesh.meshletLocalVertexRefBuffer = instance->meshletLocalVertexRefBuffer;
    outMesh.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outMesh.meshletCount = static_cast<u32>(instance->meshlets.size());
    outMesh.version = instance->editRevision;
    return outMesh.valid();
}

bool SkinnedMeshSystem::containsRuntimeMesh(const Name& meshKey, const u64 version){
    if(!meshKey)
        return false;

    bool found = false;
    m_world.view<SkinnedMeshComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshComponent& component){
            static_cast<void>(component);
            if(found)
                return;

            RuntimeMeshDesc desc;
            if(!resolveRuntimeMesh(entity, desc))
                return;

            found = desc.meshKey == meshKey && desc.version == version;
        }
    );
    return found;
}

void SkinnedMeshSystem::render(Core::IFramebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!m_runtimeResources.empty()){
        for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
            const RuntimeResources& resources = it.value();
            const SkinnedMeshRuntimeMeshInstance* instance =
                m_runtimeMeshCache->findInstance(resources.handle)
            ;
            if(!instance || !instance->valid() || instance->editRevision != resources.editRevision){
                it = m_runtimeResources.erase(it);
                continue;
            }

            ++it;
        }
    }

    Core::IDevice* device = m_graphics.getDevice();
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

    m_world.view<SkinnedMeshComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedMeshComponent& renderer){
            if(!__hidden_skinned_mesh_system::RuntimeMeshRenderVisible(m_world, entity) || !renderer.runtimeMesh.valid())
                return;

            SkinnedMeshRuntimeMeshInstance* instance =
                m_runtimeMeshCache->findInstance(renderer.runtimeMesh)
            ;
            if(!instance || !instance->valid())
                return;

            const SkinnedMeshJointPaletteComponent* jointPalette =
                m_world.tryGetComponent<SkinnedMeshJointPaletteComponent>(entity)
            ;
            const SkinnedMeshSkeletonPoseComponent* skeletonPose =
                m_world.tryGetComponent<SkinnedMeshSkeletonPoseComponent>(entity)
            ;
            if(!__hidden_skinned_mesh_system::HasPotentialSkinnedMeshWork(
                *instance,
                jointPalette,
                skeletonPose
            ))
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
        Core::ICommandList* commandLists[] = { commandList.get() };
        device->executeCommandLists(commandLists, 1);
    }
}

void SkinnedMeshSystem::invalidateResources(){
    m_runtimeResources.clear();
    m_runtimeMeshCache->clear();

    m_bindingLayout.reset();
    m_computeShader.reset();
    m_computePipeline.reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

