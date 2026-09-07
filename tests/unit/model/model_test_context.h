// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <core/assets/manager.h>
#include <core/ecs/entity.h>
#include <impl/assets_model/asset.h>
#include <impl/ecs_model/system.h>
#include <impl/ecs_scene/components.h>

#include <tests/common/ecs_test_world.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ModelTests{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class EmptyAssetSource final : public Core::Assets::IAssetBinarySource{
public:
    virtual bool readAssetBinary(const Name& virtualPath, Core::Assets::AssetBytes& outBinary)const override{
        static_cast<void>(virtualPath);
        outBinary.clear();
        ++readCount;
        return false;
    }


public:
    mutable usize readCount = 0u;
};

struct RuntimeContext{
    Tests::EcsTestWorld testWorld;
    Core::Assets::AssetRegistry registry{ testWorld.arena };
    EmptyAssetSource source;
    Core::Assets::AssetManager assetManager{ testWorld.arena, registry, source };
    Impl::ModelSystem system{ testWorld.arena, testWorld.world, assetManager };


    [[nodiscard]] Core::ECS::EntityID makeOwner(){
        auto entity = testWorld.world.createEntity();
        auto& component = entity.addComponent<Impl::ModelComponent>();
        component.model = Core::Assets::AssetRef<Impl::Model>("tests/model_runtime/model");
        auto& runtime = entity.addComponent<Impl::ModelRuntimeComponent>();
        runtime.model = component.model.name();
        entity.addComponent<Impl::Scene::TransformComponent>();
        return entity.id();
    }

    [[nodiscard]] Core::ECS::EntityID makeObject(const Core::ECS::EntityID owner){
        auto entity = testWorld.world.createEntity();
        auto& object = entity.addComponent<Impl::ModelObjectComponent>();
        object.owner = owner;
        object.model = Name("tests/model_runtime/model");
        object.object = Name("object");
        entity.addComponent<Impl::Scene::TransformComponent>();
        if(auto* const runtime = testWorld.world.tryGetComponent<Impl::ModelRuntimeComponent>(owner))
            ++runtime->objectCount;
        return entity.id();
    }

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

