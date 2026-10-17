// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "frame_state_cache.h"

#include <impl/ecs_csg/shape_registry.h>
#include <impl/ecs_mesh/system.h>
#include <impl/ecs_render/components.h>
#include <impl/ecs_scene/components.h>

#include <core/ecs/world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace CsgFrameCacheDetail{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] Expected<CsgFrameStateCacheSignature> BuildSignature(Core::ECS::World& world, const CsgShapeRegistry& shapeRegistry, const MeshSystem& meshSystem){
    CsgFrameStateCacheSignature signature;
    signature.shapeRegistryRevision = shapeRegistry.revision();

    if(world.view<SkinnedCsgMeshComponent>().candidateCount() > 0u)
        return MakeUnexpected(Failure{});

    u64 contentHash = s_Fnv64OffsetBasis;
    auto cutterView = world.view<CsgCutterComponent>();
    cutterView.each([&](const Core::ECS::EntityID entity, CsgCutterComponent& cutter){
            Fnv64AppendValue(contentHash, entity.id);
            Fnv64AppendBool(contentHash, cutter.active);
            Fnv64AppendValue(contentHash, cutter.receiverGroup.hash());
            Fnv64AppendValue(contentHash, cutter.shapeType.hash());
            Fnv64AppendValue(contentHash, cutter.worldToShape);
            Fnv64AppendValue(contentHash, cutter.shapeToWorld);
            Fnv64AppendBuffer(contentHash, cutter.parameterBytes.data(), cutter.parameterBytes.size());
        }
    );

    auto receiverView = world.view<StaticCsgMeshComponent>();
    bool cacheable = true;
    receiverView.each([&](const Core::ECS::EntityID entity, StaticCsgMeshComponent& typedReceiver){
            const CsgReceiverComponent& receiver = typedReceiver;
            Fnv64AppendValue(contentHash, entity.id);
            Fnv64AppendBool(contentHash, receiver.enabled);
            Fnv64AppendBool(contentHash, receiver.affectOpaquePass);
            Fnv64AppendBool(contentHash, receiver.affectTransparentPass);
            Fnv64AppendValue(contentHash, receiver.receiverGroup.hash());

            const RendererComponent* renderer = world.tryGetComponent<RendererComponent>(entity);
            Fnv64AppendBool(contentHash, renderer != nullptr);
            if(renderer){
                Fnv64AppendBool(contentHash, renderer->visible);
                Fnv64AppendValue(contentHash, renderer->material.name().hash());
            }

            const MeshComponent* mesh = world.tryGetComponent<MeshComponent>(entity);
            Fnv64AppendBool(contentHash, mesh != nullptr);
            if(mesh){
                Fnv64AppendValue(contentHash, mesh->mesh.name().hash());
            }
            else if(receiver.enabled && renderer){
                cacheable = false;
            }

            if(receiver.enabled && renderer && renderer->visible && meshSystem.hasRuntimeMeshBinding(entity))
                cacheable = false;

            const Scene::TransformComponent* transform = world.tryGetComponent<Scene::TransformComponent>(entity);
            Fnv64AppendBool(contentHash, transform != nullptr);
            if(transform){
                Fnv64AppendValue(contentHash, transform->position);
                Fnv64AppendValue(contentHash, transform->rotation);
                Fnv64AppendValue(contentHash, transform->scale);
            }
        }
    );
    if(!cacheable)
        return MakeUnexpected(Failure{});

    signature.contentHash = contentHash;
    return signature;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

