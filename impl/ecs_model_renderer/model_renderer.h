// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_render/components.adhd.h>
#include <impl/ecs_model/system.h>

#include <core/ecs/entity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ApplyModelObjectRenderer(Core::ECS::World& world, const Core::ECS::EntityID owner, const Core::Assets::AssetRef<Material>& material, Core::ECS::Entity& entity){
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

    if(!world.tryGetComponent<MaterialInstanceComponent>(owner))
        return;

    auto& materialInstance = entity.addComponent<MaterialInstanceComponent>();
    const MaterialInstanceComponent& ownerMaterialInstance = *MakeNotNull(world.tryGetComponent<MaterialInstanceComponent>(owner));
    materialInstance.materialInterface = ownerMaterialInstance.materialInterface;
    materialInstance.revision = ownerMaterialInstance.revision;
    materialInstance.overrides.assign(ownerMaterialInstance.overrides.begin(), ownerMaterialInstance.overrides.end());
}

[[nodiscard]] inline ModelObjectRendererHooks CreateModelObjectRendererHooks()noexcept{
    static const Core::ECS::ComponentAccess s_Accesses[] = {
        { Core::ECS::ComponentType<RendererComponent>(), Core::ECS::AccessMode::Write },
        { Core::ECS::ComponentType<MaterialInstanceComponent>(), Core::ECS::AccessMode::Write },
    };

    return ModelObjectRendererHooks{ &ApplyModelObjectRenderer, s_Accesses, LengthOf(s_Accesses) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

