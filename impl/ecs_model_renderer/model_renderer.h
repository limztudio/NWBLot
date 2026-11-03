// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/ecs_model_renderer/types.adhd.h>
#include <impl/ecs_model/system.h>

#include <core/ecs/entity.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


inline void ApplyModelObjectRenderer(const Core::ECS::EntityID owner, const Core::Assets::AssetRef<Material>& material, Core::ECS::World& world, Core::ECS::Entity& entity){
    const RendererComponent* const ownerRenderer = world.tryGetComponent<RendererComponent>(owner);
    const Expected<RendererComponent> resolvedRenderer = TryResolveModelObjectRendererComponent(
        material,
        ownerRenderer != nullptr,
        ownerRenderer ? *ownerRenderer : RendererComponent{}
    );
    if(!resolvedRenderer)
        return;

    auto& renderer = entity.addComponent<RendererComponent>();
    renderer = *resolvedRenderer;

    const MaterialInstanceComponent* const ownerMaterialInstance = world.tryGetComponent<MaterialInstanceComponent>(owner);
    if(!ownerMaterialInstance)
        return;

    auto& materialInstance = entity.addComponent<MaterialInstanceComponent>();
    CopyModelObjectMaterialInstance(*ownerMaterialInstance, materialInstance);
}

[[nodiscard]] inline ModelObjectRendererHooks CreateModelObjectRendererHooks()noexcept{
    static const Array<Core::ECS::ComponentAccess, 2> s_Accesses = {{
        { Core::ECS::ComponentType<RendererComponent>(), Core::ECS::AccessMode::Write },
        { Core::ECS::ComponentType<MaterialInstanceComponent>(), Core::ECS::AccessMode::Write },
    }};

    return ModelObjectRendererHooks{ &ApplyModelObjectRenderer, Span<const Core::ECS::ComponentAccess>(s_Accesses.data(), s_Accesses.size()) };
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

