// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#ifndef NWB_TESTS_SMOKE_CSG_SMOKE_HELPERS_H
#define NWB_TESTS_SMOKE_CSG_SMOKE_HELPERS_H

#include <global/core/ecs/world.h>
#include <global/core/mesh/frame_math.h>
#include <impl/ecs_csg/components.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace NWB{
namespace Tests{
namespace Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template<typename ParameterT>
inline void AssignCsgCutterParameters(Impl::CsgCutterComponent& cutter, const ParameterT& parameters){
    cutter.parameterBytes.resize(sizeof(ParameterT));
    NWB_MEMCPY(cutter.parameterBytes.data(), cutter.parameterBytes.size(), &parameters, sizeof(ParameterT));
}

inline void AssignCsgCutterTransform(
    Impl::CsgCutterComponent& cutter,
    const SIMDVector center,
    const SIMDVector rotation
){
    const SIMDMatrix shapeToWorld = MatrixAffineTransformation(s_SIMDOne, VectorZero(), rotation, center);
    SIMDVector determinant;
    const SIMDMatrix worldToShape = MatrixInverse(&determinant, shapeToWorld);
    StoreFloat(worldToShape, &cutter.worldToShape);
    StoreFloat(shapeToWorld, &cutter.shapeToWorld);
}

template<typename ReceiverComponentT>
inline ReceiverComponentT& AddCsgMeshReceiver(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name receiverGroup,
    const bool affectOpaquePass,
    const bool affectTransparentPass
){
    auto& receiver = world.addComponent<ReceiverComponentT>(entity);
    receiver.receiverGroup = receiverGroup;
    receiver.affectOpaquePass = affectOpaquePass;
    receiver.affectTransparentPass = affectTransparentPass;
    return receiver;
}

inline Impl::StaticCsgMeshComponent& AddStaticCsgMeshReceiver(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name receiverGroup,
    const bool affectOpaquePass,
    const bool affectTransparentPass
){
    return AddCsgMeshReceiver<Impl::StaticCsgMeshComponent>(
        world,
        entity,
        receiverGroup,
        affectOpaquePass,
        affectTransparentPass
    );
}

inline Impl::SkinnedCsgMeshComponent& AddSkinnedCsgMeshReceiver(
    Core::ECS::World& world,
    const Core::ECS::EntityID entity,
    const Name receiverGroup,
    const bool affectOpaquePass,
    const bool affectTransparentPass
){
    return AddCsgMeshReceiver<Impl::SkinnedCsgMeshComponent>(
        world,
        entity,
        receiverGroup,
        affectOpaquePass,
        affectTransparentPass
    );
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};
};
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#endif


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

