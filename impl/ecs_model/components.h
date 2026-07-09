// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#pragma once


#include "../global.h"

#include <global/core/ecs/entity_id.h>
#include <global/core/assets/ref.h>
#include <impl/assets_skeleton/joint_types.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class Model;
class Skeleton;


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelComponent{
    Core::Assets::AssetRef<Model> model;
};

static_assert(IsStandardLayout_V<ModelComponent>, "ModelComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelComponent>, "ModelComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace ModelObjectKind{
    enum Enum : u32{
        Skeleton,
        StaticMesh,
        SkinnedMesh,
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelRuntimeComponent{
    Name model = NAME_NONE;
    u32 objectCount = 0u;
};

static_assert(IsStandardLayout_V<ModelRuntimeComponent>, "ModelRuntimeComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelRuntimeComponent>, "ModelRuntimeComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelObjectComponent{
    Name model = NAME_NONE;
    Name object = NAME_NONE;
    SkeletonJointMatrix localTransform = MakeIdentitySkeletonJointMatrix();
    Core::ECS::EntityID owner = Core::ECS::ENTITY_ID_INVALID;
    u32 kind = ModelObjectKind::StaticMesh;
};

static_assert(IsStandardLayout_V<ModelObjectComponent>, "ModelObjectComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelObjectComponent>, "ModelObjectComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelSkeletonComponent{
    Core::Assets::AssetRef<Skeleton> skeleton;
};

static_assert(IsStandardLayout_V<ModelSkeletonComponent>, "ModelSkeletonComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelSkeletonComponent>, "ModelSkeletonComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


struct ModelStaticMeshAttachmentComponent{
    Name parentObject = NAME_NONE;
    Name parentJoint = NAME_NONE;
    Core::ECS::EntityID parentEntity = Core::ECS::ENTITY_ID_INVALID;
    u32 parentJointIndex = Limit<u32>::s_Max;
    SkeletonJointMatrix localTransform = MakeIdentitySkeletonJointMatrix();
};

static_assert(IsStandardLayout_V<ModelStaticMeshAttachmentComponent>, "ModelStaticMeshAttachmentComponent must stay layout-stable for ECS storage");
static_assert(IsTriviallyCopyable_V<ModelStaticMeshAttachmentComponent>, "ModelStaticMeshAttachmentComponent must stay cheap to move in dense ECS storage");


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

