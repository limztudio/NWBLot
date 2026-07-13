// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include <impl/global.h>

#include <core/alloc/general.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct CsgReceiverComponent{
    Name receiverGroup = NAME_NONE;

    bool enabled = true;

    bool affectOpaquePass = true;
    bool affectTransparentPass = true;
};

struct StaticCsgMeshComponent : CsgReceiverComponent{
};

struct SkinnedCsgMeshComponent : CsgReceiverComponent{
};

static_assert(IsStandardLayout_V<CsgReceiverComponent>, "CsgReceiverComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<CsgReceiverComponent>, "CsgReceiverComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<StaticCsgMeshComponent>, "StaticCsgMeshComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<StaticCsgMeshComponent>, "StaticCsgMeshComponent must stay cheap to move in dense ECS storage");
static_assert(IsStandardLayout_V<SkinnedCsgMeshComponent>, "SkinnedCsgMeshComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<SkinnedCsgMeshComponent>, "SkinnedCsgMeshComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


[[nodiscard]] inline Float34 CsgIdentityTransform(){
    Float34 transform = {};
    transform._11 = 1.0f;
    transform._22 = 1.0f;
    transform._33 = 1.0f;
    return transform;
}

struct CsgCutterComponent{
    using ParameterByteVector = Vector<u8, Core::Alloc::GlobalArena>;

    Name receiverGroup = NAME_NONE;

    Name shapeType = NAME_NONE;

    Float34 worldToShape;
    Float34 shapeToWorld;

    ParameterByteVector parameterBytes;

    bool active = true;

    explicit CsgCutterComponent(Core::Alloc::GlobalArena& arena)
        : worldToShape(CsgIdentityTransform())
        , shapeToWorld(CsgIdentityTransform())
        , parameterBytes(arena)
    {}
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

