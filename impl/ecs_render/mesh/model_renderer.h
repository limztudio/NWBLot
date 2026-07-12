
#pragma once


#include <impl/ecs_render/kernel/components.h>

#include <global/core/ecs/entity.h>
#include <impl/ecs_model/system.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ApplyModelObjectRenderer(
    Core::ECS::World& world,
    Core::Alloc::GlobalArena& arena,
    Core::ECS::Entity& entity,
    const Core::ECS::EntityID owner,
    const Core::Assets::AssetRef<Material>& material
){
    Core::Assets::AssetRef<Material> resolvedMaterial = material;
    bool visible = true;
    if(const RendererComponent* ownerRenderer = world.tryGetComponent<RendererComponent>(owner)){
        visible = ownerRenderer->visible;
        if(!resolvedMaterial.valid())
            resolvedMaterial = ownerRenderer->material;
    }
    if(!resolvedMaterial.valid())
        return;

    auto& renderer = entity.addComponent<RendererComponent>();
    renderer.material = resolvedMaterial;
    renderer.visible = visible;

    const MaterialInstanceComponent* ownerMaterialInstance = world.tryGetComponent<MaterialInstanceComponent>(owner);
    if(!ownerMaterialInstance)
        return;

    const Name materialInterface = ownerMaterialInstance->materialInterface;
    const u64 revision = ownerMaterialInstance->revision;
    MaterialInstanceComponent::ParameterVector overrides(arena);
    overrides.assign(ownerMaterialInstance->overrides.begin(), ownerMaterialInstance->overrides.end());

    auto& materialInstance = entity.addComponent<MaterialInstanceComponent>(arena, materialInterface);
    materialInstance.revision = revision;
    materialInstance.overrides = Move(overrides);
}

[[nodiscard]] inline ModelObjectRendererHooks CreateModelObjectRendererHooks(){
    static const Core::ECS::ComponentAccess s_Accesses[] = {
        { Core::ECS::ComponentType<RendererComponent>(), Core::ECS::AccessMode::Write },
        { Core::ECS::ComponentType<MaterialInstanceComponent>(), Core::ECS::AccessMode::Write },
    };

    return ModelObjectRendererHooks{
        &ApplyModelObjectRenderer,
        s_Accesses,
        sizeof(s_Accesses) / sizeof(s_Accesses[0])
    };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

