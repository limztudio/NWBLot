// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


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

void AssignCsgCutterTransform(SIMDVectorSingle center, SIMDVectorSingle rotation, Impl::CsgCutterComponent& cutter)noexcept;
Impl::StaticCsgMeshComponent& AddStaticCsgMeshReceiver(Core::ECS::EntityID entity, Name receiverGroup, b8 affectOpaquePass, b8 affectTransparentPass, Core::ECS::World& world);
Impl::SkinnedCsgMeshComponent& AddSkinnedCsgMeshReceiver(Core::ECS::EntityID entity, Name receiverGroup, b8 affectOpaquePass, b8 affectTransparentPass, Core::ECS::World& world);


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

