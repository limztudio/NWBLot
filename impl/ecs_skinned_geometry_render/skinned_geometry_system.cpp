// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinned_geometry_system.h"

#include "skinned_geometry_runtime_mesh_cache.h"

#include <core/ecs/world.h>
#include <core/graphics/graphics.h>
#include <impl/ecs_render/components.h>
#include <impl/ecs_skinned_geometry/skinned_geometry_runtime_helpers.h>
#include <core/common/log.h>

#include "skinned_geometry_runtime_resource_names.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_skinned_geometry_system{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static bool HasPotentialSkinnedGeometryWork(
    const SkinnedGeometryRuntimeMeshInstance& instance,
    const SkinnedGeometryJointPaletteComponent* jointPalette,
    const SkinnedGeometrySkeletonPoseComponent* skeletonPose
){
    if((instance.dirtyFlags & RuntimeMeshDirtyFlag::SkinnedGeometryInputDirty) != 0u)
        return true;

    return
        !instance.skin.empty()
        && ((jointPalette && !jointPalette->joints.empty()) || SkinnedGeometryRuntime::HasSkeletonPose(skeletonPose))
    ;
}

static bool RuntimeMeshRenderVisible(Core::ECS::World& world, const Core::ECS::EntityID entity){
    const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
    return renderer && renderer->visible;
}

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


SkinnedGeometrySystem::SkinnedGeometrySystem(
    Core::Alloc::GlobalArena& arena,
    Core::ECS::World& world,
    Core::Graphics& graphics,
    Core::Assets::AssetManager& assetManager,
    IRuntimeGeometryRegistry& runtimeGeometryRegistry,
    ShaderPathResolveCallback shaderPathResolver)
    : Core::ECS::ISystem(arena)
    , Core::IRenderPass(graphics)
    , m_arena(arena)
    , m_world(world)
    , m_graphics(graphics)
    , m_assetManager(assetManager)
    , m_runtimeGeometryRegistry(runtimeGeometryRegistry)
    , m_shaderPathResolver(Move(shaderPathResolver))
    , m_runtimeMeshCache(Core::MakeGlobalUnique<SkinnedGeometryRuntimeMeshCache>(arena, arena, graphics, assetManager))
    , m_runtimeResources(0, Hasher<u64>(), EqualTo<u64>(), arena)
{
    writeAccess<SkinnedGeometryComponent>();
    readAccess<RendererComponent>();
    readAccess<SkinnedGeometryJointPaletteComponent>();
    readAccess<SkinnedGeometrySkeletonPoseComponent>();

    m_runtimeGeometryRegistry.registerRuntimeGeometryProvider(*this);
}

SkinnedGeometrySystem::~SkinnedGeometrySystem()
{
    m_runtimeGeometryRegistry.unregisterRuntimeGeometryProvider(*this);
}

void SkinnedGeometrySystem::update(Core::ECS::World& world, const f32 delta){
    static_cast<void>(delta);
    m_runtimeMeshCache->update(world);
}

bool SkinnedGeometrySystem::resolveRuntimeGeometry(const Core::ECS::EntityID entity, RuntimeGeometryDesc& outGeometry){
    outGeometry = RuntimeGeometryDesc{};
    if(!__hidden_skinned_geometry_system::RuntimeMeshRenderVisible(m_world, entity))
        return false;

    const SkinnedGeometryComponent* renderer = m_world.tryGetComponent<SkinnedGeometryComponent>(entity);
    if(!renderer || !renderer->runtimeMesh.valid())
        return false;

    const SkinnedGeometryRuntimeMeshInstance* instance = m_runtimeMeshCache->findInstance(renderer->runtimeMesh);
    if(!instance || !instance->valid() || instance->entity != entity)
        return false;
    if(instance->meshlets.size() > static_cast<usize>(Limit<u32>::s_Max))
        return false;

    outGeometry.entity = entity;
    outGeometry.geometryKey = DeriveRuntimeResourceName(
        instance->source.name(),
        instance->handle.value,
        instance->editRevision,
        "skinned_draw"
    );
    outGeometry.positionBuffer = instance->skinnedPositionBuffer;
    outGeometry.normalBuffer = instance->skinnedNormalBuffer;
    outGeometry.tangentBuffer = instance->skinnedTangentBuffer;
    outGeometry.uv0Buffer = instance->uv0Buffer;
    outGeometry.colorBuffer = instance->colorBuffer;
    outGeometry.vertexRefBuffer = instance->vertexRefBuffer;
    outGeometry.meshletDescBuffer = instance->meshletDescBuffer;
    outGeometry.meshletBoundsBuffer = instance->meshletBoundsBuffer;
    outGeometry.meshletVertexRefBuffer = instance->meshletVertexRefBuffer;
    outGeometry.meshletPrimitiveIndexBuffer = instance->meshletPrimitiveIndexBuffer;
    outGeometry.meshletCount = static_cast<u32>(instance->meshlets.size());
    outGeometry.version = instance->editRevision;
    return outGeometry.valid();
}

bool SkinnedGeometrySystem::containsRuntimeGeometry(const Name& geometryKey, const u64 version){
    if(!geometryKey)
        return false;

    bool found = false;
    m_world.view<SkinnedGeometryComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& component){
            static_cast<void>(component);
            if(found)
                return;

            RuntimeGeometryDesc desc;
            if(!resolveRuntimeGeometry(entity, desc))
                return;

            found = desc.geometryKey == geometryKey && desc.version == version;
        }
    );
    return found;
}

void SkinnedGeometrySystem::render(Core::IFramebuffer* framebuffer){
    static_cast<void>(framebuffer);

    if(!m_runtimeResources.empty()){
        for(auto it = m_runtimeResources.begin(); it != m_runtimeResources.end();){
            const RuntimeResources& resources = it.value();
            const SkinnedGeometryRuntimeMeshInstance* instance =
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
            NWB_LOGGER_ERROR(NWB_TEXT("SkinnedGeometrySystem: failed to create command list"));
            commandListFailed = true;
            return false;
        }

        commandList->open();
        commandListOpen = true;
        return true;
    };

    m_world.view<SkinnedGeometryComponent>().each(
        [&](Core::ECS::EntityID entity, SkinnedGeometryComponent& renderer){
            if(!__hidden_skinned_geometry_system::RuntimeMeshRenderVisible(m_world, entity) || !renderer.runtimeMesh.valid())
                return;

            SkinnedGeometryRuntimeMeshInstance* instance =
                m_runtimeMeshCache->findInstance(renderer.runtimeMesh)
            ;
            if(!instance || !instance->valid())
                return;

            const SkinnedGeometryJointPaletteComponent* jointPalette =
                m_world.tryGetComponent<SkinnedGeometryJointPaletteComponent>(entity)
            ;
            const SkinnedGeometrySkeletonPoseComponent* skeletonPose =
                m_world.tryGetComponent<SkinnedGeometrySkeletonPoseComponent>(entity)
            ;
            if(!__hidden_skinned_geometry_system::HasPotentialSkinnedGeometryWork(
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

void SkinnedGeometrySystem::invalidateResources(){
    m_runtimeResources.clear();
    m_runtimeMeshCache->clear();

    m_bindingLayout.reset();
    m_computeShader.reset();
    m_computePipeline.reset();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

