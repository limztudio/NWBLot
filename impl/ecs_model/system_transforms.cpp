// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "system.h"

#include <impl/ecs_scene/view.h>

#include <global/algorithm.h>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace __hidden_model_system_transforms{


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


static void ResolveAttachmentJointQueries(const Span<const usize> parentOrder, Core::ECS::World& world, Vector<SkeletonJointMatrix, Core::Alloc::GlobalArena>& jointPalette, const Span<ModelAttachmentJointQuery> queries){
    const usize queryCount = queries.size();
    const Span<const ModelAttachmentJointQuery> constQueries(queries.data(), queryCount);
    usize groupBegin = 0u;
    while(groupBegin < queryCount){
        const Core::ECS::EntityID parentEntity = queries[ResolveModelAttachmentQueryIndex(parentOrder, groupBegin)].parentEntity;
        const usize groupEnd = FindModelAttachmentJointQueryGroupEnd(parentOrder, constQueries, groupBegin);

        const SkeletonPoseComponent* const pose = world.tryGetComponent<SkeletonPoseComponent>(parentEntity);
        if(pose && TryBuildStoredJointPaletteVectorFromSkeletonPose(*pose, jointPalette)){
            const Span<const SkeletonJointMatrix> paletteSpan(jointPalette.data(), jointPalette.size());
            for(usize queryIndex = groupBegin; queryIndex < groupEnd; ++queryIndex){
                if(!TryResolveModelAttachmentJointQuery(paletteSpan, queries[ResolveModelAttachmentQueryIndex(parentOrder, queryIndex)]))
                    break;
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

            (void)Scene::TryDecomposeAndAssignTransformComponent(MatrixMultiply(Scene::LoadEntityTransformMatrix(m_world, object.owner), LoadFloat(object.localTransform)), transform);
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
            BuildSortedModelAttachmentParentOrder(queries, parentOrder);
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
                const auto resolvedJointMatrix = TryEvaluateSkeletonPoseJointMatrix(*pose, attachment.parentJointIndex);
                if(!resolvedJointMatrix)
                    return;
                jointMatrix = *resolvedJointMatrix;
            }
            worldTransform = MatrixMultiply(MatrixMultiply(parentMatrix, jointMatrix), localMatrix);
        }

        (void)Scene::TryDecomposeAndAssignTransformComponent(worldTransform, transform);
    });
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_IMPL_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
