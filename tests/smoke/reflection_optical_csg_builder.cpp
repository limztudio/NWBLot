// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "reflection_optical_csg_builder.h"

#include <impl/ecs_csg/shape_registry.h>

#include <global/simd/constant.h>
#include <global/simd/frame.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Core::ECS::EntityID ReflectionOpticalCsgBuilder::addPlane(const Core::ECS::EntityID receiver, const Name group, const b8 affectOpaque, const b8 affectTransparent, const Float4& normalDistance){
    AddStaticCsgMeshReceiver(receiver, group, affectOpaque, affectTransparent, m_world);
    auto cutterEntity = m_world.createEntity();
    auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>();
    cutter.receiverGroup = group;
    cutter.shapeType = Name("engine/csg/plane");
    Impl::CsgPlaneShapeParameters parameters;
    parameters.normalDistance = normalDistance;
    AssignCsgCutterParameters(parameters, cutter);
    AssignCsgCutterTransform(VectorZero(), QuaternionIdentity(), cutter);
    return cutterEntity.id();
}

void ReflectionOpticalCsgBuilder::addBox(const Core::ECS::EntityID receiver, const Name group, const Float4& halfExtents, const SIMDVectorSingle center){
    AddStaticCsgMeshReceiver(receiver, group, false, true, m_world);
    auto cutterEntity = m_world.createEntity();
    auto& cutter = cutterEntity.addComponent<Impl::CsgCutterComponent>();
    cutter.receiverGroup = group;
    cutter.shapeType = Name("engine/csg/box");
    Impl::CsgBoxShapeParameters parameters;
    parameters.halfExtents = halfExtents;
    AssignCsgCutterParameters(parameters, cutter);
    AssignCsgCutterTransform(center, QuaternionIdentity(), cutter);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

