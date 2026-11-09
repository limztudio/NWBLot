// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <tests/smoke/scene_types.adhd.h>

#include <impl/ecs_csg/components.adhd.h>

#include <core/ecs/entity.h>
#include <core/ecs/world.h>

#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ParameterT>
inline void AssignCsgCutterParameters(const ParameterT& parameters, Impl::CsgCutterComponent& cutter){
    cutter.parameterBytes.resize(sizeof(ParameterT));
    NWB_MEMCPY(cutter.parameterBytes.data(), cutter.parameterBytes.size(), &parameters, sizeof(ParameterT));
}

template<typename ReceiverComponentT>
inline ReceiverComponentT& AddCsgMeshReceiver(const Core::ECS::EntityID entity, const Name receiverGroup, const b8 affectOpaquePass, const b8 affectTransparentPass, Core::ECS::World& world){
    auto& receiver = world.entity(entity).addComponent<ReceiverComponentT>();
    receiver.receiverGroup = receiverGroup;
    receiver.affectOpaquePass = affectOpaquePass;
    receiver.affectTransparentPass = affectTransparentPass;
    return receiver;
}

inline void AssignCsgCutterTransform(const SIMDVectorSingle center, const SIMDVectorSingle rotation, Impl::CsgCutterComponent& cutter)noexcept{
    SIMDMatrix worldToShape;
    SIMDMatrix shapeToWorld;
    ComputeCsgCutterTransforms(center, rotation, worldToShape, shapeToWorld);
    StoreFloat(worldToShape, cutter.worldToShape);
    StoreFloat(shapeToWorld, cutter.shapeToWorld);
}

inline Impl::StaticCsgMeshComponent& AddStaticCsgMeshReceiver(const Core::ECS::EntityID entity, const Name receiverGroup, const b8 affectOpaquePass, const b8 affectTransparentPass, Core::ECS::World& world){
    return AddCsgMeshReceiver<Impl::StaticCsgMeshComponent>(entity, receiverGroup, affectOpaquePass, affectTransparentPass, world);
}

inline Impl::SkinnedCsgMeshComponent& AddSkinnedCsgMeshReceiver(const Core::ECS::EntityID entity, const Name receiverGroup, const b8 affectOpaquePass, const b8 affectTransparentPass, Core::ECS::World& world){
    return AddCsgMeshReceiver<Impl::SkinnedCsgMeshComponent>(entity, receiverGroup, affectOpaquePass, affectTransparentPass, world);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
