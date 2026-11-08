// limztudio@gmail.com
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


#include "skinning_benchmark_pose_animator.h"


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_BEGIN


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


namespace Tests::Smoke{


u32 SkinningBenchmarkPoseAnimator::RandomJointSeed(const u32 jointIndex, const u32 characterIndex)noexcept{
    u32 seed = jointIndex * 747796405u + characterIndex * 2891336453u + 277803737u;
    seed = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    return (seed >> 22u) ^ seed;
}

b8 SkinningBenchmarkPoseAnimator::ShouldAnimateJoint(const u32 jointIndex, const u32 characterIndex, const b8 staticPreview)noexcept{
    const u32 modulo = staticPreview ? s_StaticPreviewAnimatedJointModulo : s_AnimatedJointModulo;
    return jointIndex != 0u && (RandomJointSeed(jointIndex, characterIndex) % modulo) == 0u;
}

void SkinningBenchmarkPoseAnimator::BuildAnimatedJointMatrix(const SIMDMatrix& bindJoint, const u32 jointIndex, const u32 characterIndex, const f32 timeSeconds, const b8 staticPreview, SIMDMatrix& outMatrix)noexcept{
    outMatrix = bindJoint;
    if(!ShouldAnimateJoint(jointIndex, characterIndex, staticPreview))
        return;

    const u32 seed = RandomJointSeed(jointIndex, characterIndex);
    const SIMDVectorSingle phaseCandidates = VectorMultiply(VectorSet(static_cast<f32>(seed & 255u), static_cast<f32>(characterIndex), 0.0f, 0.0f), VectorSet(0.0245436926f, 1.04719758f, 0.0f, 0.0f));
    const SIMDVectorSingle phase = staticPreview ? VectorSplatX(phaseCandidates) : VectorSplatY(phaseCandidates);
    const f32 primarySpeed = staticPreview ? 1.15f : 0.34f;
    const f32 secondarySpeed = staticPreview ? 0.83f : 0.21f;
    const f32 baseAngle = staticPreview ? 0.20f : 0.024f;
    const f32 angleJitter = staticPreview ? 0.025f : 0.0025f;
    const f32 rollBase = staticPreview ? 0.045f : 0.006f;
    const f32 rollJitter = staticPreview ? 0.008f : 0.0015f;
    SIMDVectorSingle wavePhases = VectorMultiplyAddExpression(VectorSet(timeSeconds, timeSeconds, 0.0f, 0.0f), VectorSet(primarySpeed, secondarySpeed, 0.0f, 0.0f), VectorAndInt(phase, s_SIMDMaskXY));
    wavePhases = VectorAdd(wavePhases, VectorSet(0.0f, 0.5f, 0.0f, 0.0f));
    const SIMDVectorSingle waves = VectorSin(wavePhases);
    const SIMDVectorSingle coefficientIndices = VectorSet(static_cast<f32>((seed >> 16u) % 5u), static_cast<f32>((seed >> 3u) & 3u), static_cast<f32>((seed >> 6u) & 3u), static_cast<f32>((seed >> 9u) & 3u));
    const SIMDVectorSingle coefficients = VectorMultiplyAddExpression(coefficientIndices, VectorSet(angleJitter, 0.035f, 0.04f, rollJitter), VectorSet(baseAngle, 0.18f, 0.58f, rollBase));
    const SIMDVectorSingle angle = VectorMultiply(VectorSplatX(waves), VectorSplatX(coefficients));
    const SIMDVectorSingle rotationAngles = VectorMergeX(VectorMultiply(angle, VectorSplatY(coefficients)), VectorMultiply(angle, VectorSplatZ(coefficients)), VectorMultiply(VectorSplatY(waves), VectorSplatW(coefficients)), VectorZero());

    SIMDMatrix matrix = MatrixRotationRollPitchYawFromVector(rotationAngles);
    matrix = MatrixMultiply(bindJoint, matrix);
    if(!MatrixIsInvertibleAffine(matrix, NWB::Impl::s_SkeletonAffineEpsilon, NWB::Impl::s_SkeletonJointDeterminantEpsilon))
        return;

    outMatrix = matrix;
}


void SkinningBenchmarkPoseAnimator::Animate(Core::ECS::World& world, const Span<const Core::ECS::EntityID> entities, const Span<const Impl::SkeletonJointMatrix> bindJoints, const f32 timeSeconds, const b8 staticPreview)noexcept{
    for(usize entityIndex = 0u; entityIndex < entities.size(); ++entityIndex){
        auto* pose = world.tryGetComponent<NWB::Impl::SkeletonPoseComponent>(entities[entityIndex]);
        if(!pose)
            continue;

        for(u32 jointIndex = 0u; jointIndex < pose->localJoints.size(); ++jointIndex){
            SIMDMatrix animatedJoint;
            BuildAnimatedJointMatrix(LoadFloat(bindJoints[jointIndex]), jointIndex, static_cast<u32>(entityIndex), timeSeconds, staticPreview, animatedJoint);
            StoreFloat(animatedJoint, pose->localJoints[jointIndex]);
        }
    }
}


};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


NWB_END


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

