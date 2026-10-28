// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"
#include "transform_helpers.h"

#include <impl/ecs_skeleton/runtime_helpers.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system_transforms{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


using AttachmentJointQuery = ModelAttachmentJointQuery;

static constexpr usize s_ParallelModelObjectTransformGrainSize = 256u;

static void ResolveAttachmentJointQueries(
    Core::ECS::World& world,
    const usize queryCount,
    const usize* const parentOrder,
    Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>& jointPalette,
    const NotNull<AttachmentJointQuery*> queries
){
    const auto queryAt = [&](const usize index)noexcept -> AttachmentJointQuery&{
        return queries.get()[parentOrder ? parentOrder[index] : index];
    };
    usize groupBegin = 0u;
    while(groupBegin < queryCount){
        const Core::ECS::EntityID parentEntity = queryAt(groupBegin).parentEntity;
        usize groupEnd = groupBegin + 1u;
        while(groupEnd < queryCount && queryAt(groupEnd).parentEntity == parentEntity)
            ++groupEnd;

        const SkeletonPoseComponent* const pose = world.tryGetComponent<SkeletonPoseComponent>(parentEntity);
        if(pose && SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*pose, jointPalette)){
            for(usize queryIndex = groupBegin; queryIndex < groupEnd; ++queryIndex){
                AttachmentJointQuery& query = queryAt(queryIndex);
                if(query.parentJointIndex >= jointPalette.size())
                    continue;
                query.jointMatrix = jointPalette[query.parentJointIndex];
                query.resolved = true;
            }
        }
        groupBegin = groupEnd;
    }
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Expected<void, Core::CpuTaskAdmissionError::Enum> ModelSystem::updateModelObjectTransforms(){
    return m_world.view<ModelObjectComponent, Scene::TransformComponent>().parallelEach(
        m_world.taskScope(),
        __hidden_model_system_transforms::s_ParallelModelObjectTransformGrainSize,
        [&](const Core::ECS::EntityID entity, ModelObjectComponent& object, Scene::TransformComponent& transform)noexcept{
            static_cast<void>(entity);
            if(object.kind == ModelObjectKind::StaticMesh)
                return;

            if(const auto decomposition = MatrixDecompose(MatrixMultiply(ModelTransformHelpers::LoadOwnerWorldMatrix(m_world, object.owner), LoadFloat(object.localTransform))))
                ModelTransformHelpers::StoreDecomposedTransform(*decomposition, transform);
        }
    );
}

void ModelSystem::updateStaticMeshAttachments(){
    const auto attachments = m_world.view<ModelObjectComponent, ModelStaticMeshAttachmentComponent, Scene::TransformComponent>();
    usize queryCount = 0u;
    if(attachments.candidateCount() > 1u){
        attachments.each([&](const Core::ECS::EntityID entity, const ModelObjectComponent& object, const ModelStaticMeshAttachmentComponent& attachment, const Scene::TransformComponent& transform)noexcept{
            static_cast<void>(entity);
            static_cast<void>(object);
            static_cast<void>(transform);
            if(attachment.parentEntity.valid() && attachment.parentJointIndex != s_SkeletonInvalidJointIndex)
                ++queryCount;
        });
    }

    // Attachment grouping storage is reused member state; zero or one joint query needs neither an index nor scratch.
    // Only capacity growth may allocate, never per-frame creation.
    Vector<ModelAttachmentJointQuery, Core::Alloc::GlobalArena>& jointQueries = m_attachmentJointQueries;
    jointQueries.clear();
    if(queryCount > 1u){
        jointQueries.reserve(queryCount);
        bool sharedParent = true;
        attachments.each([&](const Core::ECS::EntityID entity, const ModelObjectComponent& object, const ModelStaticMeshAttachmentComponent& attachment, const Scene::TransformComponent& transform)noexcept{
            static_cast<void>(entity);
            static_cast<void>(object);
            static_cast<void>(transform);
            if(!attachment.parentEntity.valid() || attachment.parentJointIndex == s_SkeletonInvalidJointIndex)
                return;
            if(!jointQueries.empty() && jointQueries.front().parentEntity != attachment.parentEntity)
                sharedParent = false;
            jointQueries.push_back(ModelAttachmentJointQuery{ .parentEntity = attachment.parentEntity, .parentJointIndex = attachment.parentJointIndex, });
        });
        const NotNull<ModelAttachmentJointQuery*> queries(jointQueries.data());
        if(sharedParent){
            __hidden_model_system_transforms::ResolveAttachmentJointQueries(m_world, queryCount, nullptr, m_scratchJoints, queries);
        }
        else{
            Vector<usize, Core::Alloc::GlobalArena>& parentOrder = m_attachmentParentOrder;
            parentOrder.clear();
            parentOrder.reserve(queryCount);
            for(usize queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
                parentOrder.push_back(queryIndex);
            Sort(parentOrder.begin(), parentOrder.end(), [&](const usize lhs, const usize rhs)noexcept{
                return jointQueries[lhs].parentEntity < jointQueries[rhs].parentEntity;
            });
            __hidden_model_system_transforms::ResolveAttachmentJointQueries(m_world, queryCount, parentOrder.data(), m_scratchJoints, queries);
        }
    }

    usize nextQuery = 0u;
    attachments.each([&](const Core::ECS::EntityID entity, ModelObjectComponent& object, ModelStaticMeshAttachmentComponent& attachment, Scene::TransformComponent& transform){
        static_cast<void>(entity);

        const Scene::TransformComponent* ownerTransform = m_world.tryGetComponent<Scene::TransformComponent>(object.owner);
        const Scene::TransformComponent* parentTransform = attachment.parentEntity.valid() ? m_world.tryGetComponent<Scene::TransformComponent>(attachment.parentEntity) : ownerTransform;
        const Scene::TransformComponent* basisTransform = parentTransform ? parentTransform : ownerTransform;
        const SIMDMatrix parentMatrix = basisTransform ? MatrixAffineTransformation(LoadFloat(basisTransform->scale), VectorZero(), LoadFloat(basisTransform->rotation), LoadFloat(basisTransform->position)) : MatrixIdentity();
        const SIMDMatrix localMatrix = LoadFloat(attachment.localTransform);

        SIMDMatrix worldTransform{};
        if(!attachment.parentEntity.valid() || attachment.parentJointIndex == s_SkeletonInvalidJointIndex){
            worldTransform = MatrixMultiply(parentMatrix, localMatrix);
        }
        else{
            SIMDMatrix jointMatrix{};
            if(queryCount > 1u){
                const ModelAttachmentJointQuery& query = jointQueries[nextQuery++];
                if(!query.resolved)
                    return;
                jointMatrix = LoadFloat(query.jointMatrix);
            }
            else{
                const SkeletonPoseComponent* const pose = m_world.tryGetComponent<SkeletonPoseComponent>(attachment.parentEntity);
                if(!pose || !SkeletonRuntime::BuildStoredJointPaletteFromSkeletonPose(*pose, m_scratchJoints) || attachment.parentJointIndex >= m_scratchJoints.size())
                    return;
                jointMatrix = LoadFloat(m_scratchJoints[attachment.parentJointIndex]);
            }
            worldTransform = MatrixMultiply(MatrixMultiply(parentMatrix, jointMatrix), localMatrix);
        }

        if(const auto decomposition = MatrixDecompose(worldTransform))
            ModelTransformHelpers::StoreDecomposedTransform(*decomposition, transform);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
