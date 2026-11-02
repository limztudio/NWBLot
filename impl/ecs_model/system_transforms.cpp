// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <impl/ecs_scene/view.h>
#include <impl/ecs_skeleton/runtime_helpers.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system_transforms{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResolveAttachmentJointQueries(const Span<const usize> parentOrder, Core::ECS::World& world, Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>& jointPalette, const Span<ModelAttachmentJointQuery> queries){
    const usize queryCount = queries.size();
    const auto queryAt = [&](const usize index)noexcept -> ModelAttachmentJointQuery&{
        return queries[!parentOrder.empty() ? parentOrder[index] : index];
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
                ModelAttachmentJointQuery& query = queryAt(queryIndex);
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
        s_ParallelModelObjectTransformGrainSize,
        [&]([[maybe_unused]] const Core::ECS::EntityID entity, ModelObjectComponent& object, Scene::TransformComponent& transform)noexcept{
            if(object.kind == ModelObjectKind::StaticMesh)
                return;

            if(const auto decomposition = MatrixDecompose(MatrixMultiply(Scene::LoadEntityTransformMatrix(m_world, object.owner), LoadFloat(object.localTransform))))
                Scene::StoreDecomposedTransform(*decomposition, transform);
        }
    );
}

void ModelSystem::updateStaticMeshAttachments(){
    const auto attachments = m_world.view<ModelObjectComponent, ModelStaticMeshAttachmentComponent, Scene::TransformComponent>();
    usize queryCount = 0u;
    if(attachments.candidateCount() > 1u){
        attachments.each([&]([[maybe_unused]] const Core::ECS::EntityID entity, [[maybe_unused]] const ModelObjectComponent& object, const ModelStaticMeshAttachmentComponent& attachment, [[maybe_unused]] const Scene::TransformComponent& transform)noexcept{
            if(attachment.hasJointAttachment())
                ++queryCount;
        });
    }

    // Attachment grouping storage is reused member state; zero or one joint query needs neither an index nor scratch.
    // Only capacity growth may allocate, never per-frame creation.
    Vector<ModelAttachmentJointQuery, Core::Alloc::GlobalArena>& jointQueries = m_attachmentJointQueries;
    jointQueries.clear();
    if(queryCount > 1u){
        jointQueries.reserve(queryCount);
        b8 sharedParent = true;
        attachments.each([&]([[maybe_unused]] const Core::ECS::EntityID entity, [[maybe_unused]] const ModelObjectComponent& object, const ModelStaticMeshAttachmentComponent& attachment, [[maybe_unused]] const Scene::TransformComponent& transform)noexcept{
            if(!attachment.hasJointAttachment())
                return;
            if(!jointQueries.empty() && jointQueries.front().parentEntity != attachment.parentEntity)
                sharedParent = false;
            jointQueries.push_back(MakeModelAttachmentJointQuery(attachment.parentEntity, attachment.parentJointIndex));
        });
        const Span<ModelAttachmentJointQuery> queries(jointQueries.data(), jointQueries.size());
        if(sharedParent)
            __hidden_model_system_transforms::ResolveAttachmentJointQueries({}, m_world, m_scratchJoints, queries);
        else{
            Vector<usize, Core::Alloc::GlobalArena>& parentOrder = m_attachmentParentOrder;
            parentOrder.clear();
            parentOrder.reserve(queryCount);
            for(usize queryIndex = 0u; queryIndex < queryCount; ++queryIndex)
                parentOrder.push_back(queryIndex);
            Sort(parentOrder.begin(), parentOrder.end(), [&](const usize lhs, const usize rhs)noexcept{
                return jointQueries[lhs].parentEntity < jointQueries[rhs].parentEntity;
            });
            __hidden_model_system_transforms::ResolveAttachmentJointQueries(Span<const usize>(parentOrder.data(), parentOrder.size()), m_world, m_scratchJoints, queries);
        }
    }

    usize nextQuery = 0u;
    attachments.each([&]([[maybe_unused]] const Core::ECS::EntityID entity, ModelObjectComponent& object, ModelStaticMeshAttachmentComponent& attachment, Scene::TransformComponent& transform){
        const Scene::TransformComponent* ownerTransform = m_world.tryGetComponent<Scene::TransformComponent>(object.owner);
        const Scene::TransformComponent* parentTransform = attachment.parentEntity.valid() ? m_world.tryGetComponent<Scene::TransformComponent>(attachment.parentEntity) : ownerTransform;
        const Scene::TransformComponent* basisTransform = parentTransform ? parentTransform : ownerTransform;
        const SIMDMatrix parentMatrix = basisTransform ? Scene::LoadTransformMatrix(*basisTransform) : MatrixIdentity();
        const SIMDMatrix localMatrix = LoadFloat(attachment.localTransform);

        SIMDMatrix worldTransform{};
        if(!attachment.hasJointAttachment())
            worldTransform = MatrixMultiply(parentMatrix, localMatrix);
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
                if(!pose)
                    return;
                const auto resolvedJointMatrix = SkeletonRuntime::EvaluateSkeletonPoseJointMatrix(*pose, attachment.parentJointIndex);
                if(!resolvedJointMatrix)
                    return;
                jointMatrix = *resolvedJointMatrix;
            }
            worldTransform = MatrixMultiply(MatrixMultiply(parentMatrix, jointMatrix), localMatrix);
        }

        if(const auto decomposition = MatrixDecompose(worldTransform))
            Scene::StoreDecomposedTransform(*decomposition, transform);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

